#!/usr/bin/env python3
"""Generate all charts for the launch experiments.

Reads CSVs from ``data/<experiment>/`` and writes PNGs under ``charts/``.
One module-level function per logical chart group; ``main()`` runs them all.

Conventions (project-wide):
- Default figure size: 10×8 inches.
- Wider variant (for plots with more legend / many series): 21×9 inches.
- DPI 300, PNG only.
- Sequence-length axis: log base 2, ticks labeled with raw integers.
- Time/sequence in microseconds, log scale.  GCUPs linear.
- All GCUPs / BW / GOps values are chip-wide (×8 from per-pod data so the
  number reflects all 1024 cores: 8 pods × 128 tiles).
- Colors: when comparing regular vs high-bandwidth runs, **always**
  blue = regular, orange = high bandwidth (project memory).  Other
  comparisons use a distinct palette to avoid name collisions.
- High-bandwidth = slow run with the time scaled down by 32×.  In the
  CSV that already happened (run_experiments.sh applies the projection
  to throughput cols).  In raw-data plots (vvadd) we apply it inline.
"""

import csv
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

REPO  = Path(__file__).resolve().parent
DATA  = REPO / "data"
OUT   = REPO / "charts"
OUT.mkdir(exist_ok=True)

DPI = 150                # Lower than 300 so Google Slides doesn't downsample
                         # (which it does aggressively above ~2000 px on the
                         # long edge, blurring text/lines).  At 150 DPI the
                         # default size is 1500×1200 px, well within Slides'
                         # native display range.
SIZE_DEFAULT = (10, 8)   # 1.25:1 aspect
SIZE_WIDE    = (21, 9)   # ~2.33:1 aspect, for many-series plots

NUM_PODS = 8

# Regular vs high-bandwidth comparison colors (project-wide convention).
COLOR_REGULAR = "#1f77b4"  # blue
COLOR_HIBW    = "#ff7f0e"  # orange

# Algorithm-comparison colors (avoid blue/orange to keep the bw scheme clean).
COLOR_1D = "#2ca02c"  # green
COLOR_2D = "#d62728"  # red

# Per-CPG colors for the all-CPG sweep plots.  Cool → warm as cpg grows.
# cpg=2 dropped — only one row passed (most timed out at scale).
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
    return t and t not in ("TIMEOUT", "FAILED", "COMPILE_ERROR", "N/A")


def time_per_seq_us(row):
    """Wall-clock time per sequence, chip-wide (8 pods running in parallel)."""
    t  = float(row["kernel_time_sec"])
    ns = int(row["num_seq"])
    rp = int(row["repeat"])
    return t * 1e6 / (NUM_PODS * ns * rp)


def gcups_chipwide(row):
    """SW GCUPs over all 1024 cores (= per-pod CSV value × 8)."""
    return float(row["gcups"]) * NUM_PODS


def bw_chipwide(row):
    """Roofline BW (GB/s) over all 1024 cores."""
    v = row.get("achieved_bw_GB_s", "")
    return float(v) * NUM_PODS if v else None


def gops_chipwide(row):
    v = row.get("achieved_gops_s", "")
    return float(v) * NUM_PODS if v else None


def best_per_seqlen(rows):
    """Pick the row with max gcups for each seq_len."""
    out = {}
    for r in rows:
        sl = int(r["seq_len"])
        if sl not in out or float(r["gcups"]) > float(out[sl]["gcups"]):
            out[sl] = r
    return [out[k] for k in sorted(out)]


def load_sw1d_fast():
    return [r for r in load_csv(DATA / "sw1d_cpg_fast" / "combined.csv")
            if is_ok(r) and int(r["cpg"]) not in EXCLUDED_CPGS]


def load_sw2d_fast():
    return [r for r in load_csv(DATA / "sw2d_seqlen_fast" / "results_20260427_181442.csv")
            if is_ok(r)]


def load_sw1d_slow():
    """5 OK rows from sw1d_cpg_slow (one cpg=8192 timed out)."""
    rows = []
    for c in (DATA / "sw1d_cpg_slow").glob("results_*.csv"):
        rows += [r for r in load_csv(c) if is_ok(r)]
    return rows


