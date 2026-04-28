#!/usr/bin/env python3
"""Generate all charts for the launch experiments.

Reads CSVs from ``data/<experiment>/`` and writes PNGs under ``charts/``.
One module-level function per logical chart group; ``main()`` runs them all.

Conventions (project-wide):
- Default figure size: 10×8 inches.
- Wider variant (for plots with more legend / many series): 24×8 inches.
- DPI 300, PNG only.
- Sequence-length axis: log base 2, ticks labeled with raw integers
  (32, 64, 128 — never 2^5, 2^6, ...).
- Time/sequence: log scale (microseconds).  GCUPs: linear.
- All GCUPs values are scaled ×8 so they reflect the chip (all 8 pods).
- Colors: when comparing regular vs high-bandwidth runs, **always**
  blue = regular, orange = high bandwidth (see memory).  Other
  comparisons use a distinct palette to avoid name collisions.
"""

import csv
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

REPO  = Path(__file__).resolve().parent
DATA  = REPO / "data"
OUT   = REPO / "charts"
OUT.mkdir(exist_ok=True)

DPI = 300
SIZE_DEFAULT = (10, 8)   # 1.25:1 aspect
SIZE_WIDE    = (24, 8)   # 3:1 aspect, for many-series plots

# Regular vs high-bandwidth comparison colors (project-wide convention).
COLOR_REGULAR = "#1f77b4"  # blue
COLOR_HIBW    = "#ff7f0e"  # orange

# Algorithm-comparison colors (avoid blue/orange to keep the bw scheme clean).
COLOR_1D = "#2ca02c"  # green
COLOR_2D = "#d62728"  # red

# Per-CPG colors for the all-CPG sweep plots.  Cool → warm as cpg grows.
# cpg=2 dropped — only one row passed (most timed out at scale), and it
# adds noise without insight relative to cpg=1 / cpg=4.
CPG_COLORS = {
    1:   "#1f77b4",
    4:   "#2ca02c",
    8:   "#bcbd22",
    16:  "#ff7f0e",
    32:  "#d62728",
    64:  "#9467bd",
    128: "#8c564b",
}
EXCLUDED_CPGS = {2}

plt.rcParams.update({
    "font.size": 16,
    "axes.titlesize": 20,
    "axes.labelsize": 18,
    "xtick.labelsize": 14,
    "ytick.labelsize": 14,
    "legend.fontsize": 13,
    "lines.linewidth": 2.5,
    "lines.markersize": 8,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "grid.linewidth": 0.8,
})


# ─── Data helpers ────────────────────────────────────────────────────────────
def load_csv(path: Path):
    with open(path) as f:
        return list(csv.DictReader(f))


def is_ok(row):
    t = row.get("kernel_time_sec", "")
    return t and t not in ("TIMEOUT", "FAILED", "COMPILE_ERROR")


def time_per_seq_us(row):
    """Wall-clock time per sequence, chip-wide (8 pods running in parallel)."""
    t  = float(row["kernel_time_sec"])
    ns = int(row["num_seq"])
    rp = int(row["repeat"])
    return t * 1e6 / (8 * ns * rp)


def gcups_chipwide(row):
    return float(row["gcups"]) * 8


def best_per_seqlen(rows):
    """Pick the row with max gcups for each seq_len."""
    out = {}
    for r in rows:
        sl = int(r["seq_len"])
        if sl not in out or float(r["gcups"]) > float(out[sl]["gcups"]):
            out[sl] = r
    return [out[k] for k in sorted(out)]


# ─── Plot helpers ────────────────────────────────────────────────────────────
def style_seqlen_axis(ax, seq_lens):
    ax.set_xscale("log", base=2)
    ax.set_xticks(sorted(set(seq_lens)))
    ax.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax.xaxis.set_minor_formatter(mticker.NullFormatter())
    ax.set_xlabel("Sequence Length")


def time_axis(ax):
    ax.set_yscale("log")
    ax.set_ylabel("Time/sequence")


def gcups_axis(ax):
    ax.set_ylabel("GCUPs")


def save(fig, name):
    fig.tight_layout()
    out = OUT / f"{name}.png"
    fig.savefig(out, dpi=DPI)
    plt.close(fig)
    return out


