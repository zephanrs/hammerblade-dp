// sw/2d — 2D systolic Smith-Waterman, boundary-only DP storage.
//
// Each tile owns a (QRY_CORE+1) × (REF_CORE+1) DP submatrix — but only
// its rightmost column and bottommost row are needed by neighbors.
// We therefore drop the interior from DMEM and compute it on the fly
// with a 1D rolling row + a scalar diag, exposing only:
//
//   right_col [QRY_CORE+1]   ← read by (x+1, y)
//   bottom_row[REF_CORE+1]   ← read by (x, y+1)
//
// Memory cost shrinks from O(QRY × REF) to O(QRY + REF).  Pre-rewrite
// max seq_len = 192 (full submatrix double-buffered hit the 4 KB DMEM
// budget); this file pushes the cap to ~2048 with single buffer.
//
// We drop the double buffer because explicit handshake on
// right_done / bottom_done is enough to gate concurrent overwrites,
// and the freed DMEM goes to a much larger working_row + left_col.
// The pipeline depth shrinks by 1 (no overlap of iter i's neighbor
// reads with iter i+1's compute), but the original sw/2d at the same
// seq_len is still the apples-to-apples comparison and runs from this
// file too.
//
// Recurrence on the rolling row, with old_diag scalar:
//
//   working_row[k] holds dp[j-1][k] at row j entry, dp[j][k] at exit.
//   At j entry: old_diag = working_row[0] (= dp[j-1][0]).
//               working_row[0] = left_col[j] (= dp[j][0]).
//   For each k=1..REF:
//     up   = working_row[k]      // dp[j-1][k]
//     left = working_row[k-1]    // dp[j][k-1] (just written)
//     diag = old_diag            // dp[j-1][k-1]
//     v = max(0, diag+match, up-GAP, left-GAP)
//     old_diag = up              // save dp[j-1][k] for next k's diag
//     working_row[k] = v         // overwrite with dp[j][k]
//   right_col[j] = working_row[REF_CORE]  // expose this row's right edge
//
// At j=QRY exit, working_row holds dp[QRY][*]; copy to bottom_row.

#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "../../common/repeat_config.hpp"
#include "unroll.hpp"
#include <cstdint>

#define QRY_CORE (SEQ_LEN / bsg_tiles_Y)
#define REF_CORE (SEQ_LEN / bsg_tiles_X)

#define MATCH      1
#define MISMATCH  -1
#define GAP        1

#define BUFFER { .right_done = 1, .bottom_done = 1, .max_left = -1, .max_top = -1 }

inline int max(int a, int b) { return (a > b) ? a : b; }
inline int max(int a, int b, int c) { return max(a, max(b, c)); }
inline int max(int a, int b, int c, int d) { return max(max(a, b), max(c, d)); }

struct buffer_t {
  int      right_col [QRY_CORE + 1];   // exposed: our rightmost dp column
  int      bottom_row[REF_CORE + 1];   // exposed: our bottommost dp row
  uint8_t  qrybuf    [QRY_CORE];
  uint8_t  refbuf    [REF_CORE];

  int      right_done   =  1;          // set to 1 by right neighbor when consumed
  int      bottom_done  =  1;          // set to 1 by bottom neighbor when consumed
  int      max_left     = -1;          // pushed by left neighbor at end of its iter
  int      max_top      = -1;          // pushed by top neighbor at end of its iter

  // Remote pointers — set up once at kernel entry.
  int     *left_done;        // → (x-1, y).right_done
  int     *top_done;         // → (x, y-1).bottom_done
  int     *left_right_col;   // → (x-1, y).right_col[0]
  int     *top_bottom_row;   // → (x, y-1).bottom_row[0]
  int     *right_max;        // → (x+1, y).max_left
  int     *bottom_max;       // → (x, y+1).max_top
  uint8_t *next_qry;         // → (x-1, y).qrybuf[0]
  uint8_t *next_ref;         // → (x, y-1).refbuf[0]
};

static buffer_t buf = BUFFER;

// Local working state for the rolling-row inner DP.  Static to land in
// .bss (per-tile DMEM) instead of stack — keeps stack pressure down at
// large seq_len.
static int working_row[REF_CORE + 1];
static int left_col   [QRY_CORE + 1];

static void init_buffer(buffer_t *b, int x, int y) {
  b->left_done       = (int *)     bsg_remote_ptr(x - 1, y, &b->right_done);
  b->top_done        = (int *)     bsg_remote_ptr(x, y - 1, &b->bottom_done);
  b->left_right_col  = (int *)     bsg_remote_ptr(x - 1, y, &b->right_col[0]);
  b->top_bottom_row  = (int *)     bsg_remote_ptr(x, y - 1, &b->bottom_row[0]);
  b->right_max       = (int *)     bsg_remote_ptr(x + 1, y, &b->max_left);
  b->bottom_max      = (int *)     bsg_remote_ptr(x, y + 1, &b->max_top);
  b->next_qry        = (uint8_t *) bsg_remote_ptr(x - 1, y, &b->qrybuf[0]);
  b->next_ref        = (uint8_t *) bsg_remote_ptr(x, y - 1, &b->refbuf[0]);
}

