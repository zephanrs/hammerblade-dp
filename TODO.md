# Launch plan & open work

## Find repeat counts for every experiment row

Every row of every `tests.mk` needs `repeat` (or `num-arr` for
radix_sort, `N_BARRIERS` for barrier_bench) calibrated so that fast
wall time lands at ~20 s.  Slow scaling is then handled automatically
by `run_experiments.sh` per the per-experiment policy.

| Experiment | Source for repeat values |
| --- | --- |
| `sw1d_cpg_fast`     | Use the formula `repeat = round_pow2(70000 / seq_len)` from sw/1d's existing tests.mk header.  num_seq = 1M / seq_len.  Fill in 50 rows. |
| `sw2d_seqlen_fast`  | Same `cells = num_seq × repeat × seq_len² ≈ 70 G` formula.  Existing rows for seq_len ≤ 192 use repeat=512; new seq_len ∈ {256, 512, 1024} need repeats {256, 128, 64}. |
| `nw_seqlen_fast`    | **Needs HW data.**  Run repeat=1 sweep, send `kernel_us` per row, set `repeat = round(20 / s)`.  **CAUTION nw/efficient:** the hardware cliff hangs the kernel when `iters/col = num_seq * repeat / 16` is an integer multiple of 32 (= `num_seq × repeat` multiple of 512).  Pick repeat so the product `num_seq × repeat` is a multiple of 16 but NOT a multiple of 512.  Same constraint applies to `repeat=1` calibration: every num_seq used there must already satisfy this rule, but bumping repeat can land back on a multiple of 512 even when num_seq alone was safe. |
| `radix_sort_fast`   | NUM_ARR already calibrated targeting 1 GB/buffer; verify HBM fits on first run. |
| `roofline_fast`     | Existing tests.mk has 32 calibrated rows (header table). |
| `barrier_fast`      | Tune N for ~20 s.  Estimate: default barrier ≈ 1–2 µs ⇒ N ≈ 10 M; linear ≈ 5–10 µs ⇒ N ≈ 2 M.  Verify on first run, retune. |


## Canonical experiments (the launch suite)

Each experiment needs a calibrated `tests.mk` (rows ~20 s fast, slow
auto-scaled to ~/20 by the run script), one fast-clock run, one
slow-clock run.  `run_experiments.sh:172-178` divides `repeat` by 20
in slow mode, so per-row work is constant at 20× and slow wall time
stays under the 600 s timeout.

| # | Experiment | Apps | Status |
| --- | --- | --- | --- |
| 1 | **Roofline** (HW BW + GFLOP characterization) | `dummy/roofline`         | Original prefetch+SWP kernel restored.  Re-run fast + slow to regenerate the curve. |
| 2 | **sw/1d vs sw/2d**                            | `sw/1d`, `sw/2d`         | sw/1d ✓ calibrated.  sw/2d in progress (boundary-only rewrite — see below).  Re-run experiment 2 after sw/2d lands. |
| 3 | **sw/1d CPG sweep**                           | `sw/1d` (CPG ∈ {1, 8, 16, 128}) | tests.mk already has the sweep.  Needs fast + slow run. |
| 4 | **nw/{baseline, naive, efficient} comparison**| `nw/baseline`, `nw/naive`, `nw/efficient` | All three carry a `repeat=1` calibration sweep.  Run, send `kernel_us` per row, set `repeat = round(20 / s)`. |
| 5 | **Barriers (default vs linear)**              | `dummy/barrier_bench`    | App ready, two variants × four `N`.  Run fast + slow.  Optional follow-up: rerun radix_sort with `barrier=linear` once the barrier comparison numbers are in hand. |
| 6 | **Radix sort**                                | `radix_sort`             | NUM_ARR×SIZE pinned at 256 M ints (~1 GB/buffer).  Run fast first; if HBM has headroom, double pre-cliff rows to land them at ~20 s instead of ~3 s. |

## In progress

### sw/2d — boundary-only DP storage

Today each tile holds its full `QRY_CORE × REF_CORE` DP submatrix
double-buffered, which puts seq_len ≤ 192 against the 4 KB DMEM
budget.  We only need the rightmost column + bottommost row of each
submatrix to send to the east / south neighbors; the interior is
write-only-throwaway.  Switching to a boundary-only buffer:

- per-buffer DMEM: `(QRY_CORE + REF_CORE) × 4` ints + small wavefront
- raises the seq_len ceiling from 192 to ~1024+ (exact bound depends
  on the wavefront / mailbox state we keep)
