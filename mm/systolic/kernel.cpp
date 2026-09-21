#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include "unroll.hpp"
#include <cstdint>

// Output-stationary 2-D systolic GEMM.
//
// Tile (x,y) owns the same C block as mm/parallel -- rows [y*MB, (y+1)*MB),
// columns [x*NB, (x+1)*NB) -- but A and B are no longer loaded redundantly.
// A flows west->east along tile rows, B flows north->south along tile columns,
// and each tile forwards what it receives before computing on it.
//
// Only the edge tiles touch DRAM: x==0 stages A, y==0 stages B. The other
// (TGX-1)*(TGY-1) tiles never issue a DRAM load in the compute loop at all.
// mm/parallel at 16x16x16 on 4x2 spent 26% of cycles in
// stall_depend_dram_seq_load and 9% in stall_remote_req; this removes the
// cause rather than overlapping it.
//
// C is the stationary operand because it is touched K times -- moving it would
// cost more than moving A and B combined.
//
// Not yet done: the edge tiles still stage a chunk and then feed from it
// serially, so a DRAM bubble at an edge propagates down its row or column.
// Double-buffering the chunks fixes that but does not fit the scratchpad at
// BLK_K=16 on a full pod; it needs BLK_K=8. Measure first.

#define TGX bsg_tiles_X
#define TGY bsg_tiles_Y
#define MB (MAT_M / TGY)
#define NB (MAT_N / TGX)

static_assert(MAT_M % TGY == 0, "tile group Y must divide MAT_M");
static_assert(MAT_N % TGX == 0, "tile group X must divide MAT_N");
static_assert(MAT_K % BLK_K == 0, "BLK_K must divide MAT_K");

// Every tile compiles the same binary, so all tiles pay the edge staging
// buffers in static allocation. The budget check has to assume tile (0,0),
// which is both an A feeder and a B feeder.
static_assert((MB * NB) + (2 * MB) + (2 * NB) + MB + NB
              + (MB * BLK_K) + (BLK_K * NB) <= 768,
              "systolic working set must fit in the 4 KB scratchpad");

// Inflow buffers are double-buffered on k parity so a tile can be handed step
// k+1 while it is still computing step k.
struct mailbox_t {
  elem_t a[2][MB];
  elem_t b[2][NB];
  volatile int a_full[2];     // written by the west neighbour
  volatile int b_full[2];     // written by the north neighbour
  volatile int a_credit[2];   // written by the east neighbour: slot is free
  volatile int b_credit[2];   // written by the south neighbour: slot is free
};

mailbox_t mb;

elem_t c_blk[MB * NB];
elem_t a_chunk[MB * BLK_K];
elem_t b_chunk[BLK_K * NB];
elem_t a_loc[MB];

static inline void wait_flag(volatile int* f) {
  int ready = bsg_lr((int*)f);
  if (ready == 0) {
    bsg_lr_aq((int*)f);
  }
  asm volatile("" ::: "memory");
}

extern "C" int kernel(elem_t* A, elem_t* B, elem_t* C, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();

  const int x = __bsg_x;
  const int y = __bsg_y;
  const bool feeds_a = (x == 0);          // stages A from DRAM
  const bool feeds_b = (y == 0);          // stages B from DRAM
  const bool fwd_a = (x < (TGX - 1));
  const bool fwd_b = (y < (TGY - 1));

  mailbox_t* east  = fwd_a    ? (mailbox_t*)bsg_remote_ptr(x + 1, y, &mb) : 0;
  mailbox_t* south = fwd_b    ? (mailbox_t*)bsg_remote_ptr(x, y + 1, &mb) : 0;
  mailbox_t* west  = (x != 0) ? (mailbox_t*)bsg_remote_ptr(x - 1, y, &mb) : 0;
  mailbox_t* north = (y != 0) ? (mailbox_t*)bsg_remote_ptr(x, y - 1, &mb) : 0;

  // Both slots start empty and credited. This must be visible to every
  // neighbour before any message moves, hence the barrier below.
  for (int s = 0; s < 2; s++) {
    mb.a_full[s] = 0;
    mb.b_full[s] = 0;
    mb.a_credit[s] = 1;
    mb.b_credit[s] = 1;
  }

  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int row0 = y * MB;
  const int col0 = x * NB;

  for (int t = 0; t < (MB * NB); t++) {
    c_blk[t] = (elem_t)0;
  }

  int k = 0;
  for (int kb = 0; kb < MAT_K; kb += BLK_K) {

    // Edge tiles stage a chunk so that per-k feeding is a scratchpad read.
    // Reading A's k-th column straight from DRAM would be stride-K, one
    // vcache line per element -- the mistake mm/single made.
    if (feeds_a) {
      for (int i = 0; i < MB; i++) {
        unrolled_load<elem_t, BLK_K>(&a_chunk[i * BLK_K], &A[((row0 + i) * MAT_K) + kb]);
      }
    }
    if (feeds_b) {
      for (int kk = 0; kk < BLK_K; kk++) {
        unrolled_load<elem_t, NB>(&b_chunk[kk * NB], &B[((kb + kk) * MAT_N) + col0]);
      }
    }

    for (int kk = 0; kk < BLK_K; kk++, k++) {
      const int s = k & 1;
      const elem_t* a;
      const elem_t* b;

      if (feeds_a) {
        for (int i = 0; i < MB; i++) {
          a_loc[i] = a_chunk[(i * BLK_K) + kk];
        }
        a = a_loc;
      } else {
        wait_flag(&mb.a_full[s]);
        a = mb.a[s];
      }

      // Forward A the moment it is in hand, before touching B at all. If this
      // waited for B first, a tile holding A but blocked on B would stop
      // forwarding A east -- so a DRAM bubble at a B feeder (y==0) would
      // propagate sideways into the A flow. Forwarding costs only the credit
      // wait, which is independent of B.
      if (fwd_a) {
        wait_flag(&mb.a_credit[s]);
        mb.a_credit[s] = 0;
        for (int i = 0; i < MB; i++) {
          east->a[s][i] = a[i];
        }
        asm volatile("" ::: "memory");
        east->a_full[s] = 1;
      }

      if (feeds_b) {
        b = &b_chunk[kk * NB];
      } else {
        wait_flag(&mb.b_full[s]);
        b = mb.b[s];
      }

      // Forward before computing, so the downstream tile can start its own
      // step while this one is still doing its MB*NB multiply-accumulates.
      if (fwd_b) {
        wait_flag(&mb.b_credit[s]);
        mb.b_credit[s] = 0;
        for (int j = 0; j < NB; j++) {
          south->b[s][j] = b[j];
        }
        asm volatile("" ::: "memory");
        south->b_full[s] = 1;
      }

      // rank-1 update, both operands local
      for (int i = 0; i < MB; i++) {
        const elem_t av = a[i];
        elem_t* c = &c_blk[i * NB];

        bsg_unroll(8)
        for (int j = 0; j < NB; j++) {
          c[j] += av * b[j];
        }
      }

      // Release the inflow slots only now: the data was still in use for both
      // the forward and the compute above.
      if (!feeds_a) {
        mb.a_full[s] = 0;
        asm volatile("" ::: "memory");
        west->a_credit[s] = 1;
      }
      if (!feeds_b) {
        mb.b_full[s] = 0;
        asm volatile("" ::: "memory");
        north->b_credit[s] = 1;
      }
    }
  }

  for (int i = 0; i < MB; i++) {
    unrolled_load<elem_t, NB>(&C[((row0 + i) * MAT_N) + col0], &c_blk[i * NB]);
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
