#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include "unroll.hpp"
#include <cstdint>

// Data-parallel GEMM across the whole tile group. Every tile works; there is no
// inter-tile communication at all, just the entry and exit barriers.
//
// Tile (x,y) owns the C block of rows [y*MB, (y+1)*MB) and columns
// [x*NB, (x+1)*NB), accumulates it in scratchpad across the whole of K, and
// loads the A and B sub-blocks it needs straight from DRAM.
//
// The 2-D split is what makes all 128 tiles usable. A 1-D column split would
// need N/BLK_N >= 128 panels to keep everyone busy (N >= 1024 at BLK_N=8);
// this saturates the pod from M=64, N=128.
//
// Known inefficiency, left in deliberately: each A sub-block is loaded by all
// bsg_tiles_X tiles in its row, each B sub-block by all bsg_tiles_Y tiles in
// its column, so DRAM load traffic is bsg_tiles_X*M*K + bsg_tiles_Y*K*N --
// 393216 words at 128^3 on a full pod against an ideal 32768, 12x redundant
// (8.3x if the unavoidable M*N of C stores is counted in). Removing it is
// what the systolic variant does, and measuring it here is what makes that
// gain legible rather than asserted.

#define MB (MAT_M / bsg_tiles_Y)
#define NB (MAT_N / bsg_tiles_X)

static_assert(MAT_M % bsg_tiles_Y == 0, "tile group Y must divide MAT_M");
static_assert(MAT_N % bsg_tiles_X == 0, "tile group X must divide MAT_N");
static_assert(MAT_K % BLK_K == 0, "BLK_K must divide MAT_K");

// The scratchpad is 4 KB (1024 words) and also carries the stack.
static_assert((MB * NB) + (MB * BLK_K) + (BLK_K * NB) <= 768,
              "C block + A sub-block + B sub-block must fit in the 4 KB scratchpad");

elem_t c_blk[MB * NB];
elem_t abuf[MB * BLK_K];
elem_t bbuf[BLK_K * NB];

extern "C" int kernel(elem_t* A, elem_t* B, elem_t* C, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int row0 = __bsg_y * MB;
  const int col0 = __bsg_x * NB;

  // Zeroed once rather than peeling the first k step out of a chunked loop:
  // the peel would save MB*NB stores against MB*NB*K multiply-accumulates,
  // under 1% at K=128, and would duplicate the whole staging block.
  for (int t = 0; t < (MB * NB); t++) {
    c_blk[t] = (elem_t)0;
  }

  for (int kb = 0; kb < MAT_K; kb += BLK_K) {

    // stage this tile's A sub-block: MB rows x BLK_K columns, each row a
    // contiguous run. unrolled_load issues the whole run before consuming any
    // of it, so the network round trips overlap.
    for (int i = 0; i < MB; i++) {
      unrolled_load<elem_t, BLK_K>(&abuf[i * BLK_K], &A[((row0 + i) * MAT_K) + kb]);
    }

    // stage this tile's B sub-block: BLK_K rows x NB columns.
    for (int kk = 0; kk < BLK_K; kk++) {
      unrolled_load<elem_t, NB>(&bbuf[kk * NB], &B[((kb + kk) * MAT_N) + col0]);
    }

    // both operands are local from here on: rank-1 update per kk.
    for (int kk = 0; kk < BLK_K; kk++) {
      const elem_t* b = &bbuf[kk * NB];

      for (int i = 0; i < MB; i++) {
        const elem_t a = abuf[(i * BLK_K) + kk];
        elem_t* c = &c_blk[i * NB];

        bsg_unroll(8)
        for (int j = 0; j < NB; j++) {
          c[j] += a * b[j];
        }
      }
    }
  }

  // the only DRAM traffic for C: one streaming pass per owned row.
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