def load_sw2d_slow():
    rows = []
    for c in (DATA / "sw2d_seqlen_slow").glob("results_*.csv"):
        rows += [r for r in load_csv(c) if is_ok(r)]
    return rows


def load_roofline(slow):
    """Combine all OK rows from data/roofline_{fast,slow}/results_*.csv.

    For slow, two CSVs exist (initial + retry-failed).  Concat, then
    dedupe by test_name keeping the OK row.
    """
    sub = "roofline_slow" if slow else "roofline_fast"
    rows = []
    for c in sorted((DATA / sub).glob("results_*.csv")):
        rows += [r for r in load_csv(c) if is_ok(r)]
    # Dedupe: keep first OK occurrence per test_name (csvs are listed in time
    # order; the retry CSV adds rows that were missing in the initial).
    seen = set()
    out = []
    for r in rows:
        if r["test_name"] in seen:
            continue
        seen.add(r["test_name"])
        out.append(r)
    return out


# ─── Plot helpers ────────────────────────────────────────────────────────────
def style_seqlen_axis(ax, seq_lens):
    ax.set_xscale("log", base=2)
    ax.set_xticks(sorted(set(seq_lens)))
    ax.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax.xaxis.set_minor_formatter(mticker.NullFormatter())
    ax.set_xlabel("Sequence Length")


def time_axis(ax):
    ax.set_yscale("log")
    ax.set_ylabel("Time/sequence (µs)")


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
    rows = load_sw2d_fast()
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
    rows = load_sw1d_fast()
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
    rows = load_sw1d_fast()
    by_cpg = {}
    for r in rows:
        by_cpg.setdefault(int(r["cpg"]), []).append(r)
    for cpg in by_cpg:
        by_cpg[cpg].sort(key=lambda r: int(r["seq_len"]))
    all_seqlens = sorted({int(r["seq_len"]) for r in rows})

    # Common max across cpg ∈ {4, 8, 16, 32, 64, 128} — for the max-line variant.
    high_cpgs = [c for c in by_cpg if c >= 4]
    common_max = max(gcups_chipwide(r)
                     for c in high_cpgs for r in by_cpg[c])

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        # ── time ───────────────────────────────────────────────────────────
        fig, ax = plt.subplots(figsize=size)
        for cpg in sorted(by_cpg):
            sls   = [int(r["seq_len"]) for r in by_cpg[cpg]]
            times = [time_per_seq_us(r) for r in by_cpg[cpg]]
            ax.plot(sls, times, "o-", color=CPG_COLORS[cpg], label=f"cpg={cpg}")
        style_seqlen_axis(ax, all_seqlens); time_axis(ax)
        ax.legend(ncol=2, loc="best", frameon=False)
        ax.set_title("1D performance")
        save(fig, f"1d_allcpg_time{suffix}")

        # ── gcups ──────────────────────────────────────────────────────────
        fig, ax = plt.subplots(figsize=size)
        for cpg in sorted(by_cpg):
            sls   = [int(r["seq_len"]) for r in by_cpg[cpg]]
            gcups = [gcups_chipwide(r) for r in by_cpg[cpg]]
            ax.plot(sls, gcups, "o-", color=CPG_COLORS[cpg], label=f"cpg={cpg}")
        style_seqlen_axis(ax, all_seqlens); gcups_axis(ax)
        ax.legend(ncol=2, loc="best", frameon=False)
        ax.set_title("1D performance")
        save(fig, f"1d_allcpg_gcups{suffix}")

        # ── gcups with max line for cpg ≥ 4 ────────────────────────────────
        fig, ax = plt.subplots(figsize=size)
        for cpg in sorted(by_cpg):
            sls   = [int(r["seq_len"]) for r in by_cpg[cpg]]
            gcups = [gcups_chipwide(r) for r in by_cpg[cpg]]
            ax.plot(sls, gcups, "o-", color=CPG_COLORS[cpg], label=f"cpg={cpg}")
        # Max line is annotated inline (not in the legend) to keep the
        # legend tight at 7 cpg series.
        ax.axhline(common_max, color="black", linestyle="--", linewidth=1.5)
        style_seqlen_axis(ax, all_seqlens); gcups_axis(ax)
        ax.legend(ncol=2, loc="best", frameon=False)
        ax.set_title("1D performance — common max for cpg ≥ 4")
        save(fig, f"1d_allcpg_gcups_max{suffix}")


