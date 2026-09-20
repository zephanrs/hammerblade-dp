#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include <cstdint>

// Single-tile GEMM: tile (0,0) computes C = A * B and every other tile in the
// group drops straight through to the exit barrier.
//
// The loop order is i-k-j (a rank-1 update), not the textbook i-j-k:
//
//   - B is walked with stride 1, so one victim-cache line feeds 16 consecutive
//     j instead of being evicted after a single word. The i-j-k order strides B
//     by MAT_N and touches a different line on every iteration.
//   - The accumulator row lives in the 4 KB scratchpad, so the K accumulations
//     into a given C element never leave the tile. Only one streaming pass of
//     stores reaches DRAM.
//   - The N accumulator chains are mutually independent, so the unrolled body
//     keeps several multiply-accumulates and several non-blocking loads in
//     flight at once rather than serialising on one accumulator.
//
// A is read once per (i,k) and broadcast across the inner loop, so it needs no
// staging of its own.

// The scratchpad is 4 KB (1024 words) and also carries the stack, so the
// resident C row has to stay well inside it.
static_assert(MAT_N <= 512, "C row must fit in the 4 KB scratchpad");

elem_t c_row[MAT_N];

extern "C" int kernel(elem_t* A, elem_t* B, elem_t* C, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  if ((__bsg_x == 0) && (__bsg_y == 0)) {
    const elem_t* a_row = A;
    elem_t* c_out = C;

    for (int i = 0; i < MAT_M; i++) {
      const elem_t* b_row = B;

      // k == 0 is peeled so the first pass initialises the row. Zeroing it in a
      // separate loop would cost an extra MAT_N stores per output row.
      {
        const elem_t a = a_row[0];

        bsg_unroll(8)
        for (int j = 0; j < MAT_N; j++) {
          c_row[j] = a * b_row[j];
        }

        b_row += MAT_N;
      }

      for (int k = 1; k < MAT_K; k++) {
        const elem_t a = a_row[k];

        bsg_unroll(8)
        for (int j = 0; j < MAT_N; j++) {
          c_row[j] += a * b_row[j];
        }

        b_row += MAT_N;
      }

      // the only DRAM traffic for C: one streaming pass per output row.
      bsg_unroll(8)
      for (int j = 0; j < MAT_N; j++) {
        c_out[j] = c_row[j];
      }

      a_row += MAT_K;
      c_out += MAT_N;
    }
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
