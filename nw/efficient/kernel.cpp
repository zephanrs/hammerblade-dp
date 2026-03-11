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
// local base-case matrix dimension, separate from the ref columns owned by a core
#define LOCAL_BASE_CASE_DIM 16
#define LOCAL_DP_WORDS ((LOCAL_BASE_CASE_DIM + 1) * (LOCAL_BASE_CASE_DIM + 1))
#define LOCAL_BUFFER_WORDS ((LOCAL_DP_WORDS > (SEQ_LEN + 1)) ? LOCAL_DP_WORDS : (SEQ_LEN + 1))
#define LOCAL_STACK_CAPACITY (REF_CORE + 1)

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

struct local_task_t {
  int query_start;
  int query_end;
  int ref_start;
  int ref_end;
};

hb_mailbox_state_t<mailbox_t> mailboxes = {};

// Per-core working buffers.
uint8_t ref_segment[REF_CORE + 1];
int dp_row_even[LOCAL_BUFFER_WORDS];
int dp_row_odd[LOCAL_BUFFER_WORDS];
int split_points[REF_CORE];
int boundary_scores[SEQ_LEN + 1];
local_task_t local_task_stack[LOCAL_STACK_CAPACITY];

inline void store_row_fields(mailbox_t *data, int dp_val, uint8_t qry) {
  data->dp_val = dp_val;
  data->qry = qry;
}

inline void store_result_fields(mailbox_t *data, int max_score, int idx) {
  data->max = max_score;
  data->idx = idx;
}

inline mailbox_t receive_mailbox(
  hb_mailbox_port_t<mailbox_t> *mailbox,
  volatile int *remote_ready
) {
  hb_wait_flag(&mailbox->full);
  mailbox->full = 0;
  const mailbox_t incoming = mailbox->data;
  *remote_ready = 1;
  return incoming;
}

inline void wait_for_ready(volatile int *ready) {
  hb_wait_flag(ready);
  *ready = 0;
}

void local_traceback(
  uint8_t *query,
  int query_start,
  int query_end,
  int ref_start,
  int ref_end
) {
  int *dp = dp_row_even;
  int *tb = dp_row_odd;
  const int query_span = query_end - query_start;
  const int ref_span = ref_end - ref_start;
  const int stride = ref_span + 1;

  for (int ref_col = 0; ref_col <= ref_span; ref_col++) {
    dp[ref_col] = -ref_col * GAP;
    tb[ref_col] = 2;
  }

  for (int query_row = 0; query_row <= query_span; query_row++) {
    dp[query_row * stride] = -query_row * GAP;
    tb[query_row * stride] = 1;
  }
  tb[0] = 0;

  for (int query_row = 1; query_row <= query_span; query_row++) {
    const uint8_t query_char = query[query_start + query_row - 1];
    for (int ref_col = 1; ref_col <= ref_span; ref_col++) {
      const uint8_t ref_char = ref_segment[ref_start + ref_col];
      const int diag = dp[(query_row - 1) * stride + ref_col - 1] +
        ((query_char == ref_char) ? MATCH : MISMATCH);
      const int up = dp[(query_row - 1) * stride + ref_col] - GAP;
      const int left = dp[query_row * stride + ref_col - 1] - GAP;
      int best = max(diag, up, left);
      int dir = 0;
      if ((up > diag) && (up >= left)) {
        dir = 1;
      } else if ((left > diag) && (left > up)) {
        dir = 2;
      }

      dp[query_row * stride + ref_col] = best;
      tb[query_row * stride + ref_col] = dir;
    }
  }

  int query_row = query_span;
  int ref_col = ref_span;
  while (query_row > 0 || ref_col > 0) {
    if (ref_col < ref_span) {
      split_points[ref_start + ref_col] = query_start + query_row;
    }

    const int dir = tb[query_row * stride + ref_col];
    if (dir == 0) {
      query_row--;
      ref_col--;
    } else if (dir == 1) {
      query_row--;
    } else {
      ref_col--;
    }
  }

  split_points[ref_start] = query_start;
}

