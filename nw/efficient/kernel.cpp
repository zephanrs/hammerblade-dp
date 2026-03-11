#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "../mailbox.hpp"
#include "unroll.hpp"
#include <cstdint>

#define REF_CORE (SEQ_LEN / bsg_tiles_Y)
#define MATCH 1
#define MISMATCH -1
#define GAP 1
// ref columns owned by a core still come from REF_CORE; this only sizes the local dp/tb base case
#define LOCAL_DP_SIDE 16
#define LOCAL_DP_WORDS ((LOCAL_DP_SIDE + 1) * (LOCAL_DP_SIDE + 1))
#define LOCAL_TASK_STACK_CAPACITY (((REF_CORE + 7) / 8) + 1)

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
  uint16_t query_start;
  uint16_t query_end;
  uint16_t ref_start;
  uint16_t ref_end;
};

hb_mailbox_state_t<mailbox_t> mailboxes = {};

// Per-core working buffers.
uint8_t ref_segment[REF_CORE + 1];
int dp_row_even[LOCAL_DP_WORDS];
int dp_row_odd[LOCAL_DP_WORDS];
int fill_row_even[REF_CORE + 1];
int fill_row_odd[REF_CORE + 1];
int split_points[REF_CORE];
int boundary_scores[SEQ_LEN + 1];
local_task_t local_task_stack[LOCAL_TASK_STACK_CAPACITY];

inline void store_systolic_fields(mailbox_t *data, int dp_val, uint8_t qry) {
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
  check_and_clear_mailbox(mailbox);
  const mailbox_t incoming = mailbox->data;
  set_ready(remote_ready);
  return incoming;
}

static __attribute__((noinline)) void fill(
  uint8_t *query,
  int query_start,
  int query_end,
  int direction,
  int ref_start,
  int ref_end,
  int ref_offset,
  int *scores,
  hb_mailbox_port_t<mailbox_t> *incoming_mailbox,
  hb_mailbox_port_t<mailbox_t> *outgoing_remote_mailbox,
  volatile int *incoming_remote_ready,
  volatile int *outgoing_ready
) {
  int *current_row = fill_row_even;
  int *previous_row = fill_row_odd;
  const int query_span = query_end - query_start;
  const int ref_span = ref_end - ref_start;

  for (int col = 0; col <= ref_span; col++) {
    previous_row[col] = -ref_offset - col;
  }

  if (scores != 0) {
    scores[0] = previous_row[ref_span];
  }

  query = (direction == 1) ? (query + query_start) : (query + query_end - 1);

  for (int row = 0; row < query_span; row++) {
    uint8_t query_char;

    if (incoming_mailbox == 0) {
      query_char = *query;
      query += direction;
      current_row[0] = -ref_offset - row - 1;
    } else {
      const mailbox_t incoming = receive_mailbox(incoming_mailbox, incoming_remote_ready);
      current_row[0] = incoming.dp_val;
      query_char = incoming.qry;
    }

    uint8_t *ref_ptr = &ref_segment[(direction == 1) ? (ref_start + 1) : ref_end];
    for (int col = 1; col <= ref_span; col++) {
      const int match = (query_char == *ref_ptr) ? MATCH : MISMATCH;
      ref_ptr += direction;

      const int score_diag = previous_row[col - 1] + match;
      const int score_up = previous_row[col] - GAP;
      const int score_left = current_row[col - 1] - GAP;
      current_row[col] = max(score_diag, score_up, score_left);
    }

    if (scores != 0) {
      scores[row + 1] = current_row[ref_span];
    } else {
      wait_for_ready(outgoing_ready);
      store_systolic_fields(&outgoing_remote_mailbox->data, current_row[ref_span], query_char);
      outgoing_remote_mailbox->full = 1;
    }

    int *tmp = current_row;
    current_row = previous_row;
    previous_row = tmp;
  }
}

