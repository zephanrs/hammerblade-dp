#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "unroll.hpp"
#include <cstdint>

#define GROUP_ID __bsg_x
#define NUM_GROUPS bsg_tiles_X
#define CORE_ID __bsg_y
#define CORES_PER_GROUP bsg_tiles_Y
#define HALF_CORES (CORES_PER_GROUP / 2)

// parameters
#define REF_CORE (SEQ_LEN / CORES_PER_GROUP)
#define MATCH     1
#define MISMATCH -1
#define GAP       1

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int max(int a, int b, int c) {
  return max(a, max(b,c));
}

struct mailbox_t {
  int      dp_val; // or max
  int      qry;    // or idx 
  int      full;
};

// local mailbox to receive data from left core
mailbox_t mailbox = {0, 0, 0};
mailbox_t max_mailbox = {0, 0, 0};
volatile int ready = 1;

volatile int max_ready = 0;
volatile int max_data  = 0;

// global buffers
uint8_t refbuf[REF_CORE + 1];
int H1[REF_CORE + 1];
int H2[REF_CORE + 1];
int path_chunk[REF_CORE];

int B[SEQ_LEN+1];

int parallel_fill(
  uint8_t* qry,
  uint8_t* ref,
  int      start, // inclusive
  int      end, // exclusive
  int      dir, // 1 for forward, -1 for backward
  int      init,
  int      core_id,
  int      num_cores
) {

  bool is_first = !core_id;
  bool is_last  = (core_id == num_cores - 1);

  mailbox_t *next_mailbox  = (mailbox_t *)bsg_remote_ptr(__bsg_x, __bsg_y + dir, &mailbox);
  mailbox_t *next_max_mailbox = (mailbox_t *)bsg_remote_ptr(__bsg_x, __bsg_y + dir, &max_mailbox);
  mailbox_t *first_mailbox = (mailbox_t *)bsg_remote_ptr(__bsg_x, __bsg_y - (core_id * dir), &mailbox);
  mailbox_t *first_max_mailbox = (mailbox_t *)bsg_remote_ptr(__bsg_x, __bsg_y - (core_id * dir), &max_mailbox);
  volatile int *prev_ready = (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y - dir, (void*)&ready);

  // hirschberg stuff
  volatile int *next_ready = (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y + dir, (void*)&max_ready);
  volatile int *next_B     = (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y + dir, (void*)&B[0]);

  int *H_curr = H1;
  int *H_prev = H2;

  int max_score = INT32_MIN;
  int max_idx   = 0;

  for (int k = 0; k <= REF_CORE; k++) {
    H_prev[k] = init - k * GAP;
  }

  B[0] = H_prev[REF_CORE];

  qry = (dir == 1) ? qry + start : qry + end - 1;

  // do dp calculation row by row
  for (int i = 0; i < end - start; i++) {
    uint8_t qry_char;

    if (is_first) {
      qry_char = *qry;
      qry += dir;
      H_curr[0] = init - (i + 1) * GAP;
    } else {
      // wait for core to the left to write
      int rdy = bsg_lr((int*)&(mailbox.full));
      if (rdy == 0) bsg_lr_aq((int*)&(mailbox.full));
      asm volatile("" ::: "memory");

      H_curr[0] = mailbox.dp_val;
      qry_char = mailbox.qry;

      // indicate we are done with the buffer
      mailbox.full = 0;
      *prev_ready = 1;
    }

    uint8_t* refptr = &refbuf[(dir == 1) ? 1 : REF_CORE];

    for (int k = 1; k <= REF_CORE; k++) {
      int match      = (qry_char == *refptr) ? MATCH : MISMATCH;
      refptr        += dir;

      int score_diag = H_prev[k-1] + match;
      int score_up   = H_prev[k]   - GAP;
      int score_left = H_curr[k-1] - GAP;

      int val = max(score_diag, score_up, score_left);
      H_curr[k] = val;
    }

    if (is_last) {
      int idx = i + 1;
      int ridx = (end - start) - idx;
      B[idx] = H_curr[REF_CORE];

      if (idx == ridx) { // sync halves
        *next_ready = 1;
        int rdy = bsg_lr((int*)&max_ready);
        if (rdy == 0) bsg_lr_aq((int*)&max_ready);
        asm volatile("" ::: "memory");

        int score = next_B[ridx];
        if (score + H_curr[REF_CORE] > max_score) {
          max_score = score + H_curr[REF_CORE];
          max_idx   = idx;
        }
      } else if (idx > ridx) { // start computing max
        int score = next_B[ridx];
        if (score + H_curr[REF_CORE] > max_score) {
          max_score = score + H_curr[REF_CORE];
          max_idx   = idx;
        }
      }
    } else {
      // activate right core, wait for it to be empty
      int rdy = bsg_lr((int*)&ready);
      if (rdy == 0) bsg_lr_aq((int*)&ready);
      asm volatile("" ::: "memory");

      ready = 0;

      next_mailbox->dp_val = H_curr[REF_CORE];
      next_mailbox->qry = qry_char;
      next_mailbox->full = 1;
    }

    // swap DP rows
    int *tmp = H_curr;
    H_curr = H_prev;
    H_prev = tmp;
  }

  if (is_last) {
    max_idx =  ((dir == 1) ? start + max_idx : (end - max_idx));

    next_max_mailbox->dp_val = max_score;
    next_max_mailbox->qry    = max_idx;
    next_max_mailbox->full   = 1;
    *next_ready = 0;

    int rdy = bsg_lr((int*)&max_mailbox.full);
    if (rdy == 0) bsg_lr_aq((int*)&max_mailbox.full);
    asm volatile("" ::: "memory");

    if (max_mailbox.dp_val > max_score) {
      max_score = max_mailbox.dp_val;
      max_idx   = max_mailbox.qry;
    }

    max_mailbox.full = 0;

    if (dir == -1) {
      local_path[0] = max_idx;
    }

    first_max_mailbox->dp_val = max_score;
    first_max_mailbox->qry    = max_idx;
    first_max_mailbox->full   = 1;
  }

  int rdy = bsg_lr((int*)&(max_mailbox.full));
  if (rdy == 0) bsg_lr_aq((int*)&(max_mailbox.full));
  asm volatile("" ::: "memory");

  int ret = max_mailbox.dp_val;

  if (!is_last) {
    next_max_mailbox->dp_val = max_mailbox.dp_val;
    next_max_mailbox->qry    = max_mailbox.qry;
    next_max_mailbox->full   = 1;
  }

  max_mailbox.full = 0;
  
  return ret;
}

