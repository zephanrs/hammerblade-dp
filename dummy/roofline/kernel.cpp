#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include <cstdint>

// OPS_PER_ELEM: number of multiply-add pairs per input element.
// Sweeping this from 1 (memory-bound) to 1024+ (compute-bound) traces the roofline.
//
// Operational intensity = 2*OPS_PER_ELEM / 8  ops/byte
//   (8 bytes DRAM traffic per element: 4 read + 4 write;
//    2 integer ops per OPS_PER_ELEM iteration: multiply + add)
//
// Anti-optimization strategy:
//   1. Each inner-loop iteration uses j (the loop index), so the recurrence
//      val = val*31 + j has no closed form the compiler can substitute.
//   2. asm volatile("" : "+r"(val)) after the loop marks val as potentially
//      modified, preventing the compiler from proving the chain is dead.
//   3. The result is written back to DRAM, so the entire chain is live.
//
// N_ELEMS must exceed the cache (64 kB = 16384 ints) so measurements reflect
// true DRAM bandwidth, not cache bandwidth.

#ifndef OPS_PER_ELEM
#define OPS_PER_ELEM 1
#endif

#ifndef N_ELEMS
#define N_ELEMS 1048576   // 4 MB — well above 64 kB cache
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

extern "C" int kernel(int* input, int* output, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int n_tiles = bsg_tiles_X * bsg_tiles_Y;
  const int tile_id = __bsg_x * bsg_tiles_Y + __bsg_y;

  for (int rep = 0; rep < REPEAT; rep++) {
    for (int i = tile_id; i < N_ELEMS; i += n_tiles) {
      int val = input[i];

      // Data-dependent chain: val feeds into every iteration, j prevents
      // the compiler from finding a closed-form simplification.
      for (int j = 0; j < OPS_PER_ELEM; j++) {
        val = val * 31 + j;
      }

      // Force val to be live across the asm boundary so the compiler cannot
      // prove the loop result is unused and eliminate it.
      asm volatile("" : "+r"(val));

      output[i] = val;
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