static __attribute__((noinline)) void local_traceback(
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
      int dir = 0;
      int best = diag;
      if (up > best) {
        best = up;
        dir = 1;
      }
      if (left > best) {
        best = left;
        dir = 2;
      }

      dp[query_row * stride + ref_col] = best;
      tb[query_row * stride + ref_col] = dir;
    }
  }

  int query_row = query_span;
  int ref_col = ref_span;
  while (query_row > 0 || ref_col > 0) {
    const int dir = tb[query_row * stride + ref_col];
    if (dir == 0) {
      query_row--;
      ref_col--;
    } else if (dir == 1) {
      query_row--;
    } else {
      ref_col--;
    }

    if (ref_col < ref_span) {
      split_points[ref_start + ref_col] = query_start + query_row;
    }
  }
}

static __attribute__((noinline)) int best_split_index(
  int *forward_scores,
  volatile int *backward_scores,
  int query_span,
  int start,
  int direction,
  int *best_score
) {
  const bool is_forward = (direction == 1);
  int best_query = start;
  int max_score = INT32_MIN;
  for (int row = 0; row <= query_span; row++) {
    const int candidate_query = is_forward ? (start + row) : (start - row);
    const int score = forward_scores[row] + backward_scores[query_span - row];
    if ((score > max_score) || ((score == max_score) && (candidate_query < best_query))) {
      max_score = score;
      best_query = candidate_query;
    }
  }
  if (best_score != 0) {
    *best_score = max_score;
  }
  return best_query;
}