def plot_1d_vs_2d():
    rows_2d = load_sw2d_fast()
    rows_2d.sort(key=lambda r: int(r["seq_len"]))
    rows_1d = load_sw1d_fast()
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


# ─── SW: effect of high bandwidth ────────────────────────────────────────────
def plot_sw_effect_hibw():
    """Separate regular vs high-bandwidth plots for sw/1d and sw/2d.

    y-axis is shared (0–40 GCUPs) so the two plots are directly
    comparable when placed side-by-side in a presentation.  x-axis
    is independent — each plot fits its own seq_len range to avoid
    a lot of empty space.

    sw/2d covers seq_len 32–1024 (6 points, both fast and slow have full data).
    sw/1d covers only the 4 (cpg, seq_len) pairs that have both fast and
    slow data — the slow filter sampled the largest seq_len per CPG only.
    """
    SHARED_YLIM = (0, 40)

    # ── sw/2d ──────────────────────────────────────────────────────────────
    fast_2d = {int(r["seq_len"]): r for r in load_sw2d_fast()}
    slow_2d = {int(r["seq_len"]): r for r in load_sw2d_slow()}
    common_2d = sorted(set(fast_2d) & set(slow_2d))
    reg = [gcups_chipwide(fast_2d[sl]) for sl in common_2d]
    hib = [gcups_chipwide(slow_2d[sl]) for sl in common_2d]
    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(common_2d, reg, "o-", color=COLOR_REGULAR, label="regular")
    ax.plot(common_2d, hib, "s-", color=COLOR_HIBW,    label="high bandwidth")
    style_seqlen_axis(ax, common_2d); gcups_axis(ax)
    ax.set_ylim(*SHARED_YLIM)
    ax.legend(loc="lower right", frameon=False)
    ax.set_title("2D bandwidth performance")
    save(fig, "2d_effect_hibw")

    # ── sw/1d (sparse — best-per-CPG slow rows only) ───────────────────────
    fast_1d_idx = {(int(r["cpg"]), int(r["seq_len"])): r for r in load_sw1d_fast()}
    slow_1d_idx = {(int(r["cpg"]), int(r["seq_len"])): r for r in load_sw1d_slow()}
    common_1d = sorted(set(fast_1d_idx) & set(slow_1d_idx), key=lambda x: x[1])
    sls    = [sl for (_, sl) in common_1d]
    reg_1d = [gcups_chipwide(fast_1d_idx[k]) for k in common_1d]
    hib_1d = [gcups_chipwide(slow_1d_idx[k]) for k in common_1d]
    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    ax.plot(sls, reg_1d, "o-", color=COLOR_REGULAR, label="regular")
    ax.plot(sls, hib_1d, "s-", color=COLOR_HIBW,    label="high bandwidth")
    style_seqlen_axis(ax, sls); gcups_axis(ax)
    ax.set_ylim(*SHARED_YLIM)
    ax.legend(loc="lower right", frameon=False)
    ax.set_title("1D bandwidth performance")
    save(fig, "1d_effect_hibw")


# ─── vvadd ───────────────────────────────────────────────────────────────────
def vvadd_chipwide_GBs(size, t_us):
    """vvadd c = a + b: 2 reads + 1 write × 4 B per int × 8 pods = 96 B/elem.
    t in µs → s = t × 1e-6, so GB/s = 96 N / (t_us × 1e3)."""
    return [96 * s / (t * 1e3) for s, t in zip(size, t_us)]


