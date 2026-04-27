# Experiments — launch plan

Not every experiment runs at both fast and slow clock.  Fast is the
canonical sweep across every experiment; slow is a chosen subset that
exposes the memory-vs-compute split (~32× compute slowdown vs ~5.6×
memory slowdown — see Diagnostics).

Each experiment has a registered name; launch via:
```
./run_experiments.sh <experiment-name>
```
The script enforces the name and auto-runs `cool_down` for slow
experiments.

## Fast clock

### 1. sw/1d — CPG sweep — `sw1d_cpg_fast`

Power-of-2 seq_len, min = `max(32, 4 × CPG)`, max = `256 × CPG`
(DMEM cap: REF_CORE = seq_len / CPG ≤ 256).  Target 20 s each.

- CPG=1: seq_len ∈ {32, 64, 128, 256} — **4 runs**
- CPG=2: seq_len ∈ {32, 64, 128, 256, 512} — **5 runs**
- CPG=4: seq_len ∈ {32, 64, 128, 256, 512, 1024} — **6 runs**
- CPG=8: seq_len ∈ {32, 64, 128, 256, 512, 1024, 2048} — **7 runs**
- CPG=16: seq_len ∈ {64, 128, 256, 512, 1024, 2048, 4096} — **7 runs**
- CPG=32: seq_len ∈ {128, 256, 512, 1024, 2048, 4096, 8192} — **7 runs**
- CPG=64: seq_len ∈ {256, 512, 1024, 2048, 4096, 8192, 16384} — **7 runs**
- CPG=128: seq_len ∈ {512, 1024, 2048, 4096, 8192, 16384, 32768} — **7 runs**

**Subtotal: 50 runs**

### 2. sw/2d — seq_len sweep — `sw2d_seqlen_fast`

Boundary-only DP, verified ceiling = 1024.  Power-of-2 seq_len.
Target 20 s each.

- seq_len ∈ {32, 64, 128, 256, 512, 1024} — **6 runs**

**Subtotal: 6 runs**

### 3. nw/ — seq_len sweep — `nw_seqlen_fast`

Three apps: `nw/baseline`, `nw/naive`, `nw/efficient`.  DMEM caps
seq_len ≤ 256 for nw/efficient (`boundary_scores[seq_len+1]` fits at
256).  Power-of-2 seq_len.  Target 20 s each.

- nw/baseline:  seq_len ∈ {32, 64, 128, 256} — **4 runs**
- nw/naive:     seq_len ∈ {32, 64, 128, 256} — **4 runs**
- nw/efficient: seq_len ∈ {32, 64, 128, 256} — **4 runs**

**Subtotal: 12 runs**

### 4. radix_sort — SIZE sweep — `radix_sort_fast`

Power-of-2 SIZE from 2 K (kernel min) to 256 M ints.  NUM_ARR pinned
to 1 GB/buffer (NUM_ARR × SIZE = 256 M ints).  Target 20 s each.

- SIZE ∈ {2 K, 4 K, 8 K, 16 K, 32 K, 64 K, 128 K, 256 K, 512 K, 1 M,
  2 M, 4 M, 8 M, 16 M, 32 M, 64 M, 128 M, 256 M} — **18 runs**

**Subtotal: 18 runs**

### 5. dummy/roofline — OPS sweep — `roofline_fast`

OPS_PER_ELEM √2-spaced from 1 to 16384, plus an UNROLL sweep at
ops=1.  All at N_ELEMS=4 M ints.  Target 20 s each.

- ops ∈ {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256,
  384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288,
  16384} — **28 runs**
- UNROLL sweep at ops=1: UNROLL ∈ {1, 4, 8, 16} — **4 runs**

**Subtotal: 32 runs**

### 6. dummy/barrier_bench — barrier comparison — `barrier_fast`

One run per barrier; tune N so each lands at ~20 s, then compute
time-per-barrier = wall_time / N.

- barrier ∈ {default, linear} — **2 runs**

**Subtotal: 2 runs**

---

## Fast-clock total: **120 runs**

(50 sw/1d CPG + 6 sw/2d + 12 nw/ + 18 radix_sort + 32 roofline + 2 barrier_bench)

## Slow clock (subset)

Slow clock is ~32× slower for compute, ~5.6× slower for memory.
For each kept row, divide `repeat` by an amount that lands the slow
wall time in the same ballpark as fast.

### 1. sw/1d — CPG sweep (largest seq_len per CPG) — `sw1d_cpg_slow`

Compute-bound, so `repeat /= 16`.

- CPG=1, seq_len=256
- CPG=2, seq_len=512
- CPG=4, seq_len=1024
- CPG=8, seq_len=2048
- CPG=16, seq_len=4096
- CPG=32, seq_len=8192
- CPG=64, seq_len=16384
- CPG=128, seq_len=32768

**Subtotal: 8 runs**

### 2. sw/2d — full seq_len sweep — `sw2d_seqlen_slow`

Compute-bound, `repeat /= 16`.

- seq_len ∈ {32, 64, 128, 256, 512, 1024} — **6 runs**

**Subtotal: 6 runs**

### 4. radix_sort — full sweep — `radix_sort_slow`

- SIZE < 65 K (compute-bound, vcache regime): `NUM_ARR /= 16`.
- SIZE ≥ 65 K (DRAM-bound, post-cliff):       `NUM_ARR /= 2`.

All 18 SIZE rows kept.

**Subtotal: 18 runs**

### 5. dummy/roofline — full OPS sweep — `roofline_slow`

Same 32-row grid as fast (28 ops × 4 UNROLL).  Compute-bound rows
benefit from `repeat /= 16`; the run script's existing slow path
already divides everything in the OPS sweep, so no per-row policy
needed here.

**Subtotal: 32 runs**

### 3 + 6. NOT in slow clock

- `nw/{baseline, naive, efficient}` — skipped.
- `dummy/barrier_bench` — skipped.

---

## Slow-clock total: **64 runs**

(8 sw/1d + 6 sw/2d + 18 radix_sort + 32 roofline)

## Diagnostics (already done — for reference)

- `dummy/vvadd` — confirmed ~5.6× memory BW ratio fast/slow on
  2026-04-27.  Per-pod fast BW ≈ 0.76 GB/s; slow ≈ 0.14 GB/s.  HBM
  peak is hundreds of GB/s, so the ceiling is per-core load-issue
  rate (NoC injection × MSHR), not DRAM peak.  No software fix.
- `dummy/dram_read` — early read-only probe; loop got DCE'd by the
  optimizer (non-volatile pointer + repeated XOR over even REPEAT
  cancels).  Superseded by `dummy/vvadd`.