# ─── Plots ───────────────────────────────────────────────────────────────────
def plot_2d():
    rows = [r for r in load_csv(DATA / "sw2d_seqlen_fast" / "results_20260427_181442.csv") if is_ok(r)]
    rows.sort(key=lambda r: int(r["seq_len"]))
    seq_lens = [int(r["seq_len"]) for r in rows]
    times    = [time_per_seq_us(r) for r in rows]
    gcups    = [gcups_chipwide(r) for r in rows]

    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(seq_lens, times, "o-", color=COLOR_2D)
    style_seqlen_axis(ax, seq_lens); time_axis(ax)
    ax.set_title("2D performance")
    save(fig, "2d_time")

    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(seq_lens, gcups, "o-", color=COLOR_2D)
    style_seqlen_axis(ax, seq_lens); gcups_axis(ax)
    ax.set_title("2D performance")
    save(fig, "2d_gcups")


def plot_1d_best():
    rows = [r for r in load_csv(DATA / "sw1d_cpg_fast" / "combined.csv")
            if is_ok(r) and int(r["cpg"]) not in EXCLUDED_CPGS]
    best = best_per_seqlen(rows)
    seq_lens = [int(r["seq_len"]) for r in best]
    times    = [time_per_seq_us(r) for r in best]
    gcups    = [gcups_chipwide(r) for r in best]

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        fig, ax = plt.subplots(figsize=size)
        ax.plot(seq_lens, times, "o-", color=COLOR_1D)
        style_seqlen_axis(ax, seq_lens); time_axis(ax)
        ax.set_title("1D performance (best CPG per seq)")
        save(fig, f"1d_best_time{suffix}")

        fig, ax = plt.subplots(figsize=size)
        ax.plot(seq_lens, gcups, "o-", color=COLOR_1D)
        style_seqlen_axis(ax, seq_lens); gcups_axis(ax)
        ax.set_title("1D performance (best CPG per seq)")
        save(fig, f"1d_best_gcups{suffix}")


def plot_1d_all_cpg():
    rows = [r for r in load_csv(DATA / "sw1d_cpg_fast" / "combined.csv")
            if is_ok(r) and int(r["cpg"]) not in EXCLUDED_CPGS]
    by_cpg = {}
    for r in rows:
        by_cpg.setdefault(int(r["cpg"]), []).append(r)
    for cpg in by_cpg:
        by_cpg[cpg].sort(key=lambda r: int(r["seq_len"]))
    all_seqlens = sorted({int(r["seq_len"]) for r in rows})

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        fig, ax = plt.subplots(figsize=size)
        for cpg in sorted(by_cpg):
            sls   = [int(r["seq_len"]) for r in by_cpg[cpg]]
            times = [time_per_seq_us(r) for r in by_cpg[cpg]]
            ax.plot(sls, times, "o-", color=CPG_COLORS[cpg], label=f"cpg={cpg}")
        style_seqlen_axis(ax, all_seqlens); time_axis(ax)
        ax.legend(ncol=2, loc="best", frameon=False)
        ax.set_title("1D performance")
        save(fig, f"1d_allcpg_time{suffix}")

        fig, ax = plt.subplots(figsize=size)
        for cpg in sorted(by_cpg):
            sls   = [int(r["seq_len"]) for r in by_cpg[cpg]]
            gcups = [gcups_chipwide(r) for r in by_cpg[cpg]]
            ax.plot(sls, gcups, "o-", color=CPG_COLORS[cpg], label=f"cpg={cpg}")
        style_seqlen_axis(ax, all_seqlens); gcups_axis(ax)
        ax.legend(ncol=2, loc="best", frameon=False)
        ax.set_title("1D performance")
        save(fig, f"1d_allcpg_gcups{suffix}")