def plot_vvadd():
    fast = load_csv(DATA / "vvadd" / "fast.csv")
    slow = load_csv(DATA / "vvadd" / "slow.csv")
    sizes_f = [int(r["size"])           for r in fast]
    t_f_us  = [int(r["kernel_time_us"]) for r in fast]
    sizes_s = [int(r["size"])           for r in slow]
    t_s_us  = [int(r["kernel_time_us"]) for r in slow]

    bw_regular = vvadd_chipwide_GBs(sizes_f, t_f_us)
    # High bandwidth: divide slow time by 32 (sim32bw projection) before
    # computing throughput.  Equivalent to scaling raw-slow BW by ×32.
    t_hibw_us  = [t / 32 for t in t_s_us]
    bw_hibw    = vvadd_chipwide_GBs(sizes_s, t_hibw_us)

    def make_size_axis(ax):
        ax.set_xscale("log", base=2)
        ax.set_xticks(sorted(set(sizes_f) | set(sizes_s)))
        ax.xaxis.set_major_formatter(mticker.FuncFormatter(
            lambda x, _: f"{int(x)//1024}K" if x < 1<<20 else f"{int(x)//(1<<20)}M"))
        ax.set_xlabel("Vector Size")

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        # Bandwidth
        fig, ax = plt.subplots(figsize=size)
        ax.plot(sizes_f, bw_regular, "o-", color=COLOR_REGULAR, label="regular")
        ax.plot(sizes_s, bw_hibw,    "s-", color=COLOR_HIBW,    label="high bandwidth")
        make_size_axis(ax)
        ax.set_ylabel("Bandwidth (GB/s)")
        ax.legend(loc="best", frameon=False)
        ax.set_title("vvadd performance")
        save(fig, f"vvadd_bw{suffix}")

        # Time/array (kernel time vs array size).  Plot the high-bandwidth
        # PROJECTED time (slow_t / 32), not raw slow.
        fig, ax = plt.subplots(figsize=size)
        ax.plot(sizes_f, t_f_us,    "o-", color=COLOR_REGULAR, label="regular")
        ax.plot(sizes_s, t_hibw_us, "s-", color=COLOR_HIBW,    label="high bandwidth")
        make_size_axis(ax)
        ax.set_yscale("log")
        ax.set_ylabel("Time/array (µs)")
        ax.legend(loc="best", frameon=False)
        ax.set_title("vvadd performance")
        save(fig, f"vvadd_time{suffix}")

    # Stats for the markdown.
    return {
        "peak_bw_regular": max(bw_regular),
        "peak_bw_hibw":    max(bw_hibw),
        "speedup_at_max_size": bw_hibw[-1] / bw_regular[-1],
        "size_at_peak_regular": sizes_f[bw_regular.index(max(bw_regular))],
        "size_at_peak_hibw":    sizes_s[bw_hibw.index(max(bw_hibw))],
    }


