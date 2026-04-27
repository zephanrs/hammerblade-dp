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
// Memory-level-parallelism strategy:
//   The Vanilla core has non-blocking loads (writeback stalls only when the
//   destination register is *consumed*). To saturate NoC/DRAM bandwidth at
//   low operational intensity, we (a) unroll the outer loop by UNROLL so the
//   compiler emits UNROLL independent loads back-to-back, and (b) software-
//   pipeline by prefetching the *next* batch of UNROLL elements before
//   running the dependent mul-add chain on the *current* batch. With enough
//   in-flight loads the kernel becomes throughput-bound (NoC issue rate),
//   which scales with the core clock — so slow-clock vs fast-clock
//   measurements should now show the same ratio as vvadd (~32x), instead of
//   being inflated by latency-hiding when the core slows down.
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

#ifndef UNROLL
#define UNROLL 8
#endif

static inline __attribute__((always_inline))
void chain(int *v, int j) {
  asm volatile(
    "mul %[v], %[v], %[c]\n"
    "add %[v], %[v], %[j]\n"
    : [v] "+r"(*v)
    : [c] "r"(31), [j] "r"(j)
  );
}

extern "C" int kernel(int* input, int* output, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int n_tiles = bsg_tiles_X * bsg_tiles_Y;
  const int tile_id = __bsg_x * bsg_tiles_Y + __bsg_y;

  // Each tile gets a unique contiguous chunk — no shared cache lines between cores,
  // and no striding so DRAM row-buffer hits are maximised.
  const int chunk = (N_ELEMS + n_tiles - 1) / n_tiles;
  const int start = tile_id * chunk;
  const int end   = (start + chunk < N_ELEMS) ? start + chunk : N_ELEMS;

  // Largest multiple of UNROLL within [start, end). Tail handled with the
  // original scalar loop so this works for any chunk size.
  const int unroll_end = start + ((end - start) / UNROLL) * UNROLL;

  for (int rep = 0; rep < REPEAT; rep++) {
    int i = start;

    if (i + UNROLL <= unroll_end) {
      // Prologue: issue UNROLL non-blocking loads. None depend on each other,
      // so all UNROLL misses go in flight before any is consumed.
      int next_v[UNROLL];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) next_v[u] = input[i + u];

      // Steady state: software-pipelined. While processing the current batch,
      // issue the next batch's loads. This decouples DRAM latency from the
      // compute chain, leaving NoC/issue throughput as the bottleneck.
      for (; i + 2 * UNROLL <= unroll_end; i += UNROLL) {
        int v[UNROLL];
        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) v[u] = next_v[u];

        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) next_v[u] = input[i + UNROLL + u];

        for (int j = 0; j < OPS_PER_ELEM; j++) {
          #pragma GCC unroll 16
          for (int u = 0; u < UNROLL; u++) chain(&v[u], j);
        }

        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) output[i + u] = v[u];
      }

      // Epilogue: drain the last prefetched batch.
      int v[UNROLL];
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) v[u] = next_v[u];
      for (int j = 0; j < OPS_PER_ELEM; j++) {
        #pragma GCC unroll 16
        for (int u = 0; u < UNROLL; u++) chain(&v[u], j);
      }
      #pragma GCC unroll 16
      for (int u = 0; u < UNROLL; u++) output[i + u] = v[u];
      i += UNROLL;
    }

    // Scalar tail.
    for (; i < end; i++) {
      int val = input[i];
      for (int j = 0; j < OPS_PER_ELEM; j++) chain(&val, j);
      output[i] = val;
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
