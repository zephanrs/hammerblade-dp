// dummy/vvadd — canonical memory-bound vvadd benchmark.
//
//   for i in 0..N: C[i] = A[i] + B[i]
//
// Per element: 2 loads (8 B) + 1 store (4 B) = 12 B of DRAM traffic and
// 1 add of compute.  Arithmetic intensity = 1 / 12 ≈ 0.083 ops/byte —
// firmly memory-bound on every plausible HW.  Used to answer the
// strategic question: does HammerBlade's *DRAM bandwidth* scale with
// core clock, or is it constant?
//
//   - If fast/slow BW ratio ≈ 1×  → DRAM is the actual ceiling and the
//                                    roofline regression at low AI was
//                                    driven by per-element compute and
//                                    stores; memory-bound code can be
//                                    pushed harder via MLP-only fixes.
//   - If fast/slow BW ratio ≈ 32× → the per-core NoC injection rate or
//                                    vcache MSHR depth scales with
//                                    clock; "constant DRAM BW across
//                                    clocks" is unreachable from
//                                    software, document and move on.
//
// Critical: A, B, C must be `volatile int *`.  Without it the compiler
// hoists each address's load out of the rep loop (loop-invariant) and
// collapses repeated reads — we measured this on dram_read where 80 GB
// of "reads" came back at 10 GB/s per pod (impossible) because the
// loop body had been DCE'd to nothing.

#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>

#ifndef N_ELEMS
#define N_ELEMS 1048576  // 4 MB per array — 12 MB working set per pod
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

#ifndef UNROLL
#define UNROLL 16
#endif

extern "C" int kernel(volatile int *A, volatile int *B, volatile int *C,
                      int /*pod_id*/) {
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int n_tiles = bsg_tiles_X * bsg_tiles_Y;
  const int tile_id = __bsg_x * bsg_tiles_Y + __bsg_y;
  const int chunk = (N_ELEMS + n_tiles - 1) / n_tiles;
  const int start = tile_id * chunk;
  const int end   = (start + chunk < N_ELEMS) ? start + chunk : N_ELEMS;
  const int unroll_end = start + ((end - start) / UNROLL) * UNROLL;

  for (int rep = 0; rep < REPEAT; rep++) {
    int i = start;
    for (; i + UNROLL <= unroll_end; i += UNROLL) {
      int a[UNROLL], b[UNROLL];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) a[u] = A[i + u];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) b[u] = B[i + u];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) C[i + u] = a[u] + b[u];
    }
    for (; i < end; i++) C[i] = A[i] + B[i];
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
