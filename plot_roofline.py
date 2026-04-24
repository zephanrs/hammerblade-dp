#!/usr/bin/env python3
"""
plot_roofline.py — draw rooflines for fast and slow core speeds.

Usage:
    python3 plot_roofline.py results/results_<timestamp>.csv [--out roofline.png]

Roofline model
--------------
Performance (y-axis, GCUPS or GOPS) bounded by:
  1. Compute peak:   flat line at peak GOPS
  2. Memory BW peak: sloping line at  peak_bw_GB_s * AI  (GOPS = BW * AI)

Memory BW anchor (low-OI roofline points)
-----------------------------------------
The dummy/roofline kernel sweeps OPS_PER_ELEM from 1 to 1024.
  OI = 2 * ops_per_elem / 8  ops/byte

Low-OI points (ops=1,2,4) are memory-bound and directly measure peak DRAM BW:
    achieved_bw_GB_s  is printed by main.cpp and stored in the CSV.
We take the max achieved_bw_GB_s across all low-OI rows as the BW roof.

Compute peak anchor (high-OI roofline points)
----------------------------------------------
High-OI points (ops=256,512,1024) are compute-bound:
    achieved_gops_s  is printed by main.cpp and stored in the CSV.
We take the max achieved_gops_s as the compute roof.

SW data points
--------------
SW kernels are plotted as (AI, GCUPS) scatter.  Because SW is always
compute-bound (AI >> ridge point), they should sit on or below the
compute roof.
"""

import argparse
import csv
import sys
import re
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MPL = True
except ImportError:
    HAS_MPL = False


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("csv", help="results CSV from run_experiments.sh")
    p.add_argument("--out", default="roofline.png")
    p.add_argument("--peak-bw-fast",      type=float, default=None,
                   help="Override memory BW peak (GB/s) for fast speed")
    p.add_argument("--peak-bw-slow",      type=float, default=None,
                   help="Override memory BW peak (GB/s) for slow speed")
    p.add_argument("--peak-compute-fast", type=float, default=None,
                   help="Override compute peak (GOPS) for fast speed")
    p.add_argument("--peak-compute-slow", type=float, default=None,
                   help="Override compute peak (GOPS) for slow speed")
    p.add_argument("--low-oi-max-ops",    type=int, default=4,
                   help="Use roofline rows with ops_per_elem <= N as BW anchors (default 4)")
    p.add_argument("--high-oi-min-ops",   type=int, default=256,
                   help="Use roofline rows with ops_per_elem >= N as compute anchors (default 256)")
    return p.parse_args()


