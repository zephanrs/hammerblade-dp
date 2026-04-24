#!/usr/bin/env python3
"""
fix_slow_csv.py — post-process the raw slow-clock CSV.

The live run script has a known bug: it records `gcups` using the `repeat`
embedded in the test name instead of the scaled slow_repeat the kernel
actually ran. The kernel's wallclock and achieved_* columns are correct,
only the derived gcups needs fixing.

This script produces TWO CSVs next to the input:
  <base>_actual.csv        gcups recomputed with actual_repeat (real slow)
  <base>_sim32bw.csv       actual values scaled x32 (simulated 32x BW)

Slow-mode logic on the runner:
  actual_repeat = max(1, name_repeat // 20)

Simulated-32x-BW interpretation:
  Slow clock runs compute at 1/32 speed while DRAM BW stays the same
  (wall-clock bytes/sec), which is equivalent to a fast-clock machine
  with 32x MORE BW. To project onto that hypothetical machine, multiply
  every compute/throughput column by 32.

Usage:
  ./fix_slow_csv.py results/results_20260424_072636.csv
"""
import argparse
import csv
import os
import sys

CLOCK_FACTOR = 32  # slow-mode clock divider
SLOW_REPEAT_DIVISOR = 20  # what run_experiments.sh uses

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


def fix_row(row, simulate_32x):
    speed = row.get("speed", "")
    t = parse_float(row.get("kernel_time_sec"))
    name_repeat = parse_int(row.get("repeat"))

    # Only slow rows with a valid timing need fixing.
    is_slow = (speed == "slow")

    if is_slow:
        rep = actual_repeat(name_repeat)
        if rep is not None:
            row["repeat"] = str(rep)

        # Recompute gcups from scratch for sw/nw rows.
        ns = parse_int(row.get("num_seq"))
        sl = parse_int(row.get("seq_len"))
        if t and ns and sl and rep:
            row["gcups"] = f"{ns * rep * sl * sl / t / 1e9:.4f}"

    if is_slow and simulate_32x:
        # Scale compute/throughput columns by the clock factor.
        for col in THROUGHPUT_COLS:
            v = parse_float(row.get(col))
            if v is not None:
                row[col] = f"{v * CLOCK_FACTOR:.4f}"
        row["speed"] = "slow_sim32x"

    return row


def transform(inp_path, out_path, simulate_32x):
    with open(inp_path, newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        rows = [fix_row(dict(r), simulate_32x) for r in reader]
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in rows:
            writer.writerow(r)
    print(f"wrote {out_path} ({len(rows)} rows)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    args = ap.parse_args()

    base, ext = os.path.splitext(args.csv)
    transform(args.csv, f"{base}_actual{ext}",   simulate_32x=False)
    transform(args.csv, f"{base}_sim32bw{ext}",  simulate_32x=True)


if __name__ == "__main__":
    main()