void local_fill(
  uint8_t *query,
  int query_start,
  int query_end,
  int ref_start,
  int ref_end
) {
  int stack_size = 0;
  const local_task_t root_task = {query_start, query_end, ref_start, ref_end};
  local_task_stack[stack_size++] = root_task;

  while (stack_size > 0) {
    const local_task_t task = local_task_stack[--stack_size];
    const int task_query_span = task.query_end - task.query_start;
    const int task_ref_span = task.ref_end - task.ref_start;

    if (task_ref_span <= 0) {
      continue;
    }

    if ((task_query_span <= LOCAL_BASE_CASE_DIM) &&
        (task_ref_span <= LOCAL_BASE_CASE_DIM)) {
      local_traceback(query, task.query_start, task.query_end, task.ref_start, task.ref_end);
      continue;
    }

    const int ref_mid = task.ref_start + (task_ref_span >> 1);
    int *forward_prev = boundary_scores;
    int *forward_curr = dp_row_even;
    int *forward_scores = dp_row_odd;

    for (int row = 0; row <= task_query_span; row++) {
      forward_prev[row] = -row * GAP;
    }

    for (int ref_col = task.ref_start; ref_col < ref_mid; ref_col++) {
      forward_curr[0] = -(ref_col - task.ref_start + 1) * GAP;
      const uint8_t ref_char = ref_segment[ref_col + 1];
      for (int row = 1; row <= task_query_span; row++) {
        const uint8_t query_char = query[task.query_start + row - 1];
        const int diag = forward_prev[row - 1] + ((query_char == ref_char) ? MATCH : MISMATCH);
        const int up = forward_prev[row] - GAP;
        const int left = forward_curr[row - 1] - GAP;
        forward_curr[row] = max(diag, up, left);
      }

      int *tmp = forward_prev;
      forward_prev = forward_curr;
      forward_curr = tmp;
    }

    for (int row = 0; row <= task_query_span; row++) {
      forward_scores[row] = forward_prev[row];
      boundary_scores[row] = -row * GAP;
    }

    int *backward_prev = boundary_scores;
    int *backward_curr = dp_row_even;
    for (int ref_col = task.ref_end - 1; ref_col >= ref_mid; ref_col--) {
      backward_curr[0] = -(task.ref_end - ref_col) * GAP;
      const uint8_t ref_char = ref_segment[ref_col + 1];
      for (int row = 1; row <= task_query_span; row++) {
        const uint8_t query_char = query[task.query_end - row];
        const int diag = backward_prev[row - 1] + ((query_char == ref_char) ? MATCH : MISMATCH);
        const int up = backward_prev[row] - GAP;
        const int left = backward_curr[row - 1] - GAP;
        backward_curr[row] = max(diag, up, left);
      }

      int *tmp = backward_prev;
      backward_prev = backward_curr;
      backward_curr = tmp;
    }

    int best_query = task.query_start;
    int best_score = INT32_MIN;
    for (int row = 0; row <= task_query_span; row++) {
      const int candidate_query = task.query_start + row;
      const int score = forward_scores[row] + backward_prev[task_query_span - row];
      if ((score > best_score) || ((score == best_score) && (candidate_query < best_query))) {
        best_score = score;
        best_query = candidate_query;
      }
    }

    split_points[ref_mid] = best_query;
    const local_task_t right_task = {best_query, task.query_end, ref_mid, task.ref_end};
    local_task_stack[stack_size++] = right_task;
    const local_task_t left_task = {task.query_start, best_query, task.ref_start, ref_mid};
    local_task_stack[stack_size++] = left_task;
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
  const int ref_offset = active_core_id * REF_CORE;

  volatile int *neighbor_boundary_scores =
    (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y + direction, (void *)&boundary_scores[0]);
  hb_mailbox_port_t<mailbox_t> *previous_mailbox = is_forward ? &mailbox->left : &mailbox->right;
  hb_mailbox_port_t<mailbox_t> *next_mailbox = is_forward ? &mailbox->right : &mailbox->left;
  hb_mailbox_port_t<mailbox_t> *previous_remote_mailbox =
    is_forward ? mailbox->left_mailbox : mailbox->right_mailbox;
  hb_mailbox_port_t<mailbox_t> *next_remote_mailbox =
    is_forward ? mailbox->right_mailbox : mailbox->left_mailbox;
  volatile int *previous_ready = is_forward ? &mailbox->left_ready : &mailbox->right_ready;
  volatile int *next_ready = is_forward ? &mailbox->right_ready : &mailbox->left_ready;
  volatile int *previous_remote_ready =
    is_forward ? mailbox->left_ready_flag : mailbox->right_ready_flag;
  volatile int *next_remote_ready =
    is_forward ? mailbox->right_ready_flag : mailbox->left_ready_flag;

  int *current_row = dp_row_even;
  int *previous_row = dp_row_odd;

  int max_score = INT32_MIN;
  int max_idx   = 0;

  for (int col = 0; col <= REF_CORE; col++) {
    previous_row[col] = -ref_offset - col;
  }

  boundary_scores[0] = previous_row[REF_CORE];

  query = is_forward ? (query + start) : (query + end - 1);

  for (int row = 0; row < query_span; row++) {
    uint8_t query_char;

    if (is_first) {
      query_char = *query;
      query += direction;
      current_row[0] = -ref_offset - row - 1;
    } else {
      const mailbox_t incoming = receive_mailbox(previous_mailbox, previous_remote_ready);
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
      wait_for_ready(next_ready);
      store_row_fields(&next_remote_mailbox->data, current_row[REF_CORE], query_char);
      next_remote_mailbox->full = 1;
    }

    // alternate the two dp row buffers across iterations.
    int *tmp = current_row;
    current_row = previous_row;
    previous_row = tmp;
  }

  if (is_last) {
    bsg_fence();
    wait_for_ready(next_ready);
    next_remote_mailbox->full = 1;
    receive_mailbox(next_mailbox, next_remote_ready);

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

    wait_for_ready(next_ready);
    store_result_fields(&next_remote_mailbox->data, max_score, max_idx);
    next_remote_mailbox->full = 1;
    const mailbox_t incoming_max = receive_mailbox(next_mailbox, next_remote_ready);

    if (incoming_max.max > max_score) {
      max_score = incoming_max.max;
      max_idx = incoming_max.idx;
    }

    if (direction == -1) {
      split_points[0] = max_idx;
    }
  }

  mailbox_t propagated_result;
  if (is_last) {
    propagated_result.max = max_score;
    propagated_result.idx = max_idx;
  } else {
    propagated_result = receive_mailbox(next_mailbox, next_remote_ready);
  }

  if (!is_first) {
    wait_for_ready(previous_ready);
    store_result_fields(&previous_remote_mailbox->data, propagated_result.max, propagated_result.idx);
    previous_remote_mailbox->full = 1;
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

    {
      const int direction = (CORE_ID & 1) ? -1 : 1;
      const int split_idx = parallel_fill(
        &qry[seq_id * SEQ_LEN],
        query_low,
        query_high,
        direction,
        0,
        1
      );

      if (direction == -1) {
        query_low = split_idx;
      } else {
        query_high = split_idx;
      }
    }

    local_fill(&qry[seq_id * SEQ_LEN], query_low, query_high, 0, REF_CORE);

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