def plot_1d_vs_2d():
    rows_2d = [r for r in load_csv(DATA / "sw2d_seqlen_fast" / "results_20260427_181442.csv") if is_ok(r)]
    rows_2d.sort(key=lambda r: int(r["seq_len"]))
    rows_1d = [r for r in load_csv(DATA / "sw1d_cpg_fast" / "combined.csv")
               if is_ok(r) and int(r["cpg"]) not in EXCLUDED_CPGS]
    best_1d = {int(r["seq_len"]): r for r in best_per_seqlen(rows_1d)}
    by_2d   = {int(r["seq_len"]): r for r in rows_2d}
    common  = sorted(set(best_1d) & set(by_2d))

    times_1d = [time_per_seq_us(best_1d[sl]) for sl in common]
    gcups_1d = [gcups_chipwide(best_1d[sl])  for sl in common]
    times_2d = [time_per_seq_us(by_2d[sl])   for sl in common]
    gcups_2d = [gcups_chipwide(by_2d[sl])    for sl in common]

    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(common, times_1d, "o-", color=COLOR_1D, label="1D")
    ax.plot(common, times_2d, "s-", color=COLOR_2D, label="2D")
    style_seqlen_axis(ax, common); time_axis(ax)
    ax.legend(loc="best", frameon=False)
    ax.set_title("Comparing Implementations")
    save(fig, "compare_time")

    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(common, gcups_1d, "o-", color=COLOR_1D, label="1D")
    ax.plot(common, gcups_2d, "s-", color=COLOR_2D, label="2D")
    style_seqlen_axis(ax, common); gcups_axis(ax)
    ax.legend(loc="best", frameon=False)
    ax.set_title("Comparing Implementations")
    save(fig, "compare_gcups")


def plot_vvadd():
    """vvadd diagnostic: regular vs high-bandwidth chip-wide BW + time."""
    fast = load_csv(DATA / "vvadd" / "fast.csv")
    slow = load_csv(DATA / "vvadd" / "slow.csv")
    sizes_f = [int(r["size"])           for r in fast]
    t_f_us  = [int(r["kernel_time_us"]) for r in fast]
    sizes_s = [int(r["size"])           for r in slow]
    t_s_us  = [int(r["kernel_time_us"]) for r in slow]

    # Chip-wide bytes for one vvadd (c = a + b): 2 reads + 1 write = 3×N×4 B
    # per pod; ×8 pods (shared data) → 24×N bytes touched chip-wide.
    def bw_GBs(size, t_us):
        return [24 * s / (t * 1e3) for s, t in zip(size, t_us)]

    bw_regular = bw_GBs(sizes_f, t_f_us)
    # High-bandwidth = slow with the sim32bw projection (×32 throughput).
    bw_hibw    = [b * 32 for b in bw_GBs(sizes_s, t_s_us)]

    # Bandwidth chart
    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(sizes_f, bw_regular, "o-", color=COLOR_REGULAR, label="regular")
    ax.plot(sizes_s, bw_hibw,    "s-", color=COLOR_HIBW,    label="high bandwidth")
    ax.set_xscale("log", base=2)
    ax.set_xticks(sorted(set(sizes_f) | set(sizes_s)))
    ax.xaxis.set_major_formatter(mticker.FuncFormatter(
        lambda x, _: f"{int(x)//1024}K" if x < 1<<20 else f"{int(x)//(1<<20)}M"))
    ax.set_xlabel("Vector Size")
    ax.set_ylabel("Bandwidth (GB/s)")
    ax.legend(loc="best", frameon=False)
    ax.set_title("vvadd performance")
    save(fig, "vvadd_bw")

    # Raw time chart (also useful as a sanity diagnostic).
    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(sizes_f, t_f_us, "o-", color=COLOR_REGULAR, label="regular")
    ax.plot(sizes_s, t_s_us, "s-", color=COLOR_HIBW,    label="high bandwidth (raw slow)")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xticks(sorted(set(sizes_f) | set(sizes_s)))
    ax.xaxis.set_major_formatter(mticker.FuncFormatter(
        lambda x, _: f"{int(x)//1024}K" if x < 1<<20 else f"{int(x)//(1<<20)}M"))
    ax.set_xlabel("Vector Size")
    ax.set_ylabel("Time (µs)")
    ax.legend(loc="best", frameon=False)
    ax.set_title("vvadd performance")
    save(fig, "vvadd_time")


def main():
    plot_2d()
    plot_1d_best()
    plot_1d_all_cpg()
    plot_1d_vs_2d()
    plot_vvadd()
    print(f"Wrote charts to {OUT}/")


if __name__ == "__main__":
    main()
