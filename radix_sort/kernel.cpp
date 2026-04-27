#include <bsg_cuda_lite_barrier.h>
#include <bsg_manycore.h>

// count[] is extended from 16 to 17 ints. count[0..15] is the bucket-count
// data path (unchanged). count[16] is the per-tile wakeup flag for push/pull
// rendezvous: pull writes peer's count[16] = 1 at the end; push waits on own
// count[16] != 0, then resets. Decoupling the wakeup from count[0] avoids
// the lr.w.aq race when pull's data write and push's lr/lr_aq overlap.
// Putting it in the same array means peer's flag is just &rmt[16] — no
// extra bsg_remote_ptr() call per pull site.

/*inline __attribute__((always_inline))*/ void accumulate(int *count,
                                                          int *rmt) {
  register int r0 = rmt[0];
  register int r1 = rmt[1];
  register int r2 = rmt[2];
  register int r3 = rmt[3];
  register int r4 = rmt[4];
  register int r5 = rmt[5];
  register int r6 = rmt[6];
  register int r7 = rmt[7];
  register int r8 = rmt[8];
  register int r9 = rmt[9];
  register int r10 = rmt[10];
  register int r11 = rmt[11];
  register int r12 = rmt[12];
  register int r13 = rmt[13];
  register int r14 = rmt[14];
  register int r15 = rmt[15];
  asm volatile("" ::: "memory");
  count[0] += r0;
  count[1] += r1;
  asm volatile("" ::: "memory");
  count[2] += r2;
  count[3] += r3;
  asm volatile("" ::: "memory");
  count[4] += r4;
  count[5] += r5;
  asm volatile("" ::: "memory");
  count[6] += r6;
  count[7] += r7;
  asm volatile("" ::: "memory");
  count[8] += r8;
  count[9] += r9;
  asm volatile("" ::: "memory");
  count[10] += r10;
  count[11] += r11;
  asm volatile("" ::: "memory");
  count[12] += r12;
  count[13] += r13;
  asm volatile("" ::: "memory");
  count[14] += r14;
  count[15] += r15;
}

/*inline __attribute__((always_inline))*/ void pull(int *count, int *rmt) {
  register int c = count[0];
  register int r0 = rmt[0];
  rmt[0] = c;
  register int r1 = rmt[1];
  register int r2 = rmt[2];
  register int r3 = rmt[3];
  register int r4 = rmt[4];
  register int r5 = rmt[5];
  register int r6 = rmt[6];
  register int r7 = rmt[7];
  register int r8 = rmt[8];
  register int r9 = rmt[9];
  register int r10 = rmt[10];
  register int r11 = rmt[11];
  register int r12 = rmt[12];
  register int r13 = rmt[13];
  register int r14 = rmt[14];
  register int r15 = rmt[15];
  asm volatile("" ::: "memory");
  count[0] += r0;
  count[1] += r1;
  count[2] += r2;
  count[3] += r3;
  count[4] += r4;
  count[5] += r5;
  count[6] += r6;
  count[7] += r7;
  count[8] += r8;
  count[9] += r9;
  count[10] += r10;
  count[11] += r11;
  count[12] += r12;
  count[13] += r13;
  count[14] += r14;
  count[15] += r15;
  asm volatile("" ::: "memory");
  ((volatile int*)rmt)[16] = 1;  // signal partner's push() to proceed
}

/*inline __attribute__((always_inline))*/ void push(int *count, int *rmt) {
  // Wait for own count[16] != 0 (sw/1d-style: skip lr_aq if already set).
  int rdy = bsg_lr(&count[16]);
  if (rdy == 0) {
    bsg_lr_aq(&count[16]);
  }
  asm volatile("" ::: "memory");
  count[16] = 0;  // reset for next push/pull pair
  register int r1 = rmt[1];
  register int r2 = rmt[2];
  register int r3 = rmt[3];
  register int r4 = rmt[4];
  register int r5 = rmt[5];
  register int r6 = rmt[6];
  register int r7 = rmt[7];
  register int r8 = rmt[8];
  register int r9 = rmt[9];
  register int r10 = rmt[10];
  register int r11 = rmt[11];
  register int r12 = rmt[12];
  register int r13 = rmt[13];
  register int r14 = rmt[14];
  register int r15 = rmt[15];
  asm volatile("" ::: "memory");
  count[1] = r1;
  count[2] = r2;
  count[3] = r3;
  count[4] = r4;
  count[5] = r5;
  count[6] = r6;
  count[7] = r7;
  count[8] = r8;
  count[9] = r9;
  count[10] = r10;
  count[11] = r11;
  count[12] = r12;
  count[13] = r13;
  count[14] = r14;
  count[15] = r15;
}

