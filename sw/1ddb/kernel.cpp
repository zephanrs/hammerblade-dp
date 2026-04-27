#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "../../common/repeat_config.hpp"
#include "unroll.hpp"
#include <cstdint>

// Double-buffered variant of sw/1d: two mailbox slots between adjacent cores
// in the pipeline so the producer can run one query position ahead of the
// consumer without blocking on a single shared slot.
#ifndef CORES_PER_GROUP
#define CORES_PER_GROUP bsg_tiles_Y
#endif

#define NUM_TILES   (bsg_tiles_X * bsg_tiles_Y)
#define NUM_GROUPS  (NUM_TILES / CORES_PER_GROUP)
#define MY_TILE_ID  (__bsg_x * bsg_tiles_Y + __bsg_y)
#define GROUP_ID    (MY_TILE_ID / CORES_PER_GROUP)
#define CORE_ID     (MY_TILE_ID % CORES_PER_GROUP)
#define TILE_X(id)  ((id) / bsg_tiles_Y)
#define TILE_Y(id)  ((id) % bsg_tiles_Y)

#ifndef ACTIVE_COMPUTE_GROUPS
#define ACTIVE_COMPUTE_GROUPS NUM_GROUPS
#endif

#ifndef PREFETCH
#define PREFETCH 0
#endif

#define REF_CORE  (SEQ_LEN / CORES_PER_GROUP)
#define MATCH     1
#define MISMATCH -1
#define GAP       1

inline int max(int a, int b) { return (a > b) ? a : b; }
inline int max(int a, int b, int c) { return max(a, max(b, c)); }
inline int max(int a, int b, int c, int d) { return max(max(a, b), max(c, d)); }

struct mailbox_t {
  int          dp_val;
  volatile int full;
  int          max_val;
  uint8_t      qry_char;
};

// Two slots per core. Slot s is full when producer has written and consumer
// has not yet drained; ready[s] tracks the inverse so the producer knows when
// the consumer is done with a slot.
mailbox_t    mailbox[2]        = {{0, 0, 0, 0}, {0, 0, 0, 0}};
volatile int next_is_ready[2]  = {1, 1};

uint8_t refbuf[REF_CORE + 1];
int H1[REF_CORE + 1];
int H2[REF_CORE + 1];

static inline __attribute__((always_inline))
void load_ref_chunk_8(uint8_t *dst, const uint8_t *src) {
  register uint8_t r0=src[0], r1=src[1], r2=src[2], r3=src[3];
  register uint8_t r4=src[4], r5=src[5], r6=src[6], r7=src[7];
  asm volatile("" ::: "memory");
  dst[0]=r0; dst[1]=r1; dst[2]=r2; dst[3]=r3;
  dst[4]=r4; dst[5]=r5; dst[6]=r6; dst[7]=r7;
}

extern "C" int kernel(uint8_t* qry, uint8_t* ref, int* output, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int my_id   = MY_TILE_ID;
  const int next_id = my_id + 1;
  const int prev_id = my_id - 1;
  mailbox_t   *next_mailbox       = (mailbox_t *)bsg_remote_ptr(
      TILE_X(next_id), TILE_Y(next_id), &mailbox[0]);
  volatile int *prev_next_is_ready = (volatile int *)bsg_remote_ptr(
      TILE_X(prev_id), TILE_Y(prev_id), (void*)&next_is_ready[0]);

  for (int repeat = 0; repeat < kInputRepeatFactor; repeat++) {
    for (int s = GROUP_ID;
         (GROUP_ID < ACTIVE_COMPUTE_GROUPS) && (s < NUM_SEQ);
         s += ACTIVE_COMPUTE_GROUPS) {

      const int output_idx = (repeat * NUM_SEQ) + s;
      int *H_curr = H1;
      int *H_prev = H2;

      for (int k = 0; k <= REF_CORE; k++) H_prev[k] = 0;

      int maxv = 0;
      const uint8_t *ref_src = &ref[SEQ_LEN * s + CORE_ID * REF_CORE];
      int k = 0;
      for (; k + 8 <= REF_CORE; k += 8) load_ref_chunk_8(&refbuf[k+1], &ref_src[k]);
      for (; k < REF_CORE; k++)          refbuf[k+1] = ref_src[k];

#if PREFETCH
      register uint8_t next_qry asm("s4");
      if (CORE_ID == 0 && SEQ_LEN > 0) next_qry = qry[SEQ_LEN * s];
#endif

      for (int i = 0; i < SEQ_LEN; i++) {
        const int slot = i & 1;
        uint8_t qry_char;

        if (CORE_ID == 0) {
#if PREFETCH
          qry_char = next_qry;
#else
          qry_char = qry[SEQ_LEN * s + i];
#endif
          H_curr[0] = 0;
        } else {
          int rdy = bsg_lr((int*)&mailbox[slot].full);
          if (rdy == 0) bsg_lr_aq((int*)&mailbox[slot].full);
          asm volatile("" ::: "memory");

          H_curr[0]         = mailbox[slot].dp_val;
          qry_char          = mailbox[slot].qry_char;
          int left_maxv     = mailbox[slot].max_val;
          if (left_maxv > maxv) maxv = left_maxv;

          mailbox[slot].full      = 0;
          prev_next_is_ready[slot] = 1;
        }

        for (int k = 1; k <= REF_CORE; k++) {
          int match      = (qry_char == refbuf[k]) ? MATCH : MISMATCH;
          int score_diag = H_prev[k-1] + match;
          int score_up   = H_prev[k]   - GAP;
          int score_left = H_curr[k-1] - GAP;
          int val        = max(0, score_diag, score_up, score_left);
          H_curr[k]      = val;
          if (val > maxv) maxv = val;
        }

        if (CORE_ID < CORES_PER_GROUP - 1) {
          int rdy = bsg_lr((int*)&next_is_ready[slot]);
          if (rdy == 0) bsg_lr_aq((int*)&next_is_ready[slot]);
          asm volatile("" ::: "memory");

          next_is_ready[slot]         = 0;
          next_mailbox[slot].dp_val   = H_curr[REF_CORE];
          next_mailbox[slot].max_val  = maxv;
          next_mailbox[slot].qry_char = qry_char;
          asm volatile("" ::: "memory");
          next_mailbox[slot].full     = 1;
        }

        int *tmp = H_curr; H_curr = H_prev; H_prev = tmp;

#if PREFETCH
        if (CORE_ID == 0 && i < SEQ_LEN - 1)
          next_qry = qry[SEQ_LEN * s + i + 1];
#endif
      }

      if (CORE_ID == CORES_PER_GROUP - 1)
        output[output_idx] = maxv;
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