void local_fill(
  uint8_t *query,
  int query_start,
  int query_end,
  int ref_start,
  int ref_end
) {
  int stack_size = 0;
  const local_task_t root_task = {
    static_cast<uint16_t>(query_start),
    static_cast<uint16_t>(query_end),
    static_cast<uint16_t>(ref_start),
    static_cast<uint16_t>(ref_end),
  };
  local_task_stack[stack_size++] = root_task;

  while (stack_size > 0) {
    const local_task_t task = local_task_stack[--stack_size];
    const int task_query_span = task.query_end - task.query_start;
    const int task_ref_span = task.ref_end - task.ref_start;

    if (task_ref_span <= 0) {
      continue;
    }

    if ((task_query_span + 1) * (task_ref_span + 1) <= LOCAL_DP_WORDS) {
      local_traceback(query, task.query_start, task.query_end, task.ref_start, task.ref_end);
      continue;
    }

    const int ref_mid = task.ref_start + (task_ref_span >> 1);
    int *forward_scores = dp_row_even;
    int *backward_scores = boundary_scores;
    fill(
      query,
      task.query_start,
      task.query_end,
      -1,
      ref_mid,
      task.ref_end,
      0,
      backward_scores,
      0,
      0,
      0,
      0
    );
    fill(
      query,
      task.query_start,
      task.query_end,
      1,
      task.ref_start,
      ref_mid,
      0,
      forward_scores,
      0,
      0,
      0,
      0
    );
    const int best_query = best_split_index(
      forward_scores,
      backward_scores,
      task_query_span,
      task.query_start,
      1,
      0
    );
    split_points[ref_mid] = best_query;
    const local_task_t right_task = {
      static_cast<uint16_t>(best_query),
      task.query_end,
      static_cast<uint16_t>(ref_mid),
      task.ref_end,
    };
    local_task_stack[stack_size++] = right_task;
    const local_task_t left_task = {
      task.query_start,
      static_cast<uint16_t>(best_query),
      task.ref_start,
      static_cast<uint16_t>(ref_mid),
    };
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
  const int ref_offset = active_core_id * REF_CORE;

  volatile int *neighbor_boundary_scores =
    (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y + direction, (void *)&boundary_scores[0]);
  hb_mailbox_port_t<mailbox_t> *incoming_mailbox;
  hb_mailbox_port_t<mailbox_t> *outgoing_mailbox;
  hb_mailbox_port_t<mailbox_t> *incoming_remote_mailbox;
  hb_mailbox_port_t<mailbox_t> *outgoing_remote_mailbox;
  volatile int *incoming_ready;
  volatile int *outgoing_ready;
  volatile int *incoming_remote_ready;
  volatile int *outgoing_remote_ready;
  if (direction == 1) {
    incoming_mailbox = &mailbox->left;
    outgoing_mailbox = &mailbox->right;
    incoming_remote_mailbox = mailbox->left_mailbox;
    outgoing_remote_mailbox = mailbox->right_mailbox;
    incoming_ready = &mailbox->left_ready;
    outgoing_ready = &mailbox->right_ready;
    incoming_remote_ready = mailbox->left_ready_flag;
    outgoing_remote_ready = mailbox->right_ready_flag;
  } else {
    incoming_mailbox = &mailbox->right;
    outgoing_mailbox = &mailbox->left;
    incoming_remote_mailbox = mailbox->right_mailbox;
    outgoing_remote_mailbox = mailbox->left_mailbox;
    incoming_ready = &mailbox->right_ready;
    outgoing_ready = &mailbox->left_ready;
    incoming_remote_ready = mailbox->right_ready_flag;
    outgoing_remote_ready = mailbox->left_ready_flag;
  }

  fill(
    query,
    start,
    end,
    direction,
    0,
    REF_CORE,
    ref_offset,
    is_last ? boundary_scores : 0,
    is_first ? 0 : incoming_mailbox,
    is_last ? 0 : outgoing_remote_mailbox,
    is_first ? 0 : incoming_remote_ready,
    is_last ? 0 : outgoing_ready
  );

  mailbox_t propagated_result = {};
  if (is_last) {
    bsg_fence();
    wait_for_ready(outgoing_ready);
    outgoing_remote_mailbox->full = 1;
    receive_mailbox(outgoing_mailbox, outgoing_remote_ready);

    propagated_result.idx = best_split_index(
      boundary_scores,
      neighbor_boundary_scores,
      end - start,
      (direction == 1) ? start : end,
      direction,
      &propagated_result.max
    );
    wait_for_ready(outgoing_ready);
    store_result_fields(
      &outgoing_remote_mailbox->data,
      propagated_result.max,
      propagated_result.idx
    );
    outgoing_remote_mailbox->full = 1;
    const mailbox_t incoming_max = receive_mailbox(outgoing_mailbox, outgoing_remote_ready);
    if (incoming_max.max > propagated_result.max) {
      propagated_result.max = incoming_max.max;
      propagated_result.idx = incoming_max.idx;
    }

    if (direction == -1) {
      split_points[0] = propagated_result.idx;
    }
  } else {
    propagated_result = receive_mailbox(outgoing_mailbox, outgoing_remote_ready);
  }

  if (!is_first) {
    wait_for_ready(incoming_ready);
    store_result_fields(&incoming_remote_mailbox->data, propagated_result.max, propagated_result.idx);
    incoming_remote_mailbox->full = 1;
  }

  return propagated_result.idx;
}

// Kernel main;
extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int* path, int pod_id)
{
  const int group_id = __bsg_x;
  const int core_id = __bsg_y;
  bsg_barrier_tile_group_init();
  init_mailboxes(&mailboxes);
  reset_mailboxes(&mailboxes);
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  // Each group processes a set of sequences
  for (int seq_id = group_id; seq_id < NUM_SEQ; seq_id += bsg_tiles_X) {
    unrolled_load<uint8_t, REF_CORE>(
      &ref_segment[1],
      &ref[SEQ_LEN * seq_id + (core_id * REF_CORE)]
    );

    for (int col = 0; col < REF_CORE; col++) {
      split_points[col] = -1;
    }

    int query_low = 0;
    int query_high = SEQ_LEN;

    for (int active_cores = 4; active_cores > 1; active_cores >>= 1) {
      int split_idx;
      int direction = 1;
      int active_core_id = core_id & (active_cores - 1);

      if (active_cores & core_id) {
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

      if (core_id == 0 && active_cores == 4) {
        output[seq_id] = peek_right(&mailboxes).max;
      }

      if (active_cores & core_id) {
        query_low = split_idx;
      } else { 
        query_high = split_idx;
      }
    }

    {
      const int direction = (core_id & 1) ? -1 : 1;
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
      path[(seq_id * SEQ_LEN) + (core_id * REF_CORE) + col] = split_points[col];
    }
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
