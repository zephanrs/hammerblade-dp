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

// a band of width BAND_SIZE can overlap one extra chunk when the left edge is unaligned.
#define MAX_ACTIVE_LOCAL_CHUNKS (1 + ((BAND_SIZE + COL - 2) / (COL * CORES_PER_GROUP)))
#define MAX_ACTIVE_LOCAL_COLS (MAX_ACTIVE_LOCAL_CHUNKS * COL)

// keep the mailbox payload compact because it moves at every handoff.
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
  uint8_t qry_char;
};

hb_mailbox_state_t<mailbox_t> mailboxes = {};

// each chunk is one contiguous COL-wide group of reference columns.
// a core only keeps the chunks that can be active for it at the same time.
uint8_t refbuf[MAX_ACTIVE_LOCAL_COLS];
int prev_dp[MAX_ACTIVE_LOCAL_COLS];
int chunk_diag_seed[MAX_ACTIVE_LOCAL_CHUNKS];
int slot_chunk_id[MAX_ACTIVE_LOCAL_CHUNKS];

// this is the diagonal seed for the first active cell on the next row.
int next_row_diag_seed = 0;

inline mailbox_t receive_left_token(hb_mailbox_state_t<mailbox_t> *mailbox) {
  check_and_clear_left(mailbox);
  mailbox_t incoming = read_left(mailbox);

  // core 0 wraps the ready signal back to the last core in the ring.
  if (CORE_ID == 0) {
    *(volatile int *)bsg_remote_ptr(__bsg_x, CORES_PER_GROUP - 1, (void *)&mailbox->right_ready) = 1;
  } else {
    set_left_ready(mailbox);
  }
  return incoming;
}

inline void send_right_token(hb_mailbox_state_t<mailbox_t> *mailbox, const mailbox_t &outgoing) {
  check_and_clear_right_ready(mailbox);

  // the last core wraps to core 0 while interior cores use mailbox.hpp links.
  hb_mailbox_port_t<mailbox_t> *target =
    (CORE_ID == (CORES_PER_GROUP - 1))
      ? (hb_mailbox_port_t<mailbox_t> *)bsg_remote_ptr(__bsg_x, 0, &mailbox->left)
      : mailbox->right_mailbox;
  target->data = outgoing;
  asm volatile("" ::: "memory");
  target->full = 1;
}

extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  init_mailboxes(&mailboxes);
  reset_mailboxes(&mailboxes);
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  // each x-group handles an independent sequence stream.
  for (int s = GROUP_ID; s < NUM_SEQ; s += NUM_GROUPS) {
    const int seq_offset = SEQ_LEN * s;

    // reset the small active window for this sequence.
    for (int slot = 0; slot < MAX_ACTIVE_LOCAL_CHUNKS; slot++) {
      slot_chunk_id[slot] = -1;
      chunk_diag_seed[slot] = 0;
    }

    int local_max = 0;
    next_row_diag_seed = 0;

    // the band slides one column to the right on every row.
    for (int row = 0; row < SEQ_LEN; row++) {
      const int start_col = row;
      const int end_col = min(SEQ_LEN, row + BAND_SIZE);
      const int start_chunk = start_col / COL;
      const int end_chunk = (end_col - 1) / COL;
      const int row_start_core = start_chunk % CORES_PER_GROUP;
      const int next_start_core =
        (row + 1 < SEQ_LEN) ? (((start_col + 1) / COL) % CORES_PER_GROUP) : row_start_core;
      const int next_start_chunk = (row + 1 < SEQ_LEN) ? ((row + 1) / COL) : start_chunk;
      const int first_chunk =
        start_chunk + ((CORE_ID - (start_chunk % CORES_PER_GROUP) + CORES_PER_GROUP) % CORES_PER_GROUP);
      const uint8_t starter_qry = (row_start_core == CORE_ID) ? qry[seq_offset + row] : 0;

      // row_seed becomes the diagonal seed for the next row's first active cell.
      int row_seed = 0;
      int starter_value = 0;
      int right_edge_value = 0;

      // each owned chunk covers multiple contiguous columns before one edge handoff.
      for (int global_chunk = first_chunk, local_chunk = (first_chunk - CORE_ID) / CORES_PER_GROUP;
           global_chunk <= end_chunk;
           global_chunk += CORES_PER_GROUP, local_chunk++) {
        const int chunk_start = global_chunk * COL;
        const int width = min(COL, SEQ_LEN - chunk_start);
        const int chunk_stop = min(SEQ_LEN, min(end_col, chunk_start + width));
        const int offset_start = (global_chunk == start_chunk) ? (start_col - chunk_start) : 0;
        const int offset_stop = chunk_stop - chunk_start;
        const int slot = local_chunk % MAX_ACTIVE_LOCAL_CHUNKS;
        const int base = slot * COL;
        int left;
        int diag;
        uint8_t qry_char;

        // load the chunk into its active slot the first time it enters the band.
        if (slot_chunk_id[slot] != global_chunk) {
          slot_chunk_id[slot] = global_chunk;
          chunk_diag_seed[slot] = 0;

          for (int offset = 0; offset < width; offset++) {
            refbuf[base + offset] = ref[seq_offset + chunk_start + offset];
            prev_dp[base + offset] = 0;
          }
        }

        if (global_chunk == start_chunk) {
          left = 0;
          diag = next_row_diag_seed;
          qry_char = starter_qry;
        } else {
          const mailbox_t incoming = receive_left_token(&mailboxes);
          left = incoming.dp_left;
          // each chunk remembers the previous row's boundary value from its left neighbor.
          diag = chunk_diag_seed[slot];
          qry_char = incoming.qry_char;
          chunk_diag_seed[slot] = incoming.dp_left;
        }

        // once the boundary state arrives, the whole local chunk runs from scratchpad.
        for (int offset = offset_start; offset < offset_stop; offset++) {
          const int global_col = chunk_start + offset;
          const int local_idx = base + offset;
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
            // the second active cell's left neighbor is the next row's starting diagonal.
            row_seed = left;
          }

          diag = up;
          left = value;
        }

        // only the right edge state leaves this core.
        if (global_chunk != end_chunk) {
          send_right_token(&mailboxes, mailbox_t{left, qry_char});
        } else {
          right_edge_value = left;
        }
      }

      // seed a newly entering right-edge chunk so next row can reuse the cached diagonal.
      if (row + 1 < SEQ_LEN) {
        const int next_end_col = min(SEQ_LEN, row + 1 + BAND_SIZE);
        const int next_end_chunk = (next_end_col - 1) / COL;
        if ((next_end_chunk > end_chunk) && (next_end_chunk != next_start_chunk)) {
          const int entering_core = next_end_chunk % CORES_PER_GROUP;

          // when a new chunk enters from the right, preseed its diagonal cache one row early.
          if (CORE_ID == (end_chunk % CORES_PER_GROUP)) {
            send_right_token(&mailboxes, mailbox_t{right_edge_value, 0});
          } else if (CORE_ID == entering_core) {
            const mailbox_t incoming = receive_left_token(&mailboxes);
            const int local_chunk = (next_end_chunk - CORE_ID) / CORES_PER_GROUP;
            const int slot = local_chunk % MAX_ACTIVE_LOCAL_CHUNKS;
            const int chunk_start = next_end_chunk * COL;
            const int width = min(COL, SEQ_LEN - chunk_start);
            const int base = slot * COL;

            // preseed the slot one row early so the new right edge has the right diagonal.
            if (slot_chunk_id[slot] != next_end_chunk) {
              slot_chunk_id[slot] = next_end_chunk;
              for (int offset = 0; offset < width; offset++) {
                refbuf[base + offset] = ref[seq_offset + chunk_start + offset];
                prev_dp[base + offset] = 0;
              }
            }

            chunk_diag_seed[slot] = incoming.dp_left;
          }
        }
      }

      // a single-cell band still has to move the next starter state correctly.
      if ((start_col + 1) >= end_col && (row + 1) < SEQ_LEN) {
        if (CORE_ID == row_start_core) {
          if (next_start_core == CORE_ID) {
            row_seed = starter_value;
          } else {
            send_right_token(&mailboxes, mailbox_t{starter_value, starter_qry});
          }
        } else if (CORE_ID == next_start_core) {
          const mailbox_t incoming = receive_left_token(&mailboxes);
          row_seed = incoming.dp_left;
        }
      }

      next_row_diag_seed = row_seed;
    }

    // reduce the local maxima around the same y-ring.
    int reduced_max = local_max;
    for (int step = 0; step < (CORES_PER_GROUP - 1); step++) {
      if (CORE_ID == step) {
        send_right_token(&mailboxes, mailbox_t{reduced_max, 0});
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
