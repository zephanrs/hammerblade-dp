# Experiments — launch plan

Every experiment we will run, fast clock and slow clock.  See `TODO.md`
for open work; this file is the concrete run plan for the launch.

Conventions:
- **Ready** — `tests.mk` is calibrated for ~20 s fast rows; just run it.
- **Calibrate** — `tests.mk` is in `repeat=1` placeholder mode; do one
  fast pass, send me `kernel_us` per row, I set repeats targeting ~20 s,
  re-run.
- **Blocked** — needs an upstream code change before running.

Slow-clock policy: compute-bound rows shrink `repeat /= 20` to fit the
600 s per-test timeout (`run_experiments.sh:172-178` does this today
unconditionally).  Memory-bound rows must NOT shrink — slow clock has
constant real-time per DRAM transaction, so dividing collapses the test
into the noise floor.  Per-row policy still needs to land in the run
script (see TODO.md "Slow-clock policy").

## Fast-clock runs

| # | Experiment | Apps | Sweep | Rows | State | Wall (est) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | **Roofline** (HW BW + GFLOP curve) | `dummy/roofline` | OPS_PER_ELEM × UNROLL | 32 | Ready | ~10 min |
| 2 | **sw/1d vs sw/2d** (1D vs 2D systolic SW) | `sw/1d`, `sw/2d` | seq_len; sw/2d adds shared-vs-unique pair | sw/1d ~30, sw/2d 13 (incl. 5 boundary-only smoke rows) | sw/1d Ready, sw/2d Ready for smoke + Calibrate the seq_len ∈ {256, 512, 1024, 1536, 2048} rows after smoke passes | ~15 min |
| 3 | **sw/1d CPG sweep** | `sw/1d` | CORES_PER_GROUP ∈ {1, 8, 16, 128} × seq_len | (subset of sw/1d/tests.mk) | Ready | included in #2 |
| 4 | **nw/{baseline, naive, efficient}** | `nw/baseline`, `nw/naive`, `nw/efficient` | seq_len × num_seq | 12 each | Calibrate (repeat=1 placeholders) | ~3 min calibration pass, ~60 min final |
| 5 | **Barriers** (default vs linear) | `dummy/barrier_bench` | barrier ∈ {default, linear} × N ∈ {1K, 10K, 100K, 1M} | 8 | Ready | ~2 min |
| 6 | **Radix sort** | `radix_sort` | SIZE ∈ 2 K…256 M ints, NUM_ARR pinned to 1 GB/buffer | 18 | Ready (NUM_ARR may bump if HBM has headroom) | ~5 min |

**Total fast wall time after calibration:** ~95 min, of which ~30 min
is `Ready`-now and the rest is gated on the nw/* repeat=1 calibration
+ sw/2d boundary-only smoke results.

## Slow-clock runs

| # | Experiment | Apps | Rows in slow | Repeat policy | State |
| --- | --- | --- | --- | --- | --- |
| 1 | **Roofline @ slow** | `dummy/roofline` | all 32 | unchanged (current `/20` is fine; both knees of the curve are visible at slow) | Ready |
| 2 | **Radix sort @ slow** | `radix_sort` | all 18 | SIZE < 65 K: `/ 20`.  SIZE ≥ 65 K: **unchanged** | Blocked on per-row policy in run script |
| 3 | **nw/efficient @ slow** | `nw/efficient` | all | **unchanged** (memory-bound: per-iter DRAM writes) | Blocked on per-row policy + nw repeat calibration |
| 4 | **nw/naive @ slow** | `nw/naive` | all | **unchanged** (memory-bound: full DP matrix to DRAM) | Blocked on per-row policy + nw repeat calibration |
| 5 | **sw/2d @ slow** (seq_len sweep only) | `sw/2d` | 9 rows: `seq-len_{32,64,128,192,256,512,1024,1536,2048}__num-seq_*__repeat_*`.  **Skip** the shared-vs-unique A/B pair rows. | `/ 20` (compute-bound) | Blocked on per-row "skip in slow" policy + sw/2d larger-seq_len calibration |

**Apps NOT in the slow-clock run:**
- `sw/1d` — compute-bound, sweep is comprehensive at fast; 32× slowdown adds nothing new.
- `nw/baseline` — compute-bound, redundant given nw/efficient + nw/naive characterize the memory side.
- `dummy/barrier_bench` — per-barrier latency at slow is informative but tangential; defer.
- `dummy/vvadd`, `dummy/dram_read` — diagnostic probes, already done at slow.

## Run commands

All commands assume working dir
`/home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs`.
Always `git pull` first.  Between fast and slow runs, the cluster guide
requires:
```bash
cd /cluster_src/reset_half && make cool_down UNIT_ID=2 && cd -
```

### Fast (in order)

```bash
# 1. Roofline
./run_experiments.sh dummy/roofline

# 2 + 3. sw/1d (multi-axis incl. CPG sweep) + sw/2d
./run_experiments.sh sw/1d sw/2d

# 4a. nw/* — calibration pass at repeat=1
./run_experiments.sh nw/baseline nw/naive nw/efficient
# Send me the kernel_us per row → I set repeats → push
git pull
./run_experiments.sh nw/baseline nw/naive nw/efficient   # 4b. final

# 5. Barriers
./run_experiments.sh dummy/barrier_bench

# 6. Radix sort
./run_experiments.sh radix_sort
```

### Slow (after `cool_down`, after per-row policy lands)

```bash
SLOW_MODE=1 ./run_experiments.sh dummy/roofline
SLOW_MODE=1 ./run_experiments.sh radix_sort
SLOW_MODE=1 ./run_experiments.sh nw/efficient nw/naive
SLOW_MODE=1 ./run_experiments.sh sw/2d   # skips A/B-pair rows automatically once policy lands
```

## Diagnostic probes (already done — for reference)

- `dummy/vvadd` — confirmed ~5.6× memory BW ratio fast/slow on
  2026-04-27.  Per-pod fast BW ≈ 0.76 GB/s; slow ≈ 0.14 GB/s.  HBM
  peak is hundreds of GB/s, so the ceiling is per-core load-issue
  rate (NoC injection × MSHR), not DRAM peak.  No software fix.
- `dummy/dram_read` — early read-only probe; loop got DCE'd by the
  optimizer (non-volatile pointer + repeated XOR over even REPEAT
  cancels).  Superseded by vvadd; recoverable by switching to
  `volatile int *` if a pure-read data point becomes useful again.

## Order of operations

1. **Fast roofline + sw/1d + sw/2d + radix_sort + barrier_bench**
   (~30 min, all `Ready`).
2. **Fast nw/{baseline, naive, efficient} repeat=1 calibration sweep**
   (~3 min).  Send me `kernel_us` per row.
3. **I set repeats in nw/*/tests.mk + sw/2d larger-seq_len rows**.  Push.
4. **Fast nw/{baseline, naive, efficient} full sweep + sw/2d full**
   (~60 min).
5. **Per-row slow policy lands in `run_experiments.sh`**.  Push.
6. **Slow runs** (~3 hours total — most rows are at unchanged repeat
   so wall time ≈ fast × 5.6× for memory-bound, fast × 1× for
   compute-bound after `/20`).
