#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include "unroll.hpp"
#include <cstdint>

// Single-tile GEMM with the B panel staged in scratchpad.
//
// mm/single had the right loop order for DRAM *traffic* but optimised the wrong
// resource. Profiling it at 8x8x8 showed tile (0,0) spending ~61% of its cycles
// in stall_depend_dram_load: 576 remote loads, nearly all of them vcache hits,
// each costing a network round trip. B was being re-fetched M*K*N times over
// the network even though the whole matrix is 256 bytes.
//
// Here B is copied into the 4 KB scratchpad once per column panel and the inner
// loop reads it locally (2-cycle) instead. Remote loads per panel drop from
// M*K*N to K*BLK_N, and the only remote traffic left in the compute loop is one
// A scalar per (i,k), reused BLK_N times across the row.
//
// Counting remote loads at 16x16x16 with BLK_N=16: 256 (B panel) + 256 (A)
// = 512, against 4352 for mm/single.
//
// The panel width trades scratchpad for A traffic: A is re-read once per panel,
// so N/BLK_N passes. Widen BLK_N until the panel no longer fits.

#define PANELS (MAT_N / BLK_N)

// BLK_N must divide MAT_N so every inner loop keeps a compile-time trip count.
static_assert(MAT_N % BLK_N == 0, "BLK_N must divide MAT_N");

// The scratchpad is 4 KB (1024 words) and also carries the stack. The B panel
// plus the accumulator row have to leave room for it.
static_assert((MAT_K * BLK_N) + BLK_N <= 768, "B panel + C row must fit in the 4 KB scratchpad");

elem_t bbuf[MAT_K * BLK_N];
elem_t c_row[BLK_N];

extern "C" int kernel(elem_t* A, elem_t* B, elem_t* C, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  if ((__bsg_x == 0) && (__bsg_y == 0)) {

    for (int panel = 0; panel < PANELS; panel++) {
      const int jb = panel * BLK_N;

      // stage this panel of B once; every compute-loop read of it is local from
      // here on. unrolled_load issues the whole row of loads before consuming
      // any of them, so the network round trips overlap.
      for (int k = 0; k < MAT_K; k++) {
        unrolled_load<elem_t, BLK_N>(&bbuf[k * BLK_N], &B[(k * MAT_N) + jb]);
      }

      const elem_t* a_row = A;
      elem_t* c_out = &C[jb];

      for (int i = 0; i < MAT_M; i++) {
        // k == 0 is peeled so the first pass initialises the row rather than a
        // separate zeroing loop costing an extra BLK_N stores per output row.
        {
          const elem_t a = a_row[0];
          const elem_t* b = bbuf;

          bsg_unroll(8)
          for (int j = 0; j < BLK_N; j++) {
            c_row[j] = a * b[j];
          }
        }

        for (int k = 1; k < MAT_K; k++) {
          const elem_t a = a_row[k];
          const elem_t* b = &bbuf[k * BLK_N];

          bsg_unroll(8)
          for (int j = 0; j < BLK_N; j++) {
            c_row[j] += a * b[j];
          }
        }

        // the only DRAM traffic for C: one streaming pass per output row.
        bsg_unroll(8)
        for (int j = 0; j < BLK_N; j++) {
          c_out[j] = c_row[j];
        }

        a_row += MAT_K;
        c_out += MAT_N;
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