def read_csv(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    return rows


def safe_float(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def safe_int(v):
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def main():
    args = parse_args()
    rows = read_csv(args.csv)

    # ── Separate row types ────────────────────────────────────────────────────
    sw_rows = [r for r in rows
               if r.get("app", "").startswith("sw/")
               and safe_float(r.get("gcups"))
               and safe_float(r.get("arith_intensity_ops_per_byte"))]

    rl_rows = [r for r in rows
               if r.get("app") == "dummy/roofline"
               and safe_float(r.get("kernel_time_sec"))]

    if not rl_rows:
        print("WARNING: no dummy/roofline rows found in CSV.")
        print("         Run dummy/roofline tests first to get BW/compute anchors.")

    # ── BW peak: from low-OI roofline rows ───────────────────────────────────
    bw_by_speed = defaultdict(list)
    for r in rl_rows:
        ops = safe_int(r.get("ops_per_elem"))
        bw  = safe_float(r.get("achieved_bw_GB_s"))
        if ops is not None and ops <= args.low_oi_max_ops and bw:
            bw_by_speed[r.get("speed", "fast")].append(bw)

    peak_bw = {
        "fast": args.peak_bw_fast  or (max(bw_by_speed["fast"])  if bw_by_speed["fast"]  else 0),
        "slow": args.peak_bw_slow  or (max(bw_by_speed["slow"])  if bw_by_speed["slow"]  else 0),
    }

    # ── Compute peak: from high-OI roofline rows ──────────────────────────────
    gops_by_speed = defaultdict(list)
    for r in rl_rows:
        ops  = safe_int(r.get("ops_per_elem"))
        gops = safe_float(r.get("achieved_gops_s"))
        if ops is not None and ops >= args.high_oi_min_ops and gops:
            gops_by_speed[r.get("speed", "fast")].append(gops)

    peak_compute = {
        "fast": args.peak_compute_fast or (max(gops_by_speed["fast"])  if gops_by_speed["fast"]  else 0),
        "slow": args.peak_compute_slow or (max(gops_by_speed["slow"])  if gops_by_speed["slow"]  else 0),
    }

    # ── Summary table ─────────────────────────────────────────────────────────
    print(f"{'Speed':<6}  {'Compute peak':>16}  {'BW peak':>12}  {'Ridge point':>14}")
    print("-" * 54)
    for speed in ("fast", "slow"):
        pc = peak_compute.get(speed, 0)
        bw = peak_bw.get(speed, 0)
        ridge = (pc / bw) if bw > 0 else float("nan")
        print(f"{speed:<6}  {pc:>13.2f} GOPS  {bw:>8.2f} GB/s  {ridge:>11.1f} ops/B")
    print()

    print(f"{'App':<20} {'Seq':<6} {'CPG':<5} {'Speed':<6} {'GCUPS':>8} {'AI ops/B':>10}")
    print("-" * 60)
    for r in sorted(sw_rows, key=lambda x: (x.get("speed", ""), safe_float(x.get("arith_intensity_ops_per_byte")) or 0)):
        print(f"{r['app']:<20} {r.get('seq_len','?'):<6} {r.get('cpg','8'):<5} "
              f"{r.get('speed','?'):<6} {safe_float(r.get('gcups')) or 0:>8.3f} "
              f"{safe_float(r.get('arith_intensity_ops_per_byte')) or 0:>10.1f}")

    print()
    print("Roofline kernel measurements:")
    print(f"  {'ops_per_elem':<14} {'AI ops/B':>10} {'BW GB/s':>10} {'GOPS':>10} {'speed':<6}")
    print("  " + "-" * 50)
    for r in sorted(rl_rows, key=lambda x: (x.get("speed", ""), safe_int(x.get("ops_per_elem")) or 0)):
        ops = r.get("ops_per_elem", "?")
        ai  = safe_float(r.get("arith_intensity_ops_per_byte"))
        bw  = safe_float(r.get("achieved_bw_GB_s"))
        gps = safe_float(r.get("achieved_gops_s"))
        spd = r.get("speed", "?")
        print(f"  {ops:<14} {ai or 0:>10.3f} {bw or 0:>10.3f} {gps or 0:>10.3f} {spd:<6}")

    if not HAS_MPL:
        print("\nmatplotlib not found — skipping plot. Install with: pip install matplotlib numpy")
        return

    # ── Plot ──────────────────────────────────────────────────────────────────
    fig, ax = plt.subplots(figsize=(11, 7))

    ai_range = np.logspace(-2, 4, 1000)   # 0.01 to 10000 ops/byte

    colors     = {"fast": "#1f77b4", "slow": "#ff7f0e"}
    markers_sw = {"fast": "o",       "slow": "s"}
    markers_rl = {"fast": "^",       "slow": "v"}
    linestyles = {"fast": "-",       "slow": "--"}

    for speed in ("fast", "slow"):
        pc = peak_compute.get(speed, 0)
        bw = peak_bw.get(speed, 0)
        if pc <= 0 and bw <= 0:
            continue
        c  = colors[speed]
        ls = linestyles[speed]

        # Roofline = min(compute_peak, bw * AI)
        roof = np.minimum(pc, bw * ai_range) if pc > 0 and bw > 0 \
               else (bw * ai_range if bw > 0 else np.full_like(ai_range, pc))

        ax.plot(ai_range, roof, color=c, linestyle=ls, linewidth=2,
                label=f"{speed} roofline")

        if pc > 0 and bw > 0:
            ridge_ai = pc / bw
            ax.axvline(ridge_ai, color=c, linestyle=":", alpha=0.4)
            ax.text(ridge_ai * 1.08, pc * 0.45,
                    f"ridge {ridge_ai:.0f} ops/B",
                    color=c, fontsize=8, rotation=90, va="center")

    # Plot dummy/roofline measured points (BW/compute profile)
    for speed in ("fast", "slow"):
        pts = [(safe_float(r.get("arith_intensity_ops_per_byte")),
                safe_float(r.get("achieved_gops_s")),
                safe_int(r.get("ops_per_elem")))
               for r in rl_rows
               if r.get("speed", "fast") == speed
               and safe_float(r.get("arith_intensity_ops_per_byte"))
               and safe_float(r.get("achieved_gops_s"))]
        if not pts:
            continue
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ax.scatter(xs, ys, color=colors[speed], marker=markers_rl[speed],
                   s=50, zorder=5, alpha=0.7,
                   label=f"{speed} roofline kernel")
        for ai, gops, ops in pts:
            ax.annotate(f"ops={ops}", (ai, gops),
                        textcoords="offset points", xytext=(3, 3), fontsize=6)

    # Plot SW data points
    for speed in ("fast", "slow"):
        pts = [(safe_float(r.get("arith_intensity_ops_per_byte")),
                safe_float(r.get("gcups")),
                r.get("seq_len", "?"),
                r.get("cpg", "8"),
                r.get("app", "?"))
               for r in sw_rows if r.get("speed", "fast") == speed]
        if not pts:
            continue
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ax.scatter(xs, ys, color=colors[speed], marker=markers_sw[speed],
                   s=70, zorder=6, label=f"{speed} SW points")
        for ai, gcups, sl, cpg, app in pts:
            ax.annotate(f"L={sl}\ncpg={cpg}", (ai, gcups),
                        textcoords="offset points", xytext=(4, 4), fontsize=6)

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Arithmetic Intensity (ops / byte)", fontsize=12)
    ax.set_ylabel("Performance (GOPS / GCUPS)", fontsize=12)
    ax.set_title("Roofline Model — HammerBlade (fast vs slow core)", fontsize=13)
    ax.legend(fontsize=9)
    ax.grid(True, which="both", alpha=0.3)

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"Roofline plot saved to: {args.out}")


if __name__ == "__main__":
    main()
