#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "../mailbox.hpp"
#include "unroll.hpp"
#include <cstdint>

#define GROUP_ID __bsg_x
#define NUM_GROUPS bsg_tiles_X
#define CORE_ID __bsg_y
#define CORES_PER_GROUP bsg_tiles_Y

// parameters
#define REF_CORE (SEQ_LEN / CORES_PER_GROUP)
#define MATCH     1
#define MISMATCH -1
#define GAP       1

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int max(int a, int b, int c) {
  return max(a, max(b, c));
}

struct mailbox_t {
  int dp_val;
  int max;
  int qry;
  int idx;
};

hb_mailbox_state_t<mailbox_t> mailboxes = {};

// Per-core working buffers.
uint8_t ref_segment[REF_CORE + 1];
int dp_row_even[REF_CORE + 1];
int dp_row_odd[REF_CORE + 1];
int split_points[REF_CORE];

int boundary_scores[SEQ_LEN + 1];

inline void store_row_fields(mailbox_t *data, int dp_val, uint8_t qry) {
  *data = mailbox_t{dp_val, 0, qry, 0};
}

inline void store_result_fields(mailbox_t *data, int max_score, int idx) {
  *data = mailbox_t{0, max_score, 0, idx};
}

inline mailbox_t receive_previous(hb_mailbox_state_t<mailbox_t> *mailbox, int direction) {
  mailbox_t incoming;
  if (direction == 1) {
    check_and_clear_left(mailbox);
    incoming = read_left(mailbox);
    set_left_ready(mailbox);
  } else {
    check_and_clear_right(mailbox);
    incoming = read_right(mailbox);
    set_right_ready(mailbox);
  }
  return incoming;
}

inline mailbox_t receive_next(hb_mailbox_state_t<mailbox_t> *mailbox, int direction) {
  mailbox_t incoming;
  if (direction == 1) {
    check_and_clear_right(mailbox);
    incoming = read_right(mailbox);
    set_right_ready(mailbox);
  } else {
    check_and_clear_left(mailbox);
    incoming = read_left(mailbox);
    set_left_ready(mailbox);
  }
  return incoming;
}

inline void send_previous_result(
  hb_mailbox_state_t<mailbox_t> *mailbox,
  int direction,
  int max_score,
  int idx
) {
  if (direction == 1) {
    check_and_clear_left_ready(mailbox);
    store_result_fields(&mailbox->left_mailbox->data, max_score, idx);
    set_left_full(mailbox);
  } else {
    check_and_clear_right_ready(mailbox);
    store_result_fields(&mailbox->right_mailbox->data, max_score, idx);
    set_right_full(mailbox);
  }
}

inline void send_next_row(
  hb_mailbox_state_t<mailbox_t> *mailbox,
  int direction,
  int dp_val,
  uint8_t qry
) {
  if (direction == 1) {
    check_and_clear_right_ready(mailbox);
    store_row_fields(&mailbox->right_mailbox->data, dp_val, qry);
    set_right_full(mailbox);
  } else {
    check_and_clear_left_ready(mailbox);
    store_row_fields(&mailbox->left_mailbox->data, dp_val, qry);
    set_left_full(mailbox);
  }
}