inline __attribute__((always_inline)) void prefix(int *count, int *rmt) {
  register int r0 = rmt[0];
  register int r1 = rmt[1];
  register int r2 = rmt[2];
  register int r3 = rmt[3];
  register int r4 = rmt[4];
  register int r5 = rmt[5];
  register int r6 = rmt[6];
  register int r7 = rmt[7];
  register int r8 = rmt[8];
  register int r9 = rmt[9];
  register int r10 = rmt[10];
  register int r11 = rmt[11];
  register int r12 = rmt[12];
  register int r13 = rmt[13];
  register int r14 = rmt[14];
  register int r15 = rmt[15];
  asm volatile("" ::: "memory");
  register int sum = 0;
  register int t = r0 + count[0];
  count[0] = sum;
  sum += t;
  t = r1 + count[1];
  count[1] = sum;
  sum += t;
  t = r2 + count[2];
  count[2] = sum;
  sum += t;
  t = r3 + count[3];
  count[3] = sum;
  sum += t;
  t = r4 + count[4];
  count[4] = sum;
  sum += t;
  t = r5 + count[5];
  count[5] = sum;
  sum += t;
  t = r6 + count[6];
  count[6] = sum;
  sum += t;
  t = r7 + count[7];
  count[7] = sum;
  sum += t;
  t = r8 + count[8];
  count[8] = sum;
  sum += t;
  t = r9 + count[9];
  count[9] = sum;
  sum += t;
  t = r10 + count[10];
  count[10] = sum;
  sum += t;
  t = r11 + count[11];
  count[11] = sum;
  sum += t;
  t = r12 + count[12];
  count[12] = sum;
  sum += t;
  t = r13 + count[13];
  count[13] = sum;
  sum += t;
  t = r14 + count[14];
  count[14] = sum;
  count[15] = sum + t;
}

void copy_line(int *src, int *dst) {
  register int r0 = src[0];
  bsg_fence();
  register int r1 = src[1];
  register int r2 = src[2];
  register int r3 = src[3];
  register int r4 = src[4];
  register int r5 = src[5];
  register int r6 = src[6];
  register int r7 = src[7];
  register int r8 = src[8];
  register int r9 = src[9];
  register int r10 = src[10];
  register int r11 = src[11];
  register int r12 = src[12];
  register int r13 = src[13];
  register int r14 = src[14];
  register int r15 = src[15];
  asm volatile("" ::: "memory");
  dst[0] = r0;
  dst[1] = r1;
  dst[2] = r2;
  dst[3] = r3;
  dst[4] = r4;
  dst[5] = r5;
  dst[6] = r6;
  dst[7] = r7;
  dst[8] = r8;
  dst[9] = r9;
  dst[10] = r10;
  dst[11] = r11;
  dst[12] = r12;
  dst[13] = r13;
  dst[14] = r14;
  dst[15] = r15;
}

inline __attribute__((always_inline)) void scan(int *count, int *dram, int len, int j) {
  int temp[16];
  for (int *i = dram; i < dram + len; i += 16) {
    copy_line(i, temp);
    for (int k = 0; k < 16; k++) {
      count[(temp[k] >> j) & 15]++;
    }
  }
}

inline __attribute__((always_inline)) void scatter(int *recv, int *count, int *dram, int len, int j) {
  int temp[16];
  for (int *i = dram; i < dram + len; i += 16) {
    copy_line(i, temp);
    for (int k = 0; k < 16; k++) {
      recv[count[(temp[k] >> j) & 15]++] = temp[k];
    }
  }
}

int count[17];   // [0..15] = bucket counts; [16] = wakeup flag

