# Open work — calibration & sweeps

Status of every app whose `tests.mk` is not yet ready for the full
fast + slow launch.  Slow clock is ~32× slower than fast for compute,
~5–6× slower for current DRAM-bound code; the framework's per-test
timeout is 600 s, so any test calibrated above ~18 s fast risks
timing out at slow clock.

## Calibration to ~20 s per fast-clock run

| App | Status | Next step |
| --- | --- | --- |
| `nw/baseline`         | `repeat=1` calibration sweep checked in (3 sizes × 4 seq_len = 12 rows) | Run on hardware, send `kernel_us` per row → set `repeat` to round(20 / s) |
| `nw/naive`            | `repeat=1` calibration sweep checked in                                | Same — run + report kernel_us, then tune |
| `nw/efficient`        | `repeat=1` calibration sweep checked in                                | Same — run + report kernel_us, then tune |
| `dummy/dram_read`     | 9 rows across 256 KB → 16 MB working sets, REPEAT roughed to ~2–5 s   | Run fast + slow on at least one row per working-set size; compare `bw_GB_s` ratios |
| `dummy/barrier_bench` | 8 rows: {default, linear} × N ∈ {1K, 10K, 100K, 1M}                    | Run fast at minimum; verify `per_barrier_ns` is constant in N (sanity), then set per-app barrier choice based on the comparison |
| `radix_sort`          | 18 rows targeting 1 GB/buffer (NUM_ARR × SIZE = 256 M ints)            | Run fast; confirm HBM fits.  If room to spare, double NUM_ARR for the pre-cliff (≤ 32 K ints) rows so they hit ~20 s instead of ~3 s |

## Slow-clock sweep strategy — TODO

Decide how to keep slow runs under the 600 s per-test timeout without
diverging from the fast-clock sweep:

- **Option A — split `tests.mk`**: a sibling `tests.slow.mk` with
  proportionally smaller `repeat` values per row, selected by the run
  script when `SLOW_MODE=1`.
- **Option B — per-row scaling factor**: `tests.mk` carries
  `repeat_fast` and `repeat_slow`; the makefile picks one based on a
  `mode=` parameter at generate time.
- **Option C — one knob, scale at runtime**: kernel reads a `repeat`
  argument from the host instead of compiling it in;
  `run_experiments.sh` divides it by ~32 in slow mode.  Simplest
  deployment-wise but loses the constant-work-per-row property in
  tests.mk comments.

Pick a policy and apply across all calibration-needing apps before
launching slow runs.  Compute-bound rows in `sw/*` (~20 s fast →
~640 s slow) all need the same treatment.

## `sw/*` experiments — confirm canonical sweep

The sw subapps already have calibrated repeats from earlier work; need
to verify what's still part of the launch and what needs slow-mode
adjustment per the policy above.

| Subapp | calibrated tests.mk | included in slow sweep? | notes |
| --- | --- | --- | --- |
| `sw/1d`         | ✓ multi-axis (CPG × seq_len × pod-unique-data) | TODO confirm | many rows; pick a slow subset if needed |
| `sw/2d`         | ✓ small sweep (4 seq_len + A/B unique-vs-shared)| TODO confirm | see "sw/2d update" below |
| `sw/1ddb`       | TODO check                                       | TODO confirm | |
| `sw/banded`     | TODO check                                       | TODO confirm | |
| `sw/dynamic`    | TODO check                                       | TODO confirm | |
| `sw/scheduling` | TODO check                                       | TODO confirm | |

## sw/2d update — TODO

User-flagged: there is a planned update for `sw/2d`.  Details still to
be captured.  When tackled, document the change here and re-run the
existing seq-len sweep + the shared-vs-unique A/B pair before
declaring it done.

## Hardware cliff (resolved as a constraint)

`nw/efficient` (and any kernel that does per-iteration DRAM writes
indexed by `seq_id`) hangs when **iterations per column** (= `num_seq
/ bsg_tiles_X` = `num_seq / 16`) is an integer multiple of **32**.
Equivalently: hangs when `num_seq` is an integer multiple of 512.

Confirmed across `seq_len ∈ {32, 64, 128}`. seq_len=32 fails at
iters/col ∈ {32, 64, 96}; seq_len=64 and 128 fail at iters/col=256
(= 8×32). Every non-multiple-of-32 iters/col passes regardless of
total scale (tested up to ~32k sequences per pod, ~2k iters/col).

`nw/baseline` does NOT appear affected — it only writes one int per
sequence to DRAM (no per-cell writes). Tested at `num_seq=32768` (=
mul of 512) → passed.

**Working hypothesis** for the mechanism: a 32-deep on-chip resource
(MSHR queue / wormhole router buffer / cache request table) that
synchronizes across the 16 vcaches per pod when path-write traffic
recurs at exactly the same hash positions. The 4-way 64-set vcache
with IPOLY hashing makes simple "cache thrashing" insufficient as an
explanation, since the cliff is at exact multiples (not "more than"),
which a capacity story doesn't fit.

**Workaround used everywhere**: pick `num_seq` that is a multiple of
16 (barrier requirement) but NOT a multiple of 512.
