#!/usr/bin/env python3
"""
fix_slow_csv.py — post-process benchmark CSVs.

Works on fast- or slow-clock CSVs; detects which via the `speed` column
on each row.

For SLOW rows, two issues are corrected:
  (a) gcups in the raw CSV uses the test-name `repeat` instead of the
      scaled `slow_repeat` the kernel actually ran. (The kernel wallclock
      and achieved_bw/gops columns from main.cpp are already correct —
      they used the actual repeat at exec time.)
  (b) provide a "simulated 32x BW" projection: multiply gcups /
      achieved_bw_GB_s / achieved_gops_s by the clock factor, since
      slow-clock running is equivalent to a fast-clock machine with 32x
      more DRAM bandwidth.

For every row, a chipwide view multiplies throughput by N_PODS. All 8
pods share one DRAM connection, so per-pod numbers collected during a
concurrent execute understate the chip's aggregate throughput by ~8x for
BW-bound kernels; multiplying by N_PODS recovers the right whole-chip
number. For compute-bound kernels (e.g. SW) per-pod compute is already
independent, so x8 is just the straightforward total across pods.

Output files (next to the input):
  <base>_actual.csv        real numbers (slow rows have gcups fixed)
  <base>_sim32bw.csv       slow rows scaled x32 (fast rows untouched)
  <base>_chipwide.csv      actual values x N_PODS for all rows
  <base>_sim32bw_chipwide.csv   both transforms stacked

Usage:
  ./fix_slow_csv.py results/results_20260423_233526.csv           # fast
  ./fix_slow_csv.py results/results_20260424_072636.csv           # slow
  ./fix_slow_csv.py --n-pods 8 <csv>                              # override
"""
import argparse
import csv
import os

CLOCK_FACTOR = 32          # slow-mode clock divider
SLOW_REPEAT_DIVISOR = 20   # what run_experiments.sh uses
DEFAULT_N_PODS = 8         # BSG cluster pod count

THROUGHPUT_COLS = ("gcups", "achieved_bw_GB_s", "achieved_gops_s")


def parse_int(s):
    try:
        return int(s)
    except (TypeError, ValueError):
        return None


def parse_float(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def actual_repeat(name_repeat):
    if name_repeat is None or name_repeat <= 1:
        return name_repeat
    return max(1, name_repeat // SLOW_REPEAT_DIVISOR)


def fix_row(row, simulate_32x, chipwide, n_pods):
    speed = row.get("speed", "")
    t = parse_float(row.get("kernel_time_sec"))
    name_repeat = parse_int(row.get("repeat"))
    is_slow = (speed == "slow")

    # (a) Repair slow gcups using the real repeat the kernel ran.
    if is_slow:
        rep = actual_repeat(name_repeat)
        if rep is not None:
            row["repeat"] = str(rep)
        ns = parse_int(row.get("num_seq"))
        sl = parse_int(row.get("seq_len"))
        if t and ns and sl and rep:
            row["gcups"] = f"{ns * rep * sl * sl / t / 1e9:.4f}"

    # (b) slow x 32 projection.
    if is_slow and simulate_32x:
        for col in THROUGHPUT_COLS:
            v = parse_float(row.get(col))
            if v is not None:
                row[col] = f"{v * CLOCK_FACTOR:.4f}"
        row["speed"] = "slow_sim32x"

    # (c) chipwide: multiply throughput by pod count.
    if chipwide and n_pods != 1:
        for col in THROUGHPUT_COLS:
            v = parse_float(row.get(col))
            if v is not None:
                row[col] = f"{v * n_pods:.4f}"

    return row


def transform(inp_path, out_path, simulate_32x, chipwide, n_pods):
    with open(inp_path, newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        rows = [fix_row(dict(r), simulate_32x, chipwide, n_pods) for r in reader]
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in rows:
            writer.writerow(r)
    print(f"wrote {out_path} ({len(rows)} rows)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--n-pods", type=int, default=DEFAULT_N_PODS,
                    help=f"pod count for chipwide view (default {DEFAULT_N_PODS})")
    args = ap.parse_args()

    base, ext = os.path.splitext(args.csv)
    n = args.n_pods

    transform(args.csv, f"{base}_actual{ext}",
              simulate_32x=False, chipwide=False, n_pods=n)
    transform(args.csv, f"{base}_sim32bw{ext}",
              simulate_32x=True,  chipwide=False, n_pods=n)
    transform(args.csv, f"{base}_chipwide{ext}",
              simulate_32x=False, chipwide=True,  n_pods=n)
    transform(args.csv, f"{base}_sim32bw_chipwide{ext}",
              simulate_32x=True,  chipwide=True,  n_pods=n)


if __name__ == "__main__":
    main()
