#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include "unroll.hpp"
#include <cstdint>

// mm/parallel plus tiling into registers, following section 2.4.3 of Chen's
// thesis (Cornell, 2022).
//
// mm/parallel at 16x16x16 on 4x2 issued 12032 local FP loads/stores against
// 8192 FP arithmetic ops -- more scratchpad traffic than maths -- because
// `c[j] += a * b[j]` round-trips the accumulator through the scratchpad on
// every single multiply-accumulate.
//
// Here a 4x4 sub-block of C is held in 16 registers across the *whole* k loop.
// Per k step the inner kernel loads 4 A values and 4 B values and issues 16
// FMAs, so the ratio flips from worse-than-1:1 to 2 FMAs per scratchpad load,
// and C touches the scratchpad once per sub-block per chunk rather than once
// per MAC.
//
// The loop is written out by hand rather than left to bsg_unroll for two
// reasons. First, only a full unroll lets the 16 accumulators stay in
// registers across k. Second, every load and store below is a constant offset
// from one base pointer (`ab`, `bb`, `cb`), so the compiler emits register
// offset addressing off a single base register instead of recomputing an
// address per access -- the thesis notes this specifically, and `addi` was
// 17% of our dynamic instructions.
//
// 24 FP values are live at once (16 accumulators + 4 A + 4 B) out of 32
// architectural FP registers.

#define MB (MAT_M / bsg_tiles_Y)
#define NB (MAT_N / bsg_tiles_X)

static_assert(MAT_M % bsg_tiles_Y == 0, "tile group Y must divide MAT_M");
static_assert(MAT_N % bsg_tiles_X == 0, "tile group X must divide MAT_N");
static_assert(MAT_K % BLK_K == 0, "BLK_K must divide MAT_K");
static_assert(MB % 4 == 0, "MB must be a multiple of the 4x4 register tile");
static_assert(NB % 4 == 0, "NB must be a multiple of the 4x4 register tile");

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

  bsg_unroll(8)
  for (int t = 0; t < (MB * NB); t++) {
    c_blk[t] = (elem_t)0;
  }

  for (int kb = 0; kb < MAT_K; kb += BLK_K) {

    for (int i = 0; i < MB; i++) {
      unrolled_load<elem_t, BLK_K>(&abuf[i * BLK_K], &A[((row0 + i) * MAT_K) + kb]);
    }
    for (int kk = 0; kk < BLK_K; kk++) {
      unrolled_load<elem_t, NB>(&bbuf[kk * NB], &B[((kb + kk) * MAT_N) + col0]);
    }

    for (int ii = 0; ii < MB; ii += 4) {
      for (int jj = 0; jj < NB; jj += 4) {

        elem_t* cb = &c_blk[(ii * NB) + jj];

        // load the 4x4 accumulator tile once, hold it across all of BLK_K
        elem_t c00 = cb[0],          c01 = cb[1],          c02 = cb[2],          c03 = cb[3];
        elem_t c10 = cb[NB + 0],     c11 = cb[NB + 1],     c12 = cb[NB + 2],     c13 = cb[NB + 3];
        elem_t c20 = cb[2*NB + 0],   c21 = cb[2*NB + 1],   c22 = cb[2*NB + 2],   c23 = cb[2*NB + 3];
        elem_t c30 = cb[3*NB + 0],   c31 = cb[3*NB + 1],   c32 = cb[3*NB + 2],   c33 = cb[3*NB + 3];

        for (int kk = 0; kk < BLK_K; kk++) {
          const elem_t* ab = &abuf[(ii * BLK_K) + kk];
          const elem_t a0 = ab[0];
          const elem_t a1 = ab[BLK_K];
          const elem_t a2 = ab[2 * BLK_K];
          const elem_t a3 = ab[3 * BLK_K];

          const elem_t* bb = &bbuf[(kk * NB) + jj];
          const elem_t b0 = bb[0];
          const elem_t b1 = bb[1];
          const elem_t b2 = bb[2];
          const elem_t b3 = bb[3];

          c00 = elem_mac(a0, b0, c00);  c01 = elem_mac(a0, b1, c01);
          c02 = elem_mac(a0, b2, c02);  c03 = elem_mac(a0, b3, c03);
          c10 = elem_mac(a1, b0, c10);  c11 = elem_mac(a1, b1, c11);
          c12 = elem_mac(a1, b2, c12);  c13 = elem_mac(a1, b3, c13);
          c20 = elem_mac(a2, b0, c20);  c21 = elem_mac(a2, b1, c21);
          c22 = elem_mac(a2, b2, c22);  c23 = elem_mac(a2, b3, c23);
          c30 = elem_mac(a3, b0, c30);  c31 = elem_mac(a3, b1, c31);
          c32 = elem_mac(a3, b2, c32);  c33 = elem_mac(a3, b3, c33);
        }

        cb[0]        = c00;  cb[1]        = c01;  cb[2]        = c02;  cb[3]        = c03;
        cb[NB + 0]   = c10;  cb[NB + 1]   = c11;  cb[NB + 2]   = c12;  cb[NB + 3]   = c13;
        cb[2*NB + 0] = c20;  cb[2*NB + 1] = c21;  cb[2*NB + 2] = c22;  cb[2*NB + 3] = c23;
        cb[3*NB + 0] = c30;  cb[3*NB + 1] = c31;  cb[3*NB + 2] = c32;  cb[3*NB + 3] = c33;
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
