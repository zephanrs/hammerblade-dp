#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "../../common/repeat_config.hpp"
#include "unroll.hpp"
#include <cstdint>

// Options for parallelization
#ifndef PARALLELIZE_ROWS
#ifndef PARALLELIZE_COLS
#define PARALLELIZE_COLS
#endif
#endif

#ifdef PARALLELIZE_COLS
#define GROUP_ID __bsg_x
#define NUM_GROUPS bsg_tiles_X
#define CORE_ID __bsg_y
#define CORES_PER_GROUP bsg_tiles_Y
#else
#define GROUP_ID __bsg_y
#define NUM_GROUPS bsg_tiles_Y
#define CORE_ID __bsg_x
#define CORES_PER_GROUP bsg_tiles_X
#endif

#ifndef PREFETCH
#define PREFETCH 0
#endif

// parameters
#define REF_CORE (SEQ_LEN / CORES_PER_GROUP)
#define NUM_TILES (bsg_tiles_X*bsg_tiles_Y)
#define MATCH     1
#define MISMATCH -1
#define GAP       1

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int max(int a, int b, int c) {
  return max(a, max(b,c));
}

inline int max(int a, int b, int c, int d) {
  return max(max(a,b), max(c,d));
}

struct mailbox_t {
  int      dp_val;
  volatile int full;
  int      max_val;
  uint8_t  qry_char;
};

// local mailbox to receive data from left core
mailbox_t mailbox = {0, 0, 0, 0};
volatile int next_is_ready = 1;

// global buffers
uint8_t refbuf[REF_CORE + 1];
int H1[REF_CORE + 1];
int H2[REF_CORE + 1];

// Kernel main;
extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

#ifdef PARALLELIZE_COLS
  mailbox_t *next_mailbox = (mailbox_t *)bsg_remote_ptr(__bsg_x, __bsg_y + 1, &mailbox);
  volatile int *prev_next_is_ready = (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y - 1, (void*)&next_is_ready);
#else
  mailbox_t *next_mailbox = (mailbox_t *)bsg_remote_ptr(__bsg_x + 1, __bsg_y, &mailbox);
  volatile int *prev_next_is_ready = (volatile int *)bsg_remote_ptr(__bsg_x - 1, __bsg_y, (void*)&next_is_ready);
#endif

  for (int repeat = 0; repeat < kInputRepeatFactor; repeat++) {
    for (int s = GROUP_ID; s < NUM_SEQ; s += NUM_GROUPS) {
      const int output_idx = (repeat * NUM_SEQ) + s;
      int *H_curr = H1;
      int *H_prev = H2;

      for (int k = 0; k <= REF_CORE; k++) {
        H_prev[k] = 0;
      }

      int maxv = 0;
      unrolled_load<uint8_t, REF_CORE>(
        &refbuf[1],
        &ref[SEQ_LEN * s + (CORE_ID * REF_CORE)]
      );

#if PREFETCH
      register uint8_t next_qry asm("s4");
      if (CORE_ID == 0 && SEQ_LEN > 0) {
        next_qry = qry[SEQ_LEN * s];
      }
#endif

      for (int i = 0; i < SEQ_LEN; i++) {
        uint8_t qry_char;

        if (CORE_ID == 0) {
#if PREFETCH
          qry_char = next_qry;
#else
          qry_char = qry[SEQ_LEN * s + i];
#endif
          H_curr[0] = 0;
        } else {
          int rdy = bsg_lr((int*)&mailbox.full);
          if (rdy == 0) {
            bsg_lr_aq((int*)&mailbox.full);
          }
          asm volatile("" ::: "memory");

          H_curr[0] = mailbox.dp_val;
          qry_char = mailbox.qry_char;
          int left_maxv = mailbox.max_val;
          if (left_maxv > maxv) {
            maxv = left_maxv;
          }

          mailbox.full = 0;
          *prev_next_is_ready = 1;
        }

        for (int k = 1; k <= REF_CORE; k++) {
          int match = (qry_char == refbuf[k]) ? MATCH : MISMATCH;
          int score_diag = H_prev[k-1] + match;
          int score_up = H_prev[k] - GAP;
          int score_left = H_curr[k-1] - GAP;
          int val = max(0, score_diag, score_up, score_left);
          H_curr[k] = val;
          if (val > maxv) {
            maxv = val;
          }
        }

        if (CORE_ID < CORES_PER_GROUP - 1) {
          int rdy = bsg_lr((int*)&next_is_ready);
          if (rdy == 0) {
            bsg_lr_aq((int*)&next_is_ready);
          }
          asm volatile("" ::: "memory");

          next_is_ready = 0;
          next_mailbox->dp_val = H_curr[REF_CORE];
          next_mailbox->max_val = maxv;
          next_mailbox->qry_char = qry_char;
          next_mailbox->full = 1;
        }

        int *tmp = H_curr;
        H_curr = H_prev;
        H_prev = tmp;

#if PREFETCH
        if (CORE_ID == 0 && i < SEQ_LEN - 1) {
          next_qry = qry[SEQ_LEN * s + i + 1];
        }
#endif
      }

      if (CORE_ID == CORES_PER_GROUP - 1) {
        output[output_idx] = maxv;
      }
    }
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