# ─── Roofline ────────────────────────────────────────────────────────────────
def plot_roofline():
    """Chip-wide roofline with regular + high-bandwidth scatter points and
    BW/compute envelopes.  Slow CSV's bw/gops are already ×32-projected per
    pod (run_experiments.sh); we apply the ×8 chip-wide multiplier here.

    For each AI value we keep the row with max achieved GOPS, which drops
    the UNROLL=1/4/8 sweep points at ops=1 that fall below the envelope.
    """
    import numpy as np
    from matplotlib.lines import Line2D

    fast = load_roofline(slow=False)
    slow = load_roofline(slow=True)

    def best_per_ai(rows):
        by_ai = {}
        for r in rows:
            v = r.get("achieved_gops_s", "")
            if not v:
                continue
            ai = float(r["arith_intensity_ops_per_byte"])
            g  = float(v)
            if ai not in by_ai or g > float(by_ai[ai]["achieved_gops_s"]):
                by_ai[ai] = r
        return list(by_ai.values())

    def points(rows):
        rows = best_per_ai(rows)
        ai, bw, gops = [], [], []
        for r in rows:
            b = bw_chipwide(r)
            g = gops_chipwide(r)
            if b is None or g is None:
                continue
            ai.append(float(r["arith_intensity_ops_per_byte"])); bw.append(b); gops.append(g)
        # Sort by AI for clean envelope plotting.
        order = sorted(range(len(ai)), key=lambda i: ai[i])
        return ([ai[i] for i in order], [bw[i] for i in order], [gops[i] for i in order])

    ai_f, bw_f, gops_f = points(fast)
    ai_s, bw_s, gops_s = points(slow)

    bw_peak_reg = max(bw_f)
    bw_peak_hib = max(bw_s)
    cmp_peak_reg = max(gops_f)
    cmp_peak_hib = max(gops_s)

    # AI grid spans the full range of observed points (and a bit beyond) so
    # both envelopes cover their entire BW-bound region and compute plateau.
    ai_lo = min(ai_f + ai_s) / 2
    ai_hi = max(ai_f + ai_s) * 2
    ai_grid = np.logspace(np.log10(ai_lo), np.log10(ai_hi), 500)
    env_reg = np.minimum(bw_peak_reg * ai_grid, cmp_peak_reg)
    env_hib = np.minimum(bw_peak_hib * ai_grid, cmp_peak_hib)

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        fig, ax = plt.subplots(figsize=size)
        # Plot envelopes (no legend label) and scatter (carries legend label).
        ax.plot(ai_grid, env_reg, "--", color=COLOR_REGULAR, alpha=0.8, linewidth=2)
        ax.plot(ai_grid, env_hib, "--", color=COLOR_HIBW,    alpha=0.8, linewidth=2)
        ax.scatter(ai_f, gops_f, color=COLOR_REGULAR, s=70, zorder=5)
        ax.scatter(ai_s, gops_s, color=COLOR_HIBW,    s=70, marker="s", zorder=5)
        # Combined legend: dashed line + marker per regime, single entry each.
        handles = [
            Line2D([0], [0], color=COLOR_REGULAR, linestyle="--", marker="o",
                   markersize=9, linewidth=2, label="regular"),
            Line2D([0], [0], color=COLOR_HIBW,    linestyle="--", marker="s",
                   markersize=9, linewidth=2, label="high bandwidth"),
        ]
        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlabel("Arithmetic Intensity (ops/byte)")
        ax.set_ylabel("GOPS")
        ax.legend(handles=handles, loc="lower right", frameon=False)
        ax.set_title("Roofline")
        save(fig, f"roofline{suffix}")

    return {
        "bw_peak_regular":   bw_peak_reg,
        "bw_peak_hibw":      bw_peak_hib,
        "compute_peak_regular": cmp_peak_reg,
        "compute_peak_hibw":    cmp_peak_hib,
        "bw_speedup":        bw_peak_hib / bw_peak_reg,
    }


NW_STYLES = {
    "nw/baseline":  ("#2ca02c", "o"),  # green, circle
    "nw/efficient": ("#d62728", "s"),  # red, square
    "nw/naive":     ("#9467bd", "^"),  # purple, triangle
}


def plot_nw():
    """Compare nw/{baseline, efficient, naive} — GCUPs and time/sequence.

    Three implementations, three rows each (seq_len ∈ {32, 64, 128}; 256 was
    dropped from the launch because nw/efficient hangs there).  Same plot
    conventions as the sw/* charts: GCUPs ×8 chip-wide, log seq_len axis.
    """
    rows = []
    for d in ("nw_seqlen_fast", "nw_naive_fast"):
        for c in (DATA / d).glob("results_*.csv"):
            rows += [r for r in load_csv(c) if is_ok(r)]
    by_app = {}
    for r in rows:
        by_app.setdefault(r["app"], []).append(r)
    for app in by_app:
        by_app[app].sort(key=lambda r: int(r["seq_len"]))
    all_seqlens = sorted({int(r["seq_len"]) for r in rows})

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        fig, ax = plt.subplots(figsize=size)
        for app in sorted(by_app):
            color, marker = NW_STYLES[app]
            sls   = [int(r["seq_len"]) for r in by_app[app]]
            gcups = [gcups_chipwide(r) for r in by_app[app]]
            ax.plot(sls, gcups, marker + "-", color=color, label=app)
        style_seqlen_axis(ax, all_seqlens); gcups_axis(ax)
        ax.legend(loc="best", frameon=False)
        ax.set_title("NW performance")
        save(fig, f"nw_gcups{suffix}")

        fig, ax = plt.subplots(figsize=size)
        for app in sorted(by_app):
            color, marker = NW_STYLES[app]
            sls   = [int(r["seq_len"]) for r in by_app[app]]
            times = [time_per_seq_us(r) for r in by_app[app]]
            ax.plot(sls, times, marker + "-", color=color, label=app)
        style_seqlen_axis(ax, all_seqlens); time_axis(ax)
        ax.legend(loc="best", frameon=False)
        ax.set_title("NW performance")
        save(fig, f"nw_time{suffix}")


