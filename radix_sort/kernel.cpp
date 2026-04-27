#include <bsg_cuda_lite_barrier.h>
#include <bsg_manycore.h>

enum {
  FLAG_PUSH = 16,
  FLAG_UP_Y_2 = 17,
  FLAG_UP_Y_4 = 18,
  FLAG_UP_Y_8 = 19,
  FLAG_UP_X_2 = 20,
  FLAG_UP_X_4 = 21,
  FLAG_UP_X_8 = 22,
  FLAG_UP_X_16 = 23,
  FLAG_UP_X_32 = 24,
  FLAG_UP_X_64 = 25,
  FLAG_UP_X_128 = 26,
  FLAG_UP_X_256 = 27,
  FLAG_PHASE_Y = 28,
  FLAG_PHASE_X = 29,
  COUNT_WORDS = 30,
};

inline __attribute__((always_inline)) void wait_flag(int *count, int flag) {
  int rdy = bsg_lr(&count[flag]);
  if (rdy == 0) {
    bsg_lr_aq(&count[flag]);
  }
  asm volatile("" ::: "memory");
  ((volatile int*)count)[flag] = 0;
  asm volatile("" ::: "memory");
}

inline __attribute__((always_inline)) void signal_flag(int *rmt, int flag) {
  asm volatile("" ::: "memory");
  bsg_fence();
  asm volatile("" ::: "memory");
  ((volatile int*)rmt)[flag] = 1;
  asm volatile("" ::: "memory");
}

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
  // Down-sweep exchange: peer gets our old prefix, we advance by peer's total.
  // The peer must not copy from our count[] after we resume and mutate it.
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
  rmt[0] = count[0];
  rmt[1] = count[1];
  rmt[2] = count[2];
  rmt[3] = count[3];
  rmt[4] = count[4];
  rmt[5] = count[5];
  rmt[6] = count[6];
  rmt[7] = count[7];
  rmt[8] = count[8];
  rmt[9] = count[9];
  rmt[10] = count[10];
  rmt[11] = count[11];
  rmt[12] = count[12];
  rmt[13] = count[13];
  rmt[14] = count[14];
  rmt[15] = count[15];
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
  signal_flag(rmt, FLAG_PUSH);
}

/*inline __attribute__((always_inline))*/ void push(int *count) {
  wait_flag(count, FLAG_PUSH);
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

int count[COUNT_WORDS];   // [0..15] = bucket counts; the rest are wait flags

inline void prefix_sum(int *count, int rx, int ry, int cx, int cy, int px,
                       int py, int mx, int my) {
  int *rmt;
  int i, k;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  // Y up-sweep.
  int up_signal_flag = FLAG_UP_Y_2;
  for (i = 1; i < my; i *= 2, up_signal_flag++) {
    register int k = 2 * i;
    if (!(ry & (k - 1))) {
      if (i > 1) {
        wait_flag(count, up_signal_flag - 1);
      }
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      accumulate(count, rmt);
    }
    if (k < my && ((ry & ((2 * k) - 1)) == k)) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + py * k, count);
      signal_flag(rmt, up_signal_flag);
    }
  }
  // Phase 1 sync: top-half root must finish before bottom-half root combines it.
  if (__bsg_y == my - 1) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my, count);
    signal_flag(rmt, FLAG_PHASE_Y);
  } else if (__bsg_y == my) {
    wait_flag(count, FLAG_PHASE_Y);
  }
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    accumulate(count, rmt);
    // X up-sweep.
    up_signal_flag = FLAG_UP_X_2;
    for (i = 1; i < mx; i *= 2, up_signal_flag++) {
      register int k = 2 * i;
      if (!(rx & (k - 1))) {
        if (i > 1) {
          wait_flag(count, up_signal_flag - 1);
        }
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, __bsg_y, count);
        accumulate(count, rmt);
      }
      if (k < mx && ((rx & ((2 * k) - 1)) == k)) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + px * k, __bsg_y, count);
        signal_flag(rmt, up_signal_flag);
      }
    }
  }
  // Phase 2 sync: left-half X root must finish before the seam prefix reads it.
  if (__bsg_y == my) {
    if (__bsg_x == mx - 1) {
      rmt = (int*) bsg_remote_ptr(mx, my, count);
      signal_flag(rmt, FLAG_PHASE_X);
    } else if (__bsg_x == mx) {
      wait_flag(count, FLAG_PHASE_X);
    }
  }
  if (__bsg_y == my && __bsg_x == mx) {
    rmt = (int*) bsg_remote_ptr(mx - 1, my, count);
    prefix(count, rmt);
    pull(count, rmt);
  }
  if (__bsg_x == mx - 1 && __bsg_y == my) {
    push(count);
  }
  for (k = mx; k > 1; k /= 2) {
    register int i = k / 2;
    if (__bsg_y == my) {
      if (!(rx & (k - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, my, count);
        pull(count, rmt);
      } else if (!(rx & (i - 1))) {
        push(count);
      }
    }
  }
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    pull(count, rmt);
  } else if (__bsg_y == my - 1) {
    push(count);
  }
  for (k = my; k > 1; k /= 2) {
    register int i = k / 2;
    if (!(ry & (k - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      pull(count, rmt);
    } else if (!(ry & (i - 1))) {
      push(count);
    }
  }
  bsg_fence();
  bsg_barrier_tile_group_sync();
}

extern "C" int kernel(int *A, int *B, int N) {
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  int my = bsg_tiles_Y / 2;
  int mx = bsg_tiles_X / 2;
  int rx, ry, cx, cy, px, py, id;
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
  int *dram, *tmptr;

  for (int j = 0; j < 32; j += 4) {
    // Barrier 1: before scan. Ensures the previous iter's scatter writes
    // are globally visible before this iter reads from send (= old recv).
    bsg_fence();
    bsg_barrier_tile_group_sync();

    dram = send + off;
    for (int k = 0; k < 16; k++) count[k] = 0;
    scan(count, dram, len, j);
    // Barrier 2 (inside prefix_sum): before reduction. Ensures all tiles'
    // scan results are visible before any tile starts the up-sweep.
    prefix_sum(count, rx, ry, cx, cy, px, py, mx, my);
    scatter(recv, count, dram, len, j);
    tmptr = send; send = recv; recv = tmptr;
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