- enables a fairer apples-to-apples comparison vs sw/1d at the same
  large seq_len and a meaningful sw/2d row in experiment 2

Implement, validate on real hardware (the existing seq-len sweep +
the shared-vs-unique A/B pair must still pass), then update the
seq_len ladder in `sw/2d/tests.mk` to the new ceiling and re-run
experiment 2.

## Calibration-needing apps

Already covered in the experiments table above; the action is the
same — fast-clock kernel_us per row → set `repeat`.

| App | Status | Action |
| --- | --- | --- |
| `nw/baseline`         | `repeat=1` sweep checked in | Fast run, send `kernel_us` per row |
| `nw/naive`            | `repeat=1` sweep checked in | same |
| `nw/efficient`        | `repeat=1` sweep checked in | same |
| `dummy/barrier_bench` | 8 rows: {default, linear} × N ∈ {1K, 10K, 100K, 1M} | Fast run; verify per-barrier latency is constant in N (sanity), then compare default vs linear |
| `radix_sort`          | 18-row sweep at 1 GB/buffer | Fast run; confirm HBM fits.  If headroom, double pre-cliff rows |

## Resolved findings

### Hardware memory ceiling = 5.6× fast/slow ratio (not 1×, not 32×)

Confirmed by both `dummy/vvadd` (canonical memory-bound) and
`dummy/roofline` at low AI.  Per-pod fast BW ≈ 0.76 GB/s, slow ≈
0.14 GB/s — same ratio in both kernels.  Real HBM peak is hundreds
of GB/s, so we're nowhere near DRAM peak; the ceiling is per-core
load-issue rate (NoC injection × vcache MSHR depth) which itself
scales with core clock.  No software fix.  The 5.6× ratio at
memory-bound is the result we report.

### Hardware cliff: nw/efficient hangs when num_seq is a multiple of 512

Per-iteration DRAM writes indexed by `seq_id` hang when iterations
per column (= num_seq / bsg_tiles_X = num_seq / 16) is an integer
multiple of 32.  `nw/baseline` not affected (only writes one int per
sequence).  Workaround everywhere: pick num_seq that is a multiple
of 16 but NOT a multiple of 512.  Working hypothesis: a 32-deep
on-chip resource (MSHR / wormhole-router buffer / cache request
table) synchronizes across 16 vcaches when path-write traffic
recurs at exactly the same hash positions.  The 4-way 64-set vcache
with IPOLY hashing rules out plain capacity thrashing.

### Slow-clock policy

Compute-bound rows must shrink `repeat` to fit the 600 s timeout in
slow mode.  Memory-bound rows must NOT shrink — slow clock has a
constant-real-time per memory transaction, so dividing repeat collapses
the test into the noise floor.

Slow-mode launch list (everything else: skip in slow mode):

| App | Rows in slow mode | Repeat in slow |
| --- | --- | --- |
| `radix_sort`     | all 18 rows | SIZE < 65536: `repeat / 20` (compute-bound).  SIZE ≥ 65536: unchanged (memory-bound after vcache cliff). |
| `nw/efficient`   | all rows    | unchanged (memory-bound: per-iter DRAM writes dominate). |
| `nw/naive`       | all rows    | unchanged (memory-bound: full DP matrix to DRAM per sequence). |
| `sw/2d`          | seq_len sweep only (4 rows) | `repeat / 20` (compute-bound).  Skip the shared-vs-unique A/B rows in slow. |

`run_experiments.sh:172-178` currently divides `repeat /= 20`
unconditionally in slow mode.  TODO: extend so that (a) only the apps
above run in slow at all, and (b) the divide only fires when a row
isn't tagged memory-bound.  Implementation sketch: per-app Makefile
emits `slow = divide | nodivide | skip` to `parameters.mk`; the run
script reads it and either passes a smaller `repeat=` to make,
leaves `repeat` alone, or returns early.

## Diagnostic probes (used, not part of the launch)

- `dummy/vvadd` — confirmed the 5.6× memory ceiling on 2026-04-27.
  Keep around for HW-level sanity.
- `dummy/dram_read` — early read-only probe; loop got DCE'd
  (non-volatile pointer, repeated XOR cancels).  Superseded by
  `dummy/vvadd`.  If a pure-read data point becomes useful, mark the
  pointer `volatile int *` and rerun.