def plot_radix_sort():
    """Radix sort: time per single-array sort (µs) and elements/ns vs SIZE.

    All values are SINGLE-POD — only pod 0 sorts; we do NOT multiply by 8.

    Slow CSV's NUM_ARR was scaled /8 by run_experiments.sh:
        actual_num_arr_slow = max(1, test_name_num_arr // 8)

    Time/array (µs):
        regular: t_fast × 1e6 / test_num_arr
        high BW: (t_slow / 32) × 1e6 / actual_num_arr

    Elements/ns:
        regular: (test_num_arr × SIZE) / (t_fast × 1e9)
        high BW: (actual_num_arr × SIZE) / ((t_slow / 32) × 1e9)
    """
    def parse_row(r, slow=False):
        # test_name = "radix_sort_<SIZE>__num-arr_<N>"
        try:
            head, tail = r["test_name"].split("__num-arr_")
            sz   = int(head.split("_")[-1])
            tna  = int(tail)
        except (ValueError, IndexError):
            return None
        t = float(r["kernel_time_sec"])
        if slow:
            actual_na = max(1, tna // 8)
            t_per_arr_us = (t / 32) * 1e6 / actual_na
            elem_ns      = (actual_na * sz) / ((t / 32) * 1e9)
        else:
            t_per_arr_us = t * 1e6 / tna
            elem_ns      = (tna * sz) / (t * 1e9)
        return (sz, t_per_arr_us, elem_ns)

    fast_rows = []
    for c in (DATA / "radix_sort_fast").glob("results_*.csv"):
        fast_rows += [parse_row(r, slow=False) for r in load_csv(c) if is_ok(r)]
    slow_rows = []
    for c in (DATA / "radix_sort_slow").glob("results_*.csv"):
        slow_rows += [parse_row(r, slow=True) for r in load_csv(c) if is_ok(r)]
    fast_rows = sorted(p for p in fast_rows if p is not None)
    slow_rows = sorted(p for p in slow_rows if p is not None)

    sizes_f = [p[0] for p in fast_rows]
    tarr_f  = [p[1] for p in fast_rows]   # time per array (µs)
    e_f     = [p[2] for p in fast_rows]   # elements/ns
    sizes_s = [p[0] for p in slow_rows]
    tarr_s  = [p[1] for p in slow_rows]
    e_s     = [p[2] for p in slow_rows]

    all_sizes = sorted(set(sizes_f) | set(sizes_s))

    def make_size_axis(ax):
        ax.set_xscale("log", base=2)
        ax.set_xticks(all_sizes)
        ax.xaxis.set_major_formatter(mticker.FuncFormatter(
            lambda x, _: f"{int(x)//1024}K" if x < 1 << 20 else f"{int(x)//(1<<20)}M"))
        ax.set_xlabel("Array Size")

    for size, suffix in ((SIZE_DEFAULT, ""), (SIZE_WIDE, "_wide")):
        # Time per array sort (log y, µs).
        fig, ax = plt.subplots(figsize=size)
        ax.plot(sizes_f, tarr_f, "o-", color=COLOR_REGULAR, label="regular")
        ax.plot(sizes_s, tarr_s, "s-", color=COLOR_HIBW,    label="high bandwidth")
        make_size_axis(ax)
        ax.set_yscale("log")
        ax.set_ylabel("Time/array (µs)")
        ax.legend(loc="best", frameon=False)
        ax.set_title("Radix sort performance")
        save(fig, f"radix_time{suffix}")

        # elements/ns — log y.
        fig, ax = plt.subplots(figsize=size)
        ax.plot(sizes_f, e_f, "o-", color=COLOR_REGULAR, label="regular")
        ax.plot(sizes_s, e_s, "s-", color=COLOR_HIBW,    label="high bandwidth")
        make_size_axis(ax)
        ax.set_yscale("log")
        ax.set_ylabel("elements/ns")
        ax.legend(loc="best", frameon=False)
        ax.set_title("Radix sort performance")
        save(fig, f"radix_elem_per_ns{suffix}")

        # elements/ns — linear y.
        fig, ax = plt.subplots(figsize=size)
        ax.plot(sizes_f, e_f, "o-", color=COLOR_REGULAR, label="regular")
        ax.plot(sizes_s, e_s, "s-", color=COLOR_HIBW,    label="high bandwidth")
        make_size_axis(ax)
        ax.set_ylabel("elements/ns")
        ax.legend(loc="best", frameon=False)
        ax.set_title("Radix sort performance")
        save(fig, f"radix_elem_per_ns_linear{suffix}")


def plot_arch_compare():
    """Bar chart: GCUPs/mm² across CPU, GPU, HB (1D), HB (2D).

    HB peak GCUPs is taken chip-wide (×8) from the best run with cpg > 1
    (cpg=1 is the no-grouping case and unfair to the comparison).
    """
    rows_1d = load_sw1d_fast()
    best_1d = max((r for r in rows_1d if int(r["cpg"]) > 1),
                  key=lambda r: float(r["gcups"]))
    hb_1d_gcups = gcups_chipwide(best_1d)

    rows_2d = load_sw2d_fast()
    best_2d = max(rows_2d, key=lambda r: float(r["gcups"]))
    hb_2d_gcups = gcups_chipwide(best_2d)

    HB_AREA_MM2 = 38.875
    HB_NODE     = "14/16 nm"

    bars = [
        ("CPU",     "14 nm", 734,         1396.0),  # 2× Xeon Skylake-SP XCC
        ("GPU",     "7 nm",  1940,         826.0),  # NVIDIA A100
        ("HB (1D)", HB_NODE, hb_1d_gcups, HB_AREA_MM2),
        ("HB (2D)", HB_NODE, hb_2d_gcups, HB_AREA_MM2),
    ]
    eff = [g / a for (_, _, g, a) in bars]
    labels = [f"{name}\n{node}" for (name, node, _, _) in bars]
    colors = ["#7f7f7f", "#76b900", COLOR_1D, COLOR_2D]  # CPU gray, NVIDIA green, HB green/red

    fig, ax = plt.subplots(figsize=SIZE_DEFAULT)
    xs = list(range(len(bars)))
    rects = ax.bar(xs, eff, color=colors, edgecolor="black", linewidth=1)
    for rect, v in zip(rects, eff):
        ax.text(rect.get_x() + rect.get_width()/2, rect.get_height(),
                f"{v:.2f}", ha="center", va="bottom", fontsize=15, fontweight="bold")
    ax.set_xticks(xs)
    ax.set_xticklabels(labels)
    ax.set_ylabel("GCUPs / mm²")
    ax.set_title("Area-normalized SW performance")
    ax.set_ylim(0, max(eff) * 1.15)
    # Visual divider between commodity (CPU/GPU) and HB.
    ax.axvline(1.5, color="black", linestyle=":", linewidth=1.2, alpha=0.6)
    save(fig, "arch_compare_gcups_per_mm2")

    return {
        "hb_1d_gcups_chipwide": hb_1d_gcups,
        "hb_2d_gcups_chipwide": hb_2d_gcups,
        "hb_1d_eff":            hb_1d_gcups / HB_AREA_MM2,
        "hb_2d_eff":            hb_2d_gcups / HB_AREA_MM2,
        "best_1d_seq_len":      int(best_1d["seq_len"]),
        "best_1d_cpg":          int(best_1d["cpg"]),
        "best_2d_seq_len":      int(best_2d["seq_len"]),
    }


def main():
    plot_2d()
    plot_1d_best()
    plot_1d_all_cpg()
    plot_1d_vs_2d()
    plot_sw_effect_hibw()
    plot_nw()
    vv_stats = plot_vvadd()
    rl_stats = plot_roofline()
    plot_radix_sort()
    arch_stats = plot_arch_compare()
    print(f"Wrote charts to {OUT}/")
    print()
    print("=== vvadd stats ===")
    for k, v in vv_stats.items():
        print(f"  {k:30s} {v}")
    print()
    print("=== roofline stats ===")
    for k, v in rl_stats.items():
        print(f"  {k:30s} {v}")
    print()
    print("=== arch-compare stats ===")
    for k, v in arch_stats.items():
        print(f"  {k:30s} {v}")


if __name__ == "__main__":
    main()
