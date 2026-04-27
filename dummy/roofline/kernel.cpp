// dummy/roofline — systolic column-pipeline kernel.
//
// Why this rewrite:
//   The previous kernel had every tile do its own DRAM loads, compute, and
//   stores.  Each tile could only keep ~UNROLL loads in flight, capped by
//   register pressure and per-core issue rate.  When the cores were
//   slowed down (cool_down), in-flight load count dropped with frequency
//   and DRAM bandwidth dropped 5–6× — proving the kernel was core-bound,
//   not memory-bound.  A real memory-bound roofline benchmark must show
//   the same DRAM bandwidth at fast and slow clocks.
//
// Layout:
//   Each column of 8 tiles forms a vertical pipeline:
//     y=0       — TOP    : load input[] from DRAM (BATCH non-blocking
//                          loads back-to-back), forward to y=1.
//                          Top row is dedicated to memory issue, so the
//                          sum across 16 top tiles is BATCH * 16 = 256
//                          loads in flight per pod, independent of core
//                          frequency.
//     y=1..6    — MIDDLE : wait for upstream's batch, run OPS_PER_ELEM
//                          mul-add iterations on each value, forward to
//                          y+1.
//     y=7       — BOTTOM : wait for upstream, OPS_PER_ELEM mul-add,
//                          write to output[] in DRAM.
//
// Mailbox:
//   Each tile owns a tile-local DMEM mailbox:
//     volatile int data_in[2*BATCH];   // double-buffered: phase 0 / 1
//     volatile int ack_in;             // remotely incremented by tile below
//   Producer (the upstream tile) writes BATCH ints into one phase, then
//   moves on to the other phase next batch.  Consumer reads its own
//   data_in slots; it knows the batch is fully arrived when slot
//   [BATCH-1] of the active phase observably differs from the value it
//   saw there last time at that phase (NoC same-source FIFO ordering
//   guarantees slots 0..BATCH-2 are already updated by the time
//   slot BATCH-1 becomes visible).  No extra version word needed:
//   host fills input[i]=i, every per-stage chain (mul 31 + add j) is
//   injective mod 2^32, so successive batches' values at any given slot
//   are guaranteed to differ.
//
// Backpressure:
//   Producer can be at most PHASES=2 batches ahead.  Consumer increments
//   ack_in (remotely, in producer's DMEM) after each batch read.
//   Producer waits ack_in >= b-PHASES+1 before overwriting.  This stops
//   producer from racing the consumer's read of the alternate phase.
//
// Wakeups (bsg_lr / bsg_lr_aq):
//   wait_changed: load-reserve on slot[BATCH-1]; if value != prev, done;
//                 else load-reserve-acquire (stalls the core until the
//                 cache line is invalidated by an incoming write).
//   wait_ge:      same idiom, but the predicate is value >= target
//                 (used for the ack counter).
//
// Vcache flush between repeats:
//   Each pass streams col_chunk * 4 bytes per column = N_ELEMS*4/16 bytes
//   per top tile.  At N_ELEMS=4M ints, that's 1 MB per top tile per pass
//   — far above the 128 KB per-pod vcache, so when REPEAT > 1 each
//   re-read of input[i] sees a cold vcache.

#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include <climits>

#ifndef OPS_PER_ELEM
#define OPS_PER_ELEM 1
#endif

#ifndef N_ELEMS
#define N_ELEMS 1048576
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

// BATCH non-blocking loads issued back-to-back per top-row iteration.
// Larger BATCH = more memory-level parallelism per top tile, capped by
// usable registers (~22 free in RV32I) and DMEM mailbox footprint.
#define BATCH    16
#define PHASES   2
#define SLOTS    (PHASES * BATCH)
// Cast away the int↔unsigned narrowing of 0x80000000 so brace-init compiles
// with -Wnarrowing.  The bit pattern is still INT_MIN.
#define SENTINEL ((int)0x80000000)

// Tile-local DMEM mailbox.  Each tile has one of these; the upstream
// neighbour writes to ours, the downstream neighbour increments ours.
volatile int data_in[SLOTS];
volatile int ack_in;

static inline __attribute__((always_inline))
void wait_changed(volatile int *addr, int prev) {
  while (1) {
    int v = bsg_lr((int *)addr);
    if (v != prev) break;
    bsg_lr_aq((int *)addr);
  }
}

static inline __attribute__((always_inline))
void wait_ge(volatile int *addr, int target) {
  while (1) {
    int v = bsg_lr((int *)addr);
    if (v >= target) break;
    bsg_lr_aq((int *)addr);
  }
}

