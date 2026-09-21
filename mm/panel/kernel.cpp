#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include "unroll.hpp"
#include <cstdint>

// Fixed-size output blocks, iterated -- the structure from Lin Cheng's thesis
// (Cornell, 2022) section 2.4.2, with the 4x4 register tile of 2.4.3 inside.
//
// mm/regblock tied the per-tile C block to the tile group: MB = M/TGY. That
// works while the block is small, but at 256x256 on a 16x8 pod it becomes
// 32x16 = 512 words, which leaves room for only BLK_K=4 -- the worst chunk
// setting the sweep found. The block grew with the problem, so the staging
// buffers got squeezed exactly when the problem got big enough to matter.
//
// Here the block is a compile-time BLK x BLK and each tile *loops* over as
// many blocks as it needs:
//
//   for rr = tile_y; rr < M/BLK; rr += TGY
//     for rc = tile_x; rc < N/BLK; rc += TGX
//
// so the working set is three BLK x BLK buffers -- 768 words at BLK=16, the
// same 3 KB of 4 KB the thesis uses -- no matter how large the matrices are.
// Chunk depth stays at its best setting at every problem size.

#define NBLK_M (MAT_M / BLK)
#define NBLK_N (MAT_N / BLK)

static_assert(MAT_M % BLK == 0, "BLK must divide MAT_M");
static_assert(MAT_N % BLK == 0, "BLK must divide MAT_N");
static_assert(MAT_K % BLK == 0, "BLK must divide MAT_K");
static_assert(BLK % 4 == 0, "BLK must be a multiple of the 4x4 register tile");

// Three BLK x BLK buffers. At BLK=16 that is 768 words of the 1024-word
// scratchpad, leaving 1 KB for globals and the stack.
static_assert(3 * BLK * BLK <= 768, "A, B and C blocks must fit in the 4 KB scratchpad");

elem_t a_blk[BLK * BLK];
elem_t b_blk[BLK * BLK];
elem_t c_blk[BLK * BLK];

extern "C" int kernel(elem_t* A, elem_t* B, elem_t* C, int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  // Each tile strides through the output blocks by the tile group dimensions,
  // so any matrix size spreads over the pod without changing the block size.
  for (int rr = __bsg_y; rr < NBLK_M; rr += bsg_tiles_Y) {
    for (int rc = __bsg_x; rc < NBLK_N; rc += bsg_tiles_X) {

      const int row0 = rr * BLK;
      const int col0 = rc * BLK;

      bsg_unroll(8)
      for (int t = 0; t < (BLK * BLK); t++) {
        c_blk[t] = (elem_t)0;
      }

      for (int kb = 0; kb < MAT_K; kb += BLK) {

        for (int i = 0; i < BLK; i++) {
          unrolled_load<elem_t, BLK>(&a_blk[i * BLK], &A[((row0 + i) * MAT_K) + kb]);
        }
        for (int kk = 0; kk < BLK; kk++) {
          unrolled_load<elem_t, BLK>(&b_blk[kk * BLK], &B[((kb + kk) * MAT_N) + col0]);
        }

        for (int ii = 0; ii < BLK; ii += 4) {
          for (int jj = 0; jj < BLK; jj += 4) {
            elem_t* cb = &c_blk[(ii * BLK) + jj];

            elem_t c00 = cb[0],         c01 = cb[1],         c02 = cb[2],         c03 = cb[3];
            elem_t c10 = cb[BLK + 0],   c11 = cb[BLK + 1],   c12 = cb[BLK + 2],   c13 = cb[BLK + 3];
            elem_t c20 = cb[2*BLK + 0], c21 = cb[2*BLK + 1], c22 = cb[2*BLK + 2], c23 = cb[2*BLK + 3];
            elem_t c30 = cb[3*BLK + 0], c31 = cb[3*BLK + 1], c32 = cb[3*BLK + 2], c33 = cb[3*BLK + 3];

            for (int kk = 0; kk < BLK; kk++) {
              const elem_t* ap = &a_blk[(ii * BLK) + kk];
              const elem_t a0 = ap[0];
              const elem_t a1 = ap[BLK];
              const elem_t a2 = ap[2 * BLK];
              const elem_t a3 = ap[3 * BLK];

              const elem_t* bp = &b_blk[(kk * BLK) + jj];
              const elem_t b0 = bp[0], b1 = bp[1], b2 = bp[2], b3 = bp[3];

              c00 = elem_mac(a0, b0, c00);  c01 = elem_mac(a0, b1, c01);
              c02 = elem_mac(a0, b2, c02);  c03 = elem_mac(a0, b3, c03);
              c10 = elem_mac(a1, b0, c10);  c11 = elem_mac(a1, b1, c11);
              c12 = elem_mac(a1, b2, c12);  c13 = elem_mac(a1, b3, c13);
              c20 = elem_mac(a2, b0, c20);  c21 = elem_mac(a2, b1, c21);
              c22 = elem_mac(a2, b2, c22);  c23 = elem_mac(a2, b3, c23);
              c30 = elem_mac(a3, b0, c30);  c31 = elem_mac(a3, b1, c31);
              c32 = elem_mac(a3, b2, c32);  c33 = elem_mac(a3, b3, c33);
            }

            cb[0]         = c00;  cb[1]         = c01;  cb[2]         = c02;  cb[3]         = c03;
            cb[BLK + 0]   = c10;  cb[BLK + 1]   = c11;  cb[BLK + 2]   = c12;  cb[BLK + 3]   = c13;
            cb[2*BLK + 0] = c20;  cb[2*BLK + 1] = c21;  cb[2*BLK + 2] = c22;  cb[2*BLK + 3] = c23;
            cb[3*BLK + 0] = c30;  cb[3*BLK + 1] = c31;  cb[3*BLK + 2] = c32;  cb[3*BLK + 3] = c33;
          }
        }
      }

      for (int i = 0; i < BLK; i++) {
        unrolled_load<elem_t, BLK>(&C[((row0 + i) * MAT_N) + col0], &c_blk[i * BLK]);
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
