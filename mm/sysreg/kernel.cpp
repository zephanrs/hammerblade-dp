#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "elem.hpp"
#include "unroll.hpp"
#include <cstdint>

// Output-stationary systolic array with a batched handshake and a register
// tiled inner loop -- mm/systolic and mm/regblock merged.
//
// These two had to merge as one change, not two. Register tiling needs the C
// accumulators to stay in registers *across* k, but mm/systolic's mailbox
// hands over one k step at a time, so a 4x4 tile per step would cost 16 loads
// + 16 stores per 16 FMAs: exactly the 2 scratchpad-ops-per-FMA it already
// had. The k-window has to exist first.
//
// So messages now carry KC k-steps at once. That does both jobs:
//   - the handshake amortises KC-fold. mm/systolic at 16^3/4x2 spent 1024
//     payload stores + 320 flag stores + ~450 lr/lr_aq to save 1024 remote
//     loads, a bad trade; only the flags and waits shrink here, but those are
//     the part that never amortised.
//   - the window gives C somewhere to live, so the 4x4 register tile applies
//     and scratchpad traffic drops from ~2 ops/FMA to ~0.5.
//
// Forwarding still happens as soon as a window is in hand, before this tile
// looks at the other flow, so a DRAM bubble at one feeder does not propagate
// into the other axis.

#define TGX bsg_tiles_X
#define TGY bsg_tiles_Y
#define MB (MAT_M / TGY)
#define NB (MAT_N / TGX)

static_assert(MAT_M % TGY == 0, "tile group Y must divide MAT_M");
static_assert(MAT_N % TGX == 0, "tile group X must divide MAT_N");
static_assert(MAT_K % BLK_K == 0, "BLK_K must divide MAT_K");
static_assert(BLK_K % BLK_C == 0, "BLK_C must divide BLK_K");
static_assert(MB % 4 == 0, "MB must be a multiple of the 4x4 register tile");
static_assert(NB % 4 == 0, "NB must be a multiple of the 4x4 register tile");

// Every tile compiles the same binary, so all tiles pay the edge staging
// buffers. Budget against tile (0,0), which feeds both axes.
static_assert((MB * NB) + (2 * BLK_C * MB) + (2 * BLK_C * NB) + (BLK_C * MB)
              + (MB * BLK_K) + (BLK_K * NB) <= 768,
              "sysreg working set must fit in the 4 KB scratchpad");

struct mailbox_t {
  elem_t a[2][BLK_C * MB];
  elem_t b[2][BLK_C * NB];
  volatile int a_full[2];
  volatile int b_full[2];
  volatile int a_credit[2];
  volatile int b_credit[2];
};

mailbox_t mb;

elem_t c_blk[MB * NB];
elem_t a_chunk[MB * BLK_K];
elem_t b_chunk[BLK_K * NB];
elem_t a_win[BLK_C * MB];

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
  const bool feeds_a = (x == 0);
  const bool feeds_b = (y == 0);
  const bool fwd_a = (x < (TGX - 1));
  const bool fwd_b = (y < (TGY - 1));

  mailbox_t* east  = fwd_a    ? (mailbox_t*)bsg_remote_ptr(x + 1, y, &mb) : 0;
  mailbox_t* south = fwd_b    ? (mailbox_t*)bsg_remote_ptr(x, y + 1, &mb) : 0;
  mailbox_t* west  = (x != 0) ? (mailbox_t*)bsg_remote_ptr(x - 1, y, &mb) : 0;
  mailbox_t* north = (y != 0) ? (mailbox_t*)bsg_remote_ptr(x, y - 1, &mb) : 0;

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

  bsg_unroll(8)
  for (int t = 0; t < (MB * NB); t++) {
    c_blk[t] = (elem_t)0;
  }

  int w = 0;
  for (int kb = 0; kb < MAT_K; kb += BLK_K) {

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

    for (int wb = 0; wb < BLK_K; wb += BLK_C, w++) {
      const int s = w & 1;
      const elem_t* aw;
      const elem_t* bw;

      // A window. Laid out [kk][i] so the register tile reads 4 contiguous
      // values; a_chunk is [i][kk], hence the gather on the feeding edge.
      if (feeds_a) {
        for (int kk = 0; kk < BLK_C; kk++) {
          bsg_unroll(8)
          for (int i = 0; i < MB; i++) {
            a_win[(kk * MB) + i] = a_chunk[(i * BLK_K) + wb + kk];
          }
        }
        aw = a_win;
      } else {
        wait_flag(&mb.a_full[s]);
        aw = mb.a[s];
      }

      // Forward A the moment it is in hand, before touching B.
      if (fwd_a) {
        wait_flag(&mb.a_credit[s]);
        mb.a_credit[s] = 0;
        bsg_unroll(8)
        for (int t = 0; t < (BLK_C * MB); t++) {
          east->a[s][t] = aw[t];
        }
        asm volatile("" ::: "memory");
        east->a_full[s] = 1;
      }

      if (feeds_b) {
        bw = &b_chunk[wb * NB];
      } else {
        wait_flag(&mb.b_full[s]);
        bw = mb.b[s];
      }

      if (fwd_b) {
        wait_flag(&mb.b_credit[s]);
        mb.b_credit[s] = 0;
        bsg_unroll(8)
        for (int t = 0; t < (BLK_C * NB); t++) {
          south->b[s][t] = bw[t];
        }
        asm volatile("" ::: "memory");
        south->b_full[s] = 1;
      }

      // Register-tiled compute over the whole window: the 16 accumulators are
      // loaded once and held across all BLK_C steps.
      for (int ii = 0; ii < MB; ii += 4) {
        for (int jj = 0; jj < NB; jj += 4) {
          elem_t* cb = &c_blk[(ii * NB) + jj];

          elem_t c00 = cb[0],        c01 = cb[1],        c02 = cb[2],        c03 = cb[3];
          elem_t c10 = cb[NB + 0],   c11 = cb[NB + 1],   c12 = cb[NB + 2],   c13 = cb[NB + 3];
          elem_t c20 = cb[2*NB + 0], c21 = cb[2*NB + 1], c22 = cb[2*NB + 2], c23 = cb[2*NB + 3];
          elem_t c30 = cb[3*NB + 0], c31 = cb[3*NB + 1], c32 = cb[3*NB + 2], c33 = cb[3*NB + 3];

          for (int kk = 0; kk < BLK_C; kk++) {
            const elem_t* ap = &aw[(kk * MB) + ii];
            const elem_t a0 = ap[0], a1 = ap[1], a2 = ap[2], a3 = ap[3];

            const elem_t* bp = &bw[(kk * NB) + jj];
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

          cb[0]        = c00;  cb[1]        = c01;  cb[2]        = c02;  cb[3]        = c03;
          cb[NB + 0]   = c10;  cb[NB + 1]   = c11;  cb[NB + 2]   = c12;  cb[NB + 3]   = c13;
          cb[2*NB + 0] = c20;  cb[2*NB + 1] = c21;  cb[2*NB + 2] = c22;  cb[2*NB + 3] = c23;
          cb[3*NB + 0] = c30;  cb[3*NB + 1] = c31;  cb[3*NB + 2] = c32;  cb[3*NB + 3] = c33;
        }
      }

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
