// dummy/barrier_bench — back-to-back tile-group barrier microbenchmark.
//
// Calls bsg_barrier_tile_group_sync() N_BARRIERS times in a tight loop
// with no other work between them, so:
//
//   per_barrier_latency = (kernel_end - kernel_start) / N_BARRIERS
//
// The two test variants (`barrier=default` and `barrier=linear`) compile
// the same kernel — they differ only in which symbol `bsg_barrier_amoadd`
// resolves to.  When `barrier=linear`, template.mk links
// barriers/linear_barrier.S first; that .S defines bsg_barrier_amoadd
// with the linear wakeup pattern, which the linker prefers over the
// archive copy (libbsg_manycore_riscv.a's tree-wakeup version).
// `bsg_barrier_tile_group_sync()` (a static inline that calls
// bsg_barrier_amoadd) thus picks up whichever implementation we
// supplied — no #ifdef needed in this file.

#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>

#ifndef N_BARRIERS
#define N_BARRIERS 1000
#endif

extern "C" int kernel(int /*pod_id*/) {
  bsg_barrier_tile_group_init();
  // Warmup: ensures every tile is past program start before the timed
  // region opens.  Otherwise the first measured barrier soaks up
  // straggler entry latency.
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  for (int i = 0; i < N_BARRIERS; i++) {
    bsg_barrier_tile_group_sync();
  }

  bsg_cuda_print_stat_kernel_end();
  // Closing barrier so kernel_end stat is collected from all tiles.
  bsg_barrier_tile_group_sync();
  return 0;
}