// Kernel main;
extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int* path, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  // Each group processes a set of sequences
  for (int s = GROUP_ID; s < NUM_SEQ; s += NUM_GROUPS) {
    // load reference chunk
    unrolled_load<uint8_t, REF_CORE>(
      &refbuf[1],
      &ref[SEQ_LEN * s + (CORE_ID * REF_CORE)]
    );

    for (int k = 0; k < REF_CORE; k++) {
      path_chunk[k] = -1;
    }

    int a;

    if (CORE_ID < HALF_CORES) {
      a = parallel_fill(
        &qry[s * SEQ_LEN],
        ref,
        0,
        SEQ_LEN,
        1,
        -(CORE_ID * REF_CORE * GAP),
        CORE_ID,
        4
      );
    } else {
      a = parallel_fill(
        &qry[s * SEQ_LEN],
        ref,
        0,
        SEQ_LEN,
        -1,
        -((CORES_PER_GROUP - 1 - CORE_ID) * REF_CORE * GAP),
        7 - CORE_ID,
        4
      );
    }

    for (int k = 0; k < REF_CORE; k++) {
      path[(s * SEQ_LEN) + (CORE_ID * REF_CORE) + k] = path_chunk[k];
    }

    if (CORE_ID == 0) {
      output[s] = a;
    }

    bsg_barrier_tile_group_sync();
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