static inline __attribute__((always_inline))
void chain(int *v, int j) {
  asm volatile(
    "mul %[v], %[v], %[c]\n"
    "add %[v], %[v], %[j]\n"
    : [v] "+r"(*v) : [c] "r"(31), [j] "r"(j)
  );
}

extern "C" int kernel(int *input, int *output, int /*pod_id*/) {
  bsg_barrier_tile_group_init();
  // Reset our mailbox before anyone can write to it.
  #pragma GCC unroll 16
  for (int i = 0; i < SLOTS; i++) data_in[i] = SENTINEL;
  ack_in = 0;
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int x = __bsg_x;
  const int y = __bsg_y;
  const int W = bsg_tiles_X;
  const int H = bsg_tiles_Y;

  const int col_chunk = N_ELEMS / W;
  const int col_base  = x * col_chunk;
  const int n_batches = col_chunk / BATCH;

  // Remote pointers — only the ones our role uses.
  volatile int *down_data = (y < H - 1)
      ? (volatile int *)bsg_remote_ptr(x, y + 1, (void *)data_in)
      : nullptr;
  volatile int *up_ack = (y > 0)
      ? (volatile int *)bsg_remote_ptr(x, y - 1, (void *)&ack_in)
      : nullptr;

  if (y == 0) {
    // ── TOP : DRAM → mailbox ────────────────────────────────────────
    int b = 0;
    for (int rep = 0; rep < REPEAT; rep++) {
      for (int bb = 0; bb < n_batches; bb++, b++) {
        const int phase = b & 1;
        const int slot_base = phase * BATCH;
        const int dram_off  = col_base + bb * BATCH;

        // Hold for downstream to free this phase.
        if (b >= PHASES) wait_ge(&ack_in, b - PHASES + 1);

        // BATCH non-blocking loads.  Compiler emits BATCH lw insns
        // back-to-back; non-blocking writeback keeps them all in
        // flight until the corresponding store consumes the register.
        int v[BATCH];
        #pragma GCC unroll 16
        for (int u = 0; u < BATCH; u++) v[u] = input[dram_off + u];

        // Forward to downstream's mailbox phase.
        #pragma GCC unroll 16
        for (int u = 0; u < BATCH; u++) down_data[slot_base + u] = v[u];
      }
    }
  } else {
    // ── MIDDLE / BOTTOM : mailbox → work → (mailbox | DRAM) ─────────
    const bool is_bottom = (y == H - 1);
    int prev_K[PHASES] = { SENTINEL, SENTINEL };
    int b = 0;
    for (int rep = 0; rep < REPEAT; rep++) {
      for (int bb = 0; bb < n_batches; bb++, b++) {
        const int phase = b & 1;
        const int slot_base = phase * BATCH;
        const int dram_off  = col_base + bb * BATCH;

        // Wait for upstream's batch.  Only the LAST slot needs polling
        // — NoC same-source-to-same-dest FIFO ordering guarantees the
        // earlier slots are already in our DMEM by the time the last
        // one is observable.
        wait_changed(&data_in[slot_base + BATCH - 1], prev_K[phase]);

        int v[BATCH];
        #pragma GCC unroll 16
        for (int u = 0; u < BATCH; u++) v[u] = data_in[slot_base + u];
        prev_K[phase] = v[BATCH - 1];

        // OPS_PER_ELEM mul-add iterations per element.  This is the
        // "compute knob" that traces the roofline.  At OPS=1 we are
        // memory-bound (top row's DRAM rate dominates); as OPS grows
        // these middle/bottom stages become the bottleneck and the
        // pipeline backpressures the top row through ack_in chains.
        for (int j = 0; j < OPS_PER_ELEM; j++) {
          #pragma GCC unroll 16
          for (int u = 0; u < BATCH; u++) chain(&v[u], j);
        }

        // Tell upstream we've consumed this batch (loads have retired
        // since their values fed the chain above).  bsg_fence() drains
        // local mem ops before the remote store goes onto the NoC.
        bsg_fence();
        *up_ack = b + 1;

        if (is_bottom) {
          #pragma GCC unroll 16
          for (int u = 0; u < BATCH; u++) output[dram_off + u] = v[u];
        } else {
          if (b >= PHASES) wait_ge(&ack_in, b - PHASES + 1);
          #pragma GCC unroll 16
          for (int u = 0; u < BATCH; u++) down_data[slot_base + u] = v[u];
        }
      }
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