inline void prefix_sum(int *count, int rx, int ry, int cx, int cy, int px,
                       int py, int mx, int my) {
  int *rmt;
  int i, k;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  for (i = 1; i < my; i *= 2) {
    register int k = 2 * i;
    if (!(ry & (k - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      accumulate(count, rmt);
    }
  }
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    accumulate(count, rmt);
    for (i = 1; i < mx; i *= 2) {
      register int k = 2 * i;
      if (!(rx & (k - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, __bsg_y, count);
        accumulate(count, rmt);
      }
    }
    if (__bsg_x == mx) {
      rmt = (int*) bsg_remote_ptr(mx - 1, my, count);
      prefix(count, rmt);
      pull(count, rmt);
    }
  }
  if (__bsg_x == mx - 1 && __bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(mx, my, count);
    push(count, rmt);
  }
  for (k = mx; k > 1; k /= 2) {
    register int i = k / 2;
    if (__bsg_y == my) {
      if (!(rx & (k - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, my, count);
        pull(count, rmt);
      } else if (!(rx & (i - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + px * i, my, count);
        push(count, rmt);
      }
    }
  }
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    pull(count, rmt);
  } else if (__bsg_y == my - 1) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my, count);
    push(count, rmt);
  }
  for (k = my; k > 1; k /= 2) {
    register int i = k / 2;
    if (!(ry & (k - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      pull(count, rmt);
    } else if (!(ry & (i - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + py * i, count);
      push(count, rmt);
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
}

// Compile-time STAGE bisect. Build with `STAGE=N make exec.log ...`.
// Stage 6 is split into three sub-stages so we can localize *which* iter
// of the X down-sweep loop hangs.
//
// 1   init + epilogue only (no work)
// 2   + ID setup + scan (j=0)
// 3   + Y up-sweep
// 4   + half-combine + X up-sweep
// 5   + seam prefix() + pull() + push()
// 6   + X down-sweep iter 1 (kk=mx, i=mx/2)
// 7   + X down-sweep iter 2 (kk=mx/2, i=mx/4)
// 8   + X down-sweep iter 3 (kk=2, i=1)  [full X down-sweep done]
// 9   + half-redistribute (y=my-1 ↔ y=my)
// 10  + Y down-sweep
// 11  + scatter (j=0)
// 12  + remaining 7 outer-loop iters using full prefix_sum() (default)

#ifndef STAGE
#define STAGE 12
#endif

extern "C" int kernel(int *A, int *B, int N) {
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  int my = bsg_tiles_Y / 2;
  int mx = bsg_tiles_X / 2;
  int rx = 0, ry = 0, cx = 0, cy = 0, px = 0, py = 0, id = 0;
  if (__bsg_x < mx) {
    rx = mx - 1 - __bsg_x;
    id = __bsg_x * bsg_tiles_Y;
    cx = -1; px = 1;
  } else {
    rx = __bsg_x - mx;
    id = (bsg_tiles_X - rx - 1) * bsg_tiles_Y;
    cx = 1; px = -1;
  }
  if (__bsg_y < my) {
    ry = my - 1 - __bsg_y;
    id += __bsg_y;
    cy = -1; py = 1;
  } else {
    ry = __bsg_y - my;
    id += bsg_tiles_Y - 1 - ry;
    cy = 1; py = -1;
  }
  int len = N / (bsg_tiles_X * bsg_tiles_Y);
  int off = id * len;
  int *send = A;
  int *recv = B;
  int *dram = send + off;
  int *rmt = nullptr;
  (void)rmt; (void)px; (void)py;  // silence unused warnings at low STAGE

#if STAGE >= 2
  for (int k = 0; k < 16; k++) count[k] = 0;
  scan(count, dram, len, 0);
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 3
  // Y up-sweep — every core executes the loop, only some do the accumulate.
  for (int i = 1; i < my; i *= 2) {
    int k2 = 2 * i;
    if (!(ry & (k2 - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      accumulate(count, rmt);
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 4
  // half-combine + X up-sweep — only y=my row does work; everyone barriers.
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    accumulate(count, rmt);
    for (int i = 1; i < mx; i *= 2) {
      int k2 = 2 * i;
      if (!(rx & (k2 - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, __bsg_y, count);
        accumulate(count, rmt);
      }
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 5
  // Seam: prefix() + pull() at (mx, my); push() at (mx-1, my).
  if (__bsg_y == my && __bsg_x == mx) {
    rmt = (int*) bsg_remote_ptr(mx - 1, my, count);
    prefix(count, rmt);
    pull(count, rmt);
  }
  if (__bsg_x == mx - 1 && __bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(mx, my, count);
    push(count, rmt);
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 6
  // X down-sweep iter 1 (kk=mx, i=mx/2).
  {
    int kk = mx;
    int i = kk / 2;
    if (__bsg_y == my) {
      if (!(rx & (kk - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, my, count);
        pull(count, rmt);
      } else if (!(rx & (i - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + px * i, my, count);
        push(count, rmt);
      }
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 7
  // X down-sweep iter 2 (kk=mx/2, i=mx/4).
  {
    int kk = mx / 2;
    int i = kk / 2;
    if (__bsg_y == my) {
      if (!(rx & (kk - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, my, count);
        pull(count, rmt);
      } else if (!(rx & (i - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + px * i, my, count);
        push(count, rmt);
      }
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 8
  // X down-sweep iter 3 (kk=2, i=1) — final iter.
  {
    int kk = 2;
    int i = 1;
    if (__bsg_y == my) {
      if (!(rx & (kk - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, my, count);
        pull(count, rmt);
      } else if (!(rx & (i - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + px * i, my, count);
        push(count, rmt);
      }
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 9
  // Half-redistribute (y=my pulls from y=my-1; y=my-1 pushes to y=my).
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    pull(count, rmt);
  } else if (__bsg_y == my - 1) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my, count);
    push(count, rmt);
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 10
  // Y down-sweep — every core executes the loop, only some do work.
  for (int kk = my; kk > 1; kk /= 2) {
    int i = kk / 2;
    if (!(ry & (kk - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      pull(count, rmt);
    } else if (!(ry & (i - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + py * i, count);
      push(count, rmt);
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 11
  // Scatter for j=0.
  scatter(recv, count, dram, len, 0);
  bsg_fence();
  bsg_barrier_tile_group_sync();
#endif

#if STAGE >= 12
  // Remaining 7 outer-loop iters using the full prefix_sum() function.
  int *tmptr = send; send = recv; recv = tmptr;
  for (int j = 4; j < 32; j += 4) {
    dram = send + off;
    for (int k = 0; k < 16; k++) count[k] = 0;
    scan(count, dram, len, j);
    prefix_sum(count, rx, ry, cx, cy, px, py, mx, my);
    scatter(recv, count, dram, len, j);
    tmptr = send; send = recv; recv = tmptr;
  }
#endif

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