int parallel_fill(
  uint8_t *query,
  int      start, // inclusive
  int      end, // exclusive
  int      direction, // 1 for forward, -1 for backward
  int      active_core_id,
  int      active_cores
) {
  hb_mailbox_state_t<mailbox_t> *mailbox = &mailboxes;
  const bool is_first = (active_core_id == 0);
  const bool is_last = (active_core_id == active_cores - 1);
  const bool is_forward = (direction == 1);
  const int query_span = end - start;

  volatile int *neighbor_boundary_scores =
    (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y + direction, (void *)&boundary_scores[0]);

  int *current_row = dp_row_even;
  int *previous_row = dp_row_odd;

  int max_score = INT32_MIN;
  int max_idx   = 0;

  for (int col = 0; col <= REF_CORE; col++) {
    previous_row[col] = -(active_core_id * REF_CORE * GAP) - col * GAP;
  }

  boundary_scores[0] = previous_row[REF_CORE];

  query = is_forward ? (query + start) : (query + end - 1);

  for (int row = 0; row < query_span; row++) {
    uint8_t query_char;

    if (is_first) {
      query_char = *query;
      query += direction;
      current_row[0] = -(active_core_id * REF_CORE * GAP) - (row + 1) * GAP;
    } else {
      const mailbox_t incoming = receive_previous(mailbox, direction);
      current_row[0] = incoming.dp_val;
      query_char = incoming.qry;
    }

    uint8_t *ref_ptr = &ref_segment[is_forward ? 1 : REF_CORE];

    for (int col = 1; col <= REF_CORE; col++) {
      const int match = (query_char == *ref_ptr) ? MATCH : MISMATCH;
      ref_ptr += direction;

      const int score_diag = previous_row[col - 1] + match;
      const int score_up = previous_row[col] - GAP;
      const int score_left = current_row[col - 1] - GAP;

      current_row[col] = max(score_diag, score_up, score_left);
    }

    if (is_last) {
      boundary_scores[row + 1] = current_row[REF_CORE];
    } else {
      send_next_row(mailbox, direction, current_row[REF_CORE], query_char);
    }

    // alternate the two dp row buffers across iterations.
    int *tmp = current_row;
    current_row = previous_row;
    previous_row = tmp;
  }

  if (is_last) {
    bsg_fence();
    if (direction == 1) {
      check_and_clear_right_ready(mailbox);
      mailbox->right_mailbox->data = mailbox_t{};
      set_right_full(mailbox);
    } else {
      check_and_clear_left_ready(mailbox);
      mailbox->left_mailbox->data = mailbox_t{};
      set_left_full(mailbox);
    }
    receive_next(mailbox, direction);

    int best_row = start;
    for (int idx = 0; idx <= query_span; idx++) {
      const int candidate_row = is_forward ? (start + idx) : (end - idx);
      const int score = boundary_scores[idx] + neighbor_boundary_scores[query_span - idx];
      if ((score > max_score) || ((score == max_score) && (candidate_row < best_row))) {
        max_score = score;
        best_row = candidate_row;
      }
    }

    max_idx = best_row;

    if (direction == 1) {
      check_and_clear_right_ready(mailbox);
      store_result_fields(&mailbox->right_mailbox->data, max_score, max_idx);
      set_right_full(mailbox);
    } else {
      check_and_clear_left_ready(mailbox);
      store_result_fields(&mailbox->left_mailbox->data, max_score, max_idx);
      set_left_full(mailbox);
    }

    const mailbox_t incoming_max = receive_next(mailbox, direction);

    if (incoming_max.max > max_score) {
      max_score = incoming_max.max;
      max_idx = incoming_max.idx;
    }

    if (direction == -1) {
      split_points[0] = max_idx;
    }
  }

  const mailbox_t propagated_result = is_last
    ? mailbox_t{0, max_score, 0, max_idx}
    : receive_next(mailbox, direction);

  if (!is_first) {
    send_previous_result(mailbox, direction, propagated_result.max, propagated_result.idx);
  }

  return propagated_result.idx;
}

// Kernel main;
extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int* path, int pod_id)
{
  bsg_barrier_tile_group_init();
  init_mailboxes(&mailboxes);
  reset_mailboxes(&mailboxes);
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  // Each group processes a set of sequences
  for (int seq_id = GROUP_ID; seq_id < NUM_SEQ; seq_id += NUM_GROUPS) {
    unrolled_load<uint8_t, REF_CORE>(
      &ref_segment[1],
      &ref[SEQ_LEN * seq_id + (CORE_ID * REF_CORE)]
    );

    for (int col = 0; col < REF_CORE; col++) {
      split_points[col] = -1;
    }

    int query_low = 0;
    int query_high = SEQ_LEN;

    for (int active_cores = 4; active_cores > 1; active_cores >>= 1) {
      int split_idx;
      int direction = 1;
      int active_core_id = CORE_ID & (active_cores - 1);

      if (active_cores & CORE_ID) {
        direction = -1;
        active_core_id ^= (active_cores - 1);
      }

      split_idx = parallel_fill(
        &qry[seq_id * SEQ_LEN],
        query_low,
        query_high,
        direction,
        active_core_id,
        active_cores
      );

      if (CORE_ID == 0 && active_cores == 4) {
        output[seq_id] = peek_right(&mailboxes).max;
      }

      if (active_cores & CORE_ID) {
        query_low = split_idx;
      } else { 
        query_high = split_idx;
      }
    }

    for (int col = 0; col < REF_CORE; col++) {
      path[(seq_id * SEQ_LEN) + (CORE_ID * REF_CORE) + col] = split_points[col];
    }
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
