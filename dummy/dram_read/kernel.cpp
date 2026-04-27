// dummy/dram_read — read-only DRAM bandwidth probe.
//
// Reads N_ELEMS ints from DRAM, REPEAT times. No stores, no compute (one
// XOR per load just to keep the compiler from killing the loads).  The
// idea: if even this trivial kernel's BW drops with core clock, the
// ceiling we're hitting in roofline isn't compute pressure — it's
// per-core NoC injection rate or vcache MSHR depth (both scale with
// frequency in real time even though the in-cycle pattern is constant).
// If this kernel's BW IS roughly constant across fast/slow, then the
// roofline regression at low AI really is from the chain+store work,
// and the fix is to push the memory-bound knee toward higher MLP.
//
// Working-set sizing:
//   N_ELEMS = 65536 ints = 256 KB → 2× the per-pod 128 KB vcache.
//   Each pod's 128 tiles split this contiguously: 512 ints / 2 KB per
//   tile.  Sequential per-tile access keeps DRAM row-buffer hits high.

#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include <cstdint>

#ifndef N_ELEMS
#define N_ELEMS 65536  // 256 KB
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

#ifndef UNROLL
#define UNROLL 16
#endif

extern "C" int kernel(int* input, int /*pod_id*/) {
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int n_tiles = bsg_tiles_X * bsg_tiles_Y;
  const int tile_id = __bsg_x * bsg_tiles_Y + __bsg_y;

  const int chunk = (N_ELEMS + n_tiles - 1) / n_tiles;
  const int start = tile_id * chunk;
  const int end   = (start + chunk < N_ELEMS) ? start + chunk : N_ELEMS;
  const int unroll_end = start + ((end - start) / UNROLL) * UNROLL;

  // Sink — XOR-accumulate so loads are live but contribute zero real
  // work beyond the dependency.  The asm volatile at the end pins it
  // live across the kernel boundary so the optimizer can't hollow the
  // loop out.
  int sink = 0;

  for (int rep = 0; rep < REPEAT; rep++) {
    int i = start;

    if (i + UNROLL <= unroll_end) {
      // Prologue: fire UNROLL non-blocking loads.
      int next_v[UNROLL];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) next_v[u] = input[i + u];

      // Steady state: prefetch next batch while consuming current via XOR.
      // No stores, so the only per-element work is one xor — minimal
      // dependency chain on the load result.
      for (; i + 2 * UNROLL <= unroll_end; i += UNROLL) {
        int v[UNROLL];
        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) v[u] = next_v[u];

        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) next_v[u] = input[i + UNROLL + u];

        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) sink ^= v[u];
      }

      // Epilogue.
      int v[UNROLL];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) v[u] = next_v[u];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) sink ^= v[u];
      i += UNROLL;
    }

    for (; i < end; i++) sink ^= input[i];
  }

  // Force sink to be live so the chain isn't dead-code-eliminated.
  asm volatile("" : : "r"(sink));

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
