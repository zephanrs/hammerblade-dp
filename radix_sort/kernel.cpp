#include <bsg_cuda_lite_barrier.h>
#include <bsg_manycore.h>

// Debug instrumentation: tile (0,0) prints kernel-level boundaries; tile
// (mx, my) = (8, 4) prints prefix_sum phase boundaries. After a hang, the
// last printed value tells us which phase deadlocked.
//
// Marker scheme (decimal):
//   1     kernel start (after init barrier)
//   100+j outer-loop iter start (j = 0,4,...,28)
//   200+j after scan, before prefix_sum
//   300+j after prefix_sum return
//   400+j after scatter
//   999   kernel end
//   10    prefix_sum: entered (post initial barrier)
//   20    prefix_sum: after Y up-sweep loop
//   30    prefix_sum: after half-combine
//   40    prefix_sum: after X up-sweep loop
//   50    prefix_sum: after prefix() + pull() at seam
//   60    prefix_sum: after X down-sweep loop
//   70    prefix_sum: after half-redistribute
//   80    prefix_sum: after Y down-sweep loop
//   90    prefix_sum: end (before final barrier)
static inline void dbg(int v) {
  if (__bsg_x == 0 && __bsg_y == 0) bsg_print_int(v);
}
static inline void dbg_seam(int v) {
  if (__bsg_x == (bsg_tiles_X/2) && __bsg_y == (bsg_tiles_Y/2)) bsg_print_int(v);
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
}

/*inline __attribute__((always_inline))*/ void push(int *count, int *rmt) {
  bsg_lr(count);
  bsg_lr_aq(count);
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

int count[16];

inline void prefix_sum(int *count, int rx, int ry, int cx, int cy, int px,
                       int py, int mx, int my) {
  int *rmt;
  int i, k;
  bsg_fence();
  bsg_barrier_hw_tile_group_sync();
  dbg_seam(10);
  for (i = 1; i < my; i *= 2) {
    register int k = 2 * i;
    if (!(ry & (k - 1))) {
      rmt = (int*) bsg_remote_ptr(__bsg_x, __bsg_y + cy * i, count);
      accumulate(count, rmt);
    }
  }
  dbg_seam(20);
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    accumulate(count, rmt);
    dbg_seam(30);
    for (i = 1; i < mx; i *= 2) {
      register int k = 2 * i;
      if (!(rx & (k - 1))) {
        rmt = (int*) bsg_remote_ptr(__bsg_x + cx * i, __bsg_y, count);
        accumulate(count, rmt);
      }
    }
    dbg_seam(40);
    if (__bsg_x == mx) {
      rmt = (int*) bsg_remote_ptr(mx - 1, my, count);
      prefix(count, rmt);
      pull(count, rmt);
    }
    dbg_seam(50);
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
  dbg_seam(60);
  if (__bsg_y == my) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my - 1, count);
    pull(count, rmt);
  } else if (__bsg_y == my - 1) {
    rmt = (int*) bsg_remote_ptr(__bsg_x, my, count);
    push(count, rmt);
  }
  dbg_seam(70);
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
  dbg_seam(80);
  bsg_fence();
  bsg_barrier_hw_tile_group_sync();
  dbg_seam(90);
}

extern "C" __attribute__((noinline)) int kernel_radix_sort(int *A, int *B,
                                                           int N) {
  bsg_barrier_hw_tile_group_init();
  bsg_cuda_print_stat_kernel_start();
  bsg_fence();
  bsg_barrier_hw_tile_group_sync();
  dbg(1);
  int id, my, mx, cy, cx, py, px, ry, rx;
  int *rmt;
  my = (bsg_tiles_Y / 2);
  mx = (bsg_tiles_X / 2);
  if (__bsg_x < mx) {
    rx = mx - 1 - __bsg_x;
    id = __bsg_x * bsg_tiles_Y;
    cx = -1;
    px = 1;
  } else {
    rx = __bsg_x - mx;
    id = (bsg_tiles_X - rx - 1) * bsg_tiles_Y;
    cx = 1;
    px = -1;
  }
  if (__bsg_y < my) {
    ry = my - 1 - __bsg_y;
    id += __bsg_y;
    cy = -1;
    py = 1;
  } else {
    ry = __bsg_y - my;
    id += bsg_tiles_Y - 1 - ry;
    cy = 1;
    py = -1;
  }
  int len = N / (bsg_tiles_X * bsg_tiles_Y);
  int off = id * len;
  int *send, *recv, *dram, *tmptr;
  send = A;
  recv = B;
  for (int j = 0; j < 32; j += 4) {
    dbg(100 + j);
    dram = send + off;
    for (int k = 0; k < 16; k++) {
      count[k] = 0;
    }
    scan(count, dram, len, j);
    dbg(200 + j);
    prefix_sum(count, rx, ry, cx, cy, px, py, mx, my);
    dbg(300 + j);
    scatter(recv, count, dram, len, j);
    dbg(400 + j);
    tmptr = send;
    send = recv;
    recv = tmptr;
  }

  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  bsg_fence();
  bsg_barrier_hw_tile_group_sync();
  dbg(999);
  return 0;
}
