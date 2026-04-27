# Experiments — launch plan

Not every experiment runs at both fast and slow clock.  Fast is the
canonical sweep across every experiment; slow is a chosen subset that
exposes the memory-vs-compute split (~32× compute slowdown vs ~5.6×
memory slowdown — see Diagnostics).

Each subsection below is intentionally blank — fill in together as we
finalize the sweep grids.

## Fast clock

### 1. sw/1d — CPG sweep

_TODO: rows, target wall time, command_

### 2. sw/2d — seq_len sweep

_TODO: rows (incl. boundary-only ceiling), target wall time, command_

### 3. nw/ — seq_len sweep

Apps: `nw/baseline`, `nw/naive`, `nw/efficient`.

_TODO: rows per app, calibration plan (currently repeat=1 placeholders), command_

### 4. radix_sort — SIZE sweep

_TODO: rows (NUM_ARR×SIZE = 1 GB/buffer policy), HBM headroom check, command_

### 5. dummy/roofline — OPS sweep

_TODO: OPS_PER_ELEM × UNROLL grid, command_

### 6. dummy/barrier_bench — barrier comparison

_TODO: default vs linear × N grid, command_

## Slow clock (subset)

`run_experiments.sh:172-178` divides `repeat /= 20` in slow mode
unconditionally today.  That's correct for compute-bound rows, wrong
for memory-bound — slow has constant real-time per DRAM transaction,
so dividing collapses memory-bound rows into the noise floor.  Per-row
policy in the run script is still TODO (see TODO.md).

_TODO: which subset of fast experiments lift to slow, which rows
within each, and per-row repeat policy._

## Diagnostics (already done — for reference)

- `dummy/vvadd` — confirmed ~5.6× memory BW ratio fast/slow on
  2026-04-27.  Per-pod fast BW ≈ 0.76 GB/s; slow ≈ 0.14 GB/s.  HBM
  peak is hundreds of GB/s, so the ceiling is per-core load-issue
  rate (NoC injection × MSHR), not DRAM peak.  No software fix.
- `dummy/dram_read` — early read-only probe; loop got DCE'd by the
  optimizer (non-volatile pointer + repeated XOR over even REPEAT
  cancels).  Superseded by `dummy/vvadd`.
