#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "../../nw/mailbox.hpp"
#include <cstdint>

#define GROUP_ID __bsg_x
#define NUM_GROUPS bsg_tiles_X
#define CORE_ID __bsg_y
#define CORES_PER_GROUP bsg_tiles_Y

#ifndef COL
#define COL 1
#endif

#ifndef BAND_SIZE
#define BAND_SIZE (2 * CORES_PER_GROUP)
#endif

#define MATCH     1
#define MISMATCH -1
#define GAP       1
#define BLOCK_STRIDE (CORES_PER_GROUP * COL)
#define MAX_LOCAL_BLOCKS ((SEQ_LEN + BLOCK_STRIDE - 1) / BLOCK_STRIDE)
#define MAX_LOCAL_COLS (MAX_LOCAL_BLOCKS * COL)

#if COL <= 0
#error "COL must be positive"
#endif

#if BAND_SIZE <= 0
#error "BAND_SIZE must be positive"
#endif

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int max(int a, int b, int c) {
  return max(a, max(b, c));
}

inline int max(int a, int b, int c, int d) {
  return max(max(a, b), max(c, d));
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

struct mailbox_t {
  int dp_left;
  int dp_diag;
  uint8_t qry_char;
};

hb_mailbox_state_t<mailbox_t> mailboxes = {};

uint8_t refbuf[MAX_LOCAL_COLS];
int prev_dp[MAX_LOCAL_COLS];
int next_row_diag_seed = 0;

inline int min3(int a, int b, int c) {
  return min(a, min(b, c));
}

inline int total_blocks() {
  return (SEQ_LEN + COL - 1) / COL;
}

inline int local_block_count() {
  const int blocks = total_blocks();
  if (blocks <= CORE_ID) {
    return 0;
  }

  return 1 + ((blocks - 1 - CORE_ID) / CORES_PER_GROUP);
}

inline int core_for_col(int col) {
  return (col / COL) % CORES_PER_GROUP;
}

inline int first_owned_block(int start_block) {
  return start_block + ((CORE_ID - (start_block % CORES_PER_GROUP) + CORES_PER_GROUP) % CORES_PER_GROUP);
}

inline int block_start_col(int global_block) {
  return global_block * COL;
}

inline int block_width(int global_block) {
  return min(COL, SEQ_LEN - block_start_col(global_block));
}

inline int local_block_index(int global_block) {
  return global_block / CORES_PER_GROUP;
}

inline int local_col_index(int local_block, int offset) {
  return (local_block * COL) + offset;
}

inline mailbox_t receive_left_token(hb_mailbox_state_t<mailbox_t> *mailbox) {
  check_and_clear_left(mailbox);
  mailbox_t incoming = read_left(mailbox);
  set_left_ready(mailbox);
  return incoming;
}

inline void send_right_token(hb_mailbox_state_t<mailbox_t> *mailbox, const mailbox_t &outgoing) {
  check_and_clear_right_ready(mailbox);
  mailbox->right_mailbox->data = outgoing;
  asm volatile("" ::: "memory");
  set_right_full(mailbox);
}

inline void init_ring_mailboxes(hb_mailbox_state_t<mailbox_t> *mailbox) {
  init_mailboxes(mailbox);

  const int left_core = (CORE_ID == 0) ? (CORES_PER_GROUP - 1) : (CORE_ID - 1);
  const int right_core = (CORE_ID == (CORES_PER_GROUP - 1)) ? 0 : (CORE_ID + 1);

  mailbox->left_mailbox =
    (hb_mailbox_port_t<mailbox_t> *)bsg_remote_ptr(__bsg_x, left_core, &mailbox->right);
  mailbox->right_mailbox =
    (hb_mailbox_port_t<mailbox_t> *)bsg_remote_ptr(__bsg_x, right_core, &mailbox->left);

  mailbox->left_ready_flag =
    (volatile int *)bsg_remote_ptr(__bsg_x, left_core, (void *)&mailbox->right_ready);
  mailbox->right_ready_flag =
    (volatile int *)bsg_remote_ptr(__bsg_x, right_core, (void *)&mailbox->left_ready);
}

extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  init_ring_mailboxes(&mailboxes);
  reset_mailboxes(&mailboxes);
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int num_local_blocks = local_block_count();

  for (int s = GROUP_ID; s < NUM_SEQ; s += NUM_GROUPS) {
    const int seq_offset = SEQ_LEN * s;

    for (int local_block = 0; local_block < num_local_blocks; local_block++) {
      const int global_block = CORE_ID + (local_block * CORES_PER_GROUP);
      const int block_start = block_start_col(global_block);
      const int width = block_width(global_block);
      const int base = local_col_index(local_block, 0);

      for (int offset = 0; offset < width; offset++) {
        refbuf[base + offset] = ref[seq_offset + block_start + offset];
        prev_dp[base + offset] = 0;
      }
    }

    int local_max = 0;
    next_row_diag_seed = 0;

    for (int row = 0; row < SEQ_LEN; row++) {
      const int start_col = row;
      const int end_col = min(SEQ_LEN, row + BAND_SIZE);
      const int start_block = start_col / COL;
      const int end_block = (end_col - 1) / COL;
      const int row_start_core = core_for_col(start_col);
      const int next_start_core = (row + 1 < SEQ_LEN) ? core_for_col(start_col + 1) : row_start_core;
      const int first_block = first_owned_block(start_block);
      const uint8_t starter_qry = (row_start_core == CORE_ID) ? qry[seq_offset + row] : 0;
      int row_seed = 0;
      int starter_value = 0;

      for (int global_block = first_block; global_block <= end_block; global_block += CORES_PER_GROUP) {
        const int local_block = local_block_index(global_block);
        const int block_start = block_start_col(global_block);
        const int block_stop = min3(SEQ_LEN, end_col, block_start + COL);
        const int offset_start = (global_block == start_block) ? (start_col - block_start) : 0;
        const int offset_stop = block_stop - block_start;
        int left;
        int diag;
        uint8_t qry_char;

        if (global_block == start_block) {
          left = 0;
          diag = next_row_diag_seed;
          qry_char = starter_qry;
        } else {
          const mailbox_t incoming = receive_left_token(&mailboxes);
          left = incoming.dp_left;
          diag = incoming.dp_diag;
          qry_char = incoming.qry_char;
        }

        for (int offset = offset_start; offset < offset_stop; offset++) {
          const int global_col = block_start + offset;
          const int local_idx = local_col_index(local_block, offset);
          const int up = prev_dp[local_idx];
          const int match = (qry_char == refbuf[local_idx]) ? MATCH : MISMATCH;
          const int score_diag = diag + match;
          const int score_up = up - GAP;
          const int score_left = left - GAP;
          const int value = max(0, score_diag, score_up, score_left);

          prev_dp[local_idx] = value;
          if (value > local_max) {
            local_max = value;
          }

          if (global_col == start_col) {
            starter_value = value;
          } else if (global_col == (start_col + 1)) {
            row_seed = left;
          }

          diag = up;
          left = value;
        }

        if (global_block != end_block) {
          send_right_token(&mailboxes, mailbox_t{left, diag, qry_char});
        }
      }

      if ((start_col + 1) >= end_col && (row + 1) < SEQ_LEN) {
        if (CORE_ID == row_start_core) {
          if (next_start_core == CORE_ID) {
            row_seed = starter_value;
          } else {
            send_right_token(&mailboxes, mailbox_t{starter_value, 0, starter_qry});
          }
        } else if (CORE_ID == next_start_core) {
          const mailbox_t incoming = receive_left_token(&mailboxes);
          row_seed = incoming.dp_left;
        }
      }

      next_row_diag_seed = row_seed;
    }

    int reduced_max = local_max;
    for (int step = 0; step < (CORES_PER_GROUP - 1); step++) {
      if (CORE_ID == step) {
        send_right_token(&mailboxes, mailbox_t{reduced_max, 0, 0});
      } else if (CORE_ID == (step + 1)) {
        const mailbox_t incoming = receive_left_token(&mailboxes);
        if (incoming.dp_left > reduced_max) {
          reduced_max = incoming.dp_left;
        }
      }
    }

    if (CORE_ID == (CORES_PER_GROUP - 1)) {
      output[s] = reduced_max;
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