extern "C" int kernel(uint8_t *qry, uint8_t *ref, int *output, int /*pod_id*/) {
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  init_buffer(&buf, __bsg_x, __bsg_y);

  for (int repeat = 0; repeat < kInputRepeatFactor; repeat++) {
    for (int i = 0; i < NUM_SEQ; i++) {
      const int output_idx = (repeat * NUM_SEQ) + i;
      buffer_t *curr = &buf;

      // Wait for last iter's right_col / bottom_row to be consumed
      // before we overwrite them.
      int rdy = bsg_lr(&curr->right_done);
      if (!rdy) bsg_lr_aq(&curr->right_done);
      asm volatile("" ::: "memory");

      rdy = bsg_lr(&curr->bottom_done);
      if (!rdy) bsg_lr_aq(&curr->bottom_done);
      asm volatile("" ::: "memory");

      // Pull qry / ref from DRAM at the pod's left/top edges; everyone
      // else copies from their chained neighbor below.
      if (!__bsg_x) {
        unrolled_load<uint8_t, QRY_CORE>(curr->qrybuf,
          &qry[SEQ_LEN * i + (__bsg_y * QRY_CORE)]);
      }
      if (!__bsg_y) {
        unrolled_load<uint8_t, REF_CORE>(curr->refbuf,
          &ref[SEQ_LEN * i + (__bsg_x * REF_CORE)]);
      }

      int maxv = 0;

      // Pull left neighbor's right_col + max + qrybuf chain.
      if (__bsg_x) {
        int rdy_left = bsg_lr(&curr->max_left);
        if (rdy_left == -1) bsg_lr_aq(&curr->max_left);
        asm volatile("" ::: "memory");
        maxv = max(maxv, curr->max_left);
        curr->max_left = -1;
        unrolled_load<uint8_t, QRY_CORE>(curr->qrybuf, curr->next_qry);
        unrolled_load<int,     QRY_CORE + 1>(left_col, curr->left_right_col);
        *(curr->left_done) = 1;
      } else {
        for (int j = 0; j <= QRY_CORE; j++) left_col[j] = 0;
      }

      // Pull top neighbor's bottom_row + max + refbuf chain.
      if (__bsg_y) {
        int rdy_top = bsg_lr(&curr->max_top);
        if (rdy_top == -1) bsg_lr_aq(&curr->max_top);
        asm volatile("" ::: "memory");
        maxv = max(maxv, curr->max_top);
        curr->max_top = -1;
        unrolled_load<uint8_t, REF_CORE>(curr->refbuf, curr->next_ref);
        unrolled_load<int,     REF_CORE + 1>(working_row, curr->top_bottom_row);
        *(curr->top_done) = 1;
      } else {
        for (int k = 0; k <= REF_CORE; k++) working_row[k] = 0;
      }

      // Snapshot the corner cell into right_col[0].  At this point
      // working_row[REF_CORE] = top's bottom_row[REF_CORE] (or 0 if
      // y==0), which is exactly our dp[0][REF_CORE].
      curr->right_col[0] = working_row[REF_CORE];

      for (int j = 1; j <= QRY_CORE; j++) {
        int old_diag = working_row[0];      // dp[j-1][0]
        working_row[0] = left_col[j];       // dp[j][0]
        for (int k = 1; k <= REF_CORE; k++) {
          int up   = working_row[k];        // dp[j-1][k]
          int left = working_row[k - 1];    // dp[j][k-1]
          int diag = old_diag;              // dp[j-1][k-1]
          int match = (curr->qrybuf[j - 1] == curr->refbuf[k - 1]) ? MATCH : MISMATCH;
          int v = max(0, diag + match, up - GAP, left - GAP);
          if (v > maxv) maxv = v;
          old_diag = up;
          working_row[k] = v;
        }
        curr->right_col[j] = working_row[REF_CORE];
      }

      // working_row now holds dp[QRY_CORE][*]; expose it as bottom_row.
      for (int k = 0; k <= REF_CORE; k++) curr->bottom_row[k] = working_row[k];

      // Push max to right + bottom and clear our done flags so the
      // neighbors can read.
      if (__bsg_x < (bsg_tiles_X - 1)) {
        curr->right_done = 0;
        *(curr->right_max) = maxv;
      }
      if (__bsg_y < (bsg_tiles_Y - 1)) {
        curr->bottom_done = 0;
        *(curr->bottom_max) = maxv;
      }

      // Bottom-right tile carries the final score.
      if ((__bsg_x == bsg_tiles_X - 1) && (__bsg_y == bsg_tiles_Y - 1)) {
        output[output_idx] = maxv;
      }
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
