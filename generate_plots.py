#!/usr/bin/env python3
"""generate_plots.py — build the benchmark plot set + markdown index.

All plots use the CHIPWIDE views (per-pod × 8) since pods share one DRAM
and we care about whole-chip throughput.

Data sources (produced by fix_slow_csv.py):
  results/results_20260423_233526_chipwide.csv         fast-clock
  results/results_20260424_072636_sim32bw_chipwide.csv slow × 32 = "32× BW"

Outputs:
  plots/*.png       300 dpi figures
  plots/README.md   descriptions and image index
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from textwrap import dedent

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
import pandas as pd


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
RESULTS_DIR = Path("results")
PLOTS_DIR = Path("plots")
PLOTS_DIR.mkdir(exist_ok=True)
DPI = 300

N_PODS = 8
POD_AREA_MM2 = 38.875  # all 8 pods

FAST_CSV = RESULTS_DIR / "results_20260423_233526_chipwide.csv"
SIM_CSV  = RESULTS_DIR / "results_20260424_072636_sim32bw_chipwide.csv"

plt.rcParams.update({
    "figure.dpi":        100,
    "savefig.dpi":       DPI,
    "savefig.bbox":      "tight",
    "font.size":         10,
    "axes.titlesize":    11,
    "axes.labelsize":    10,
    "legend.fontsize":   9,
    "axes.grid":         True,
    "grid.alpha":        0.3,
    "axes.axisbelow":    True,
    "figure.figsize":    (6.2, 4.2),
})

COLORS = {
    "sw/1d":       "#1f77b4",
    "sw/2d":       "#d62728",
    "fast":        "#1f77b4",
    "sim32bw":     "#ff7f0e",
    "sw/1d_fast":  "#1f77b4",
    "sw/1d_sim":   "#aec7e8",
    "sw/2d_fast":  "#d62728",
    "sw/2d_sim":   "#ff9896",
}

# cpg colour palette (blues, cool to warm)
CPG_COLORS = {
    1:   "#08306b",
    4:   "#2171b5",
    8:   "#6baed6",
    16:  "#fd8d3c",
    128: "#99000d",
}

PLOT_INDEX: list[tuple[str, str, str]] = []   # (section, name, desc)


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------
def load_chipwide(path):
    df = pd.read_csv(path)
    return df


def sw_rows(df, app):
    d = df[df["app"] == app].copy()
    for c in ("seq_len", "num_seq", "repeat", "cpg", "pod_unique_data"):
        d[c] = pd.to_numeric(d[c], errors="coerce").astype("Int64")
    for c in ("kernel_time_sec", "gcups"):
        d[c] = pd.to_numeric(d[c], errors="coerce")
    d = d.dropna(subset=["seq_len", "gcups", "kernel_time_sec"])
    d["time_per_seq_s"] = d["kernel_time_sec"] / (d["num_seq"] * d["repeat"])
    return d


def rl_rows(df):
    d = df[df["app"] == "dummy/roofline"].copy()
    for c in ("ops_per_elem", "n_elems", "repeat", "cpg"):
        d[c] = pd.to_numeric(d[c], errors="coerce").astype("Int64")
    for c in ("arith_intensity_ops_per_byte",
              "achieved_bw_GB_s", "achieved_gops_s", "kernel_time_sec"):
        d[c] = pd.to_numeric(d[c], errors="coerce")
    return d.dropna(subset=["arith_intensity_ops_per_byte",
                            "achieved_bw_GB_s",
                            "achieved_gops_s"]).sort_values("ops_per_elem")


fast_df = load_chipwide(FAST_CSV)
sim_df  = load_chipwide(SIM_CSV)

fast_sw1d = sw_rows(fast_df, "sw/1d")
fast_sw2d = sw_rows(fast_df, "sw/2d")
sim_sw1d  = sw_rows(sim_df,  "sw/1d")
sim_sw2d  = sw_rows(sim_df,  "sw/2d")
fast_rl   = rl_rows(fast_df)
sim_rl    = rl_rows(sim_df)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def best_per_config(d, group_cols, val="gcups"):
    """Pick the highest-<val> row per (group_cols) — deduplicates multiple
    data-volume variants at the same seq_len/cpg/pu by keeping the fastest."""
    idx = d.groupby(group_cols)[val].idxmax()
    return d.loc[idx].sort_values("seq_len")


SEQ_LEN_TICKS = [32, 64, 128, 256, 512, 1024, 2048]


def set_log2_seq_axis(ax, limit_to=None):
    ax.set_xscale("log", base=2)
    ax.set_xlabel("seq_len")
    ticks = SEQ_LEN_TICKS if limit_to is None else [t for t in SEQ_LEN_TICKS if t <= limit_to]
    ax.set_xticks(ticks)
    ax.xaxis.set_major_formatter(mticker.FixedFormatter([str(t) for t in ticks]))
    ax.xaxis.set_minor_formatter(mticker.NullFormatter())
    ax.xaxis.set_minor_locator(mticker.NullLocator())


def set_loglog_seq_axis(ax, limit_to=None):
    """Log-log with seq_len labels on x, standard log on y."""
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("seq_len")
    ticks = SEQ_LEN_TICKS if limit_to is None else [t for t in SEQ_LEN_TICKS if t <= limit_to]
    ax.set_xticks(ticks)
    ax.xaxis.set_major_formatter(mticker.FixedFormatter([str(t) for t in ticks]))
    ax.xaxis.set_minor_formatter(mticker.NullFormatter())
    ax.xaxis.set_minor_locator(mticker.NullLocator())


def save(fig, name, section, desc):
    path = PLOTS_DIR / f"{name}.png"
    fig.savefig(path)
    plt.close(fig)
    PLOT_INDEX.append((section, name, desc))
    print(f"wrote {path}")


# ===========================================================================
# Section 1: Single-algorithm scaling
# ===========================================================================

def plot_single_algo_scaling():
    """Per-algorithm scaling: time/alignment and GCUPS vs seq_len.

    For sw/1d we fix cpg=8 (hardware default: one tile-column per pipeline).
    Duplicates at the same (seq_len, cpg, pu) are resolved by picking the
    best GCUPS — i.e. the config that stressed the kernel most.
    """
    # ---- sw/1d canonical (cpg=8, pu=0) ------------------------------------
    sw1d = fast_sw1d[(fast_sw1d.cpg == 8) & (fast_sw1d.pod_unique_data == 0)]
    sw1d = best_per_config(sw1d, ["seq_len", "cpg", "pod_unique_data"])

    fig, ax = plt.subplots()
    ax.plot(sw1d.seq_len, sw1d.gcups, "o-", color=COLORS["sw/1d"], lw=2)
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/1d — chip throughput vs sequence length (cpg=8)")
    ax.set_ylim(bottom=0)
    save(fig, "01_sw1d_gcups_vs_seqlen", "Single-algorithm scaling",
         "sw/1d chip-wide GCUPS at cpg=8, shared-data config. Shows the "
         "throughput ramp from short sequences (startup-dominated) to the "
         "~31 GCUPS plateau around seq_len=256+.")

    fig, ax = plt.subplots()
    ax.plot(sw1d.seq_len, sw1d.time_per_seq_s, "o-",
            color=COLORS["sw/1d"], lw=2)
    set_loglog_seq_axis(ax)
    ax.set_ylabel("time per alignment (s)")
    ax.set_title("sw/1d — per-alignment wallclock (cpg=8)")
    # O(n^2) reference anchored to the largest point
    xs = np.array([sw1d.seq_len.min(), sw1d.seq_len.max()])
    y0 = float(sw1d.time_per_seq_s.iloc[-1])
    x0 = float(sw1d.seq_len.iloc[-1])
    ax.plot(xs, y0 * (xs / x0) ** 2, ":", color="grey",
            lw=1, label="O(n²) reference")
    ax.legend()
    save(fig, "02_sw1d_time_vs_seqlen", "Single-algorithm scaling",
         "sw/1d per-alignment wallclock on log-log axes. Follows the O(n²) "
         "reference slope closely, with a slight efficiency gain at larger "
         "sizes (the pipeline-fill overhead amortizes).")

    # ---- sw/2d canonical (pu=0) -------------------------------------------
    sw2d = best_per_config(fast_sw2d[fast_sw2d.pod_unique_data == 0],
                           ["seq_len", "cpg", "pod_unique_data"])

    fig, ax = plt.subplots()
    ax.plot(sw2d.seq_len, sw2d.gcups, "s-", color=COLORS["sw/2d"], lw=2)
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/2d — chip throughput vs sequence length")
    ax.set_ylim(bottom=0)
    save(fig, "03_sw2d_gcups_vs_seqlen", "Single-algorithm scaling",
         "sw/2d chip-wide GCUPS. Sweeps only four sizes (32, 64, 128, 192) "
         "because the 2D systolic DMEM footprint is dominant.")

    fig, ax = plt.subplots()
    ax.plot(sw2d.seq_len, sw2d.time_per_seq_s, "s-",
            color=COLORS["sw/2d"], lw=2)
    set_loglog_seq_axis(ax, limit_to=256)
    ax.set_ylabel("time per alignment (s)")
    ax.set_title("sw/2d — per-alignment wallclock")
    xs = np.array([sw2d.seq_len.min(), sw2d.seq_len.max()])
    y0 = float(sw2d.time_per_seq_s.iloc[-1])
    x0 = float(sw2d.seq_len.iloc[-1])
    ax.plot(xs, y0 * (xs / x0) ** 2, ":", color="grey",
            lw=1, label="O(n²) reference")
    ax.legend()
    save(fig, "04_sw2d_time_vs_seqlen", "Single-algorithm scaling",
         "sw/2d per-alignment wallclock on log-log axes. Also O(n²) but "
         "with a steeper constant factor at small sizes relative to sw/1d.")


# ===========================================================================
# Section 2: Algorithm comparison
# ===========================================================================

def plot_algorithm_comparison():
    """sw/1d and sw/2d on the same axes, chip-wide throughput and latency."""
    sw1d = best_per_config(
        fast_sw1d[(fast_sw1d.cpg == 8) & (fast_sw1d.pod_unique_data == 0)],
        ["seq_len", "cpg", "pod_unique_data"])
    sw2d = best_per_config(fast_sw2d[fast_sw2d.pod_unique_data == 0],
                           ["seq_len", "cpg", "pod_unique_data"])

    fig, ax = plt.subplots()
    ax.plot(sw1d.seq_len, sw1d.gcups, "o-", color=COLORS["sw/1d"],
            lw=2, label="sw/1d (cpg=8)")
    ax.plot(sw2d.seq_len, sw2d.gcups, "s-", color=COLORS["sw/2d"],
            lw=2, label="sw/2d")
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("Algorithm comparison — chip GCUPS vs seq_len")
    ax.set_ylim(bottom=0)
    ax.legend()
    save(fig, "05_algos_gcups_vs_seqlen", "Algorithm comparison",
         "sw/1d vs sw/2d chip throughput. Both converge to ~30 GCUPS at "
         "larger sequences; sw/1d reaches the plateau earlier. sw/2d has a "
         "tiny range because its DMEM budget caps seq_len at 192.")

    fig, ax = plt.subplots()
    ax.plot(sw1d.seq_len, sw1d.time_per_seq_s, "o-",
            color=COLORS["sw/1d"], lw=2, label="sw/1d (cpg=8)")
    ax.plot(sw2d.seq_len, sw2d.time_per_seq_s, "s-",
            color=COLORS["sw/2d"], lw=2, label="sw/2d")
    set_loglog_seq_axis(ax)
    ax.set_ylabel("time per alignment (s)")
    ax.set_title("Algorithm comparison — per-alignment wallclock")
    ax.legend()
    save(fig, "06_algos_time_vs_seqlen", "Algorithm comparison",
         "Per-alignment wallclock on log-log axes. Both algorithms show "
         "O(n²) scaling; at matched seq_len sw/1d and sw/2d land within a "
         "small constant factor.")


# ===========================================================================
# Section 3: Cores-per-group (sw/1d)
# ===========================================================================

def plot_cpg_effect():
    """How cpg (pipeline length) shapes sw/1d throughput and latency."""
    d = best_per_config(fast_sw1d[fast_sw1d.pod_unique_data == 0],
                        ["seq_len", "cpg", "pod_unique_data"])

    fig, ax = plt.subplots()
    for cpg in sorted(d.cpg.unique()):
        sub = d[d.cpg == cpg]
        ax.plot(sub.seq_len, sub.gcups, "o-",
                color=CPG_COLORS.get(int(cpg), "#333"),
                lw=2, label=f"cpg={cpg}")
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/1d — cores-per-group sweep")
    ax.set_ylim(bottom=0)
    ax.legend(title="cores/group")
    save(fig, "07_sw1d_cpg_gcups", "Cores-per-group effect",
         "Chip-wide GCUPS as a function of pipeline length (cpg). "
         "Small cpg (1) lets the whole chip run 128 independent pipelines, "
         "which wins at short sequences. Long cpg (128) dedicates all "
         "128 cores to one sequence — only pays off at the longest sizes "
         "because pipeline-fill overhead dominates otherwise.")

    fig, ax = plt.subplots()
    for cpg in sorted(d.cpg.unique()):
        sub = d[d.cpg == cpg].sort_values("seq_len")
        if len(sub) < 2:
            continue
        ax.plot(sub.seq_len, sub.time_per_seq_s, "o-",
                color=CPG_COLORS.get(int(cpg), "#333"),
                lw=2, label=f"cpg={cpg}")
    set_loglog_seq_axis(ax)
    ax.set_ylabel("time per alignment (s)")
    ax.set_title("sw/1d — per-alignment wallclock by cpg")
    ax.legend(title="cores/group")
    save(fig, "08_sw1d_cpg_time", "Cores-per-group effect",
         "Per-alignment wallclock by cpg. Larger cpg = longer pipeline = "
         "each alignment completes faster (more cores work on it in "
         "parallel), but the chip runs fewer alignments concurrently. The "
         "per-alignment-latency vs chip-throughput trade-off is the whole "
         "point of this knob.")


# ===========================================================================
# Section 4: Pod-unique-data effect
# ===========================================================================

def plot_pod_unique_effect():
    """Shared-input vs pod-unique input at matching configs."""
    # sw/1d: seq=256 has both variants across several cpgs; pick best GCUPS
    # for each (cpg, pu) pair.
    d = fast_sw1d[fast_sw1d.seq_len == 256]
    d = best_per_config(d, ["seq_len", "cpg", "pod_unique_data"])
    cpgs = sorted(set(d.cpg.astype(int)))
    paired_cpgs = [c for c in cpgs
                   if {0, 1} <= set(d[d.cpg == c].pod_unique_data.astype(int))]

    fig, ax = plt.subplots()
    width = 0.35
    xpos = np.arange(len(paired_cpgs))
    shared = [float(d[(d.cpg == c) & (d.pod_unique_data == 0)].gcups.iloc[0])
              for c in paired_cpgs]
    unique = [float(d[(d.cpg == c) & (d.pod_unique_data == 1)].gcups.iloc[0])
              for c in paired_cpgs]
    ax.bar(xpos - width/2, shared, width, label="shared",
           color="#6baed6")
    ax.bar(xpos + width/2, unique, width, label="per-pod unique",
           color="#fd8d3c")
    ax.set_xticks(xpos, [str(c) for c in paired_cpgs])
    ax.set_xlabel("cores per group")
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/1d — shared vs per-pod-unique inputs (seq_len=256)")
    ax.legend()
    save(fig, "09_sw1d_pod_unique", "Pod-unique-data effect",
         "Whether all pods see the same input or each pod gets its own "
         "slice. The SW kernels are compute-bound, so their DRAM read "
         "pressure is low — we expect no material difference, and the plot "
         "confirms that (differences are sub-percent).")

    # sw/2d
    d2 = fast_sw2d.copy()
    d2["key"] = list(zip(d2.seq_len.astype(int), d2.num_seq.astype(int),
                         d2.repeat.astype(int)))
    paired = sorted({k for k in d2.key
                     if {0, 1} <= set(d2[d2.key == k].pod_unique_data.astype(int))})
    fig, ax = plt.subplots()
    xpos = np.arange(len(paired))
    shared = [float(d2[(d2.key == k) & (d2.pod_unique_data == 0)].gcups.iloc[0])
              for k in paired]
    unique = [float(d2[(d2.key == k) & (d2.pod_unique_data == 1)].gcups.iloc[0])
              for k in paired]
    labels = [f"seq={k[0]}\nnum_seq={k[1]}" for k in paired]
    ax.bar(xpos - width/2, shared, width, label="shared", color="#6baed6")
    ax.bar(xpos + width/2, unique, width, label="per-pod unique", color="#fd8d3c")
    ax.set_xticks(xpos, labels)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/2d — shared vs per-pod-unique inputs")
    ax.legend()
    save(fig, "10_sw2d_pod_unique", "Pod-unique-data effect",
         "Same question for sw/2d. Again compute-bound and indifferent to "
         "whether pods read the same or different slices of DRAM.")


# ===========================================================================
# Section 5: Fast vs simulated-high-BW
# ===========================================================================

def _sw_pair_fast_sim(fast_d, sim_d, fixed_cpg=None):
    """Join the fast and sim data on (seq_len, cpg, pu), best per config."""
    keep = ["seq_len", "cpg", "pod_unique_data"]
    f = best_per_config(
        fast_d[fast_d.pod_unique_data == 0]
              if fixed_cpg is None else
              fast_d[(fast_d.pod_unique_data == 0) & (fast_d.cpg == fixed_cpg)],
        keep)
    s = best_per_config(
        sim_d[sim_d.pod_unique_data == 0]
              if fixed_cpg is None else
              sim_d[(sim_d.pod_unique_data == 0) & (sim_d.cpg == fixed_cpg)],
        keep)
    return f, s


def plot_fast_vs_sim_bw():
    """Fast-clock (measured) vs 32× BW simulation (slow × 32)."""
    # sw/1d (cpg=8)
    f1, s1 = _sw_pair_fast_sim(fast_sw1d, sim_sw1d, fixed_cpg=8)

    fig, ax = plt.subplots()
    ax.plot(f1.seq_len, f1.gcups, "o-", color=COLORS["sw/1d_fast"],
            lw=2, label="fast (real)")
    ax.plot(s1.seq_len, s1.gcups, "o--", color=COLORS["sim32bw"],
            lw=2, label="32× BW (sim)")
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/1d — fast vs 32× BW (cpg=8)")
    ax.set_ylim(bottom=0)
    ax.legend()
    save(fig, "11_sw1d_fast_vs_sim32bw", "Fast vs simulated high-BW",
         "sw/1d chip-wide GCUPS, measured at full clock (solid) and "
         "projected to a hypothetical 32× DRAM bandwidth machine (dashed). "
         "At mid and large seq_len the curves overlap — sw/1d is compute-"
         "bound and extra BW doesn't help. The small-seq_len gap where "
         "sim32bw > fast reveals that short-sequence runs do hit some "
         "BW-related overhead (pipeline-fill reloads, barrier fences) that "
         "the 32×-BW projection papers over.")

    # sw/2d
    f2, s2 = _sw_pair_fast_sim(fast_sw2d, sim_sw2d)
    fig, ax = plt.subplots()
    ax.plot(f2.seq_len, f2.gcups, "s-", color=COLORS["sw/2d_fast"],
            lw=2, label="fast (real)")
    ax.plot(s2.seq_len, s2.gcups, "s--", color=COLORS["sim32bw"],
            lw=2, label="32× BW (sim)")
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("sw/2d — fast vs 32× BW")
    ax.set_ylim(bottom=0)
    ax.legend()
    save(fig, "12_sw2d_fast_vs_sim32bw", "Fast vs simulated high-BW",
         "Same comparison for sw/2d. At seq_len=128 and 192 the curves "
         "overlap; at seq_len=32 the sim line is noticeably above, which "
         "tells us sw/2d at very short sequences pays a per-rep fixed "
         "overhead (barrier + wavefront fill) that would cost proportionally "
         "less wallclock on a fast-clock machine with more memory headroom.")

    # Both algos on one plot
    fig, ax = plt.subplots()
    ax.plot(f1.seq_len, f1.gcups, "o-", color=COLORS["sw/1d_fast"],
            lw=2, label="sw/1d fast")
    ax.plot(s1.seq_len, s1.gcups, "o--", color=COLORS["sw/1d_sim"],
            lw=2, label="sw/1d 32× BW")
    ax.plot(f2.seq_len, f2.gcups, "s-", color=COLORS["sw/2d_fast"],
            lw=2, label="sw/2d fast")
    ax.plot(s2.seq_len, s2.gcups, "s--", color=COLORS["sw/2d_sim"],
            lw=2, label="sw/2d 32× BW")
    set_log2_seq_axis(ax)
    ax.set_ylabel("GCUPS (chip)")
    ax.set_title("Fast vs 32× BW — both SW algorithms")
    ax.set_ylim(bottom=0)
    ax.legend(ncol=2)
    save(fig, "13_algos_fast_vs_sim32bw", "Fast vs simulated high-BW",
         "Combined view. Fast and 32× BW lines practically overlap per "
         "algorithm — confirming both kernels operate in the compute-bound "
         "half of the roofline.")


# ===========================================================================
# Section 6: Roofline
# ===========================================================================

def _plot_roofline_frame(ax, rl, label, color, marker, ls):
    ax.loglog(rl.arith_intensity_ops_per_byte, rl.achieved_gops_s,
              marker=marker, linestyle=ls, color=color,
              lw=1.5, ms=5, label=f"{label} measured")


def _overlay_ceilings(ax, rl, color, label):
    """Plot the BW and compute ceilings derived from a measurement series."""
    bw_peak  = rl.achieved_bw_GB_s.max()
    cpt_peak = rl.achieved_gops_s.max()
    xs = np.logspace(np.log10(rl.arith_intensity_ops_per_byte.min()) - 0.3,
                     np.log10(rl.arith_intensity_ops_per_byte.max()) + 0.3,
                     400)
    roof = np.minimum(cpt_peak, bw_peak * xs)
    ax.loglog(xs, roof, "--", color=color, lw=2, alpha=0.8,
              label=f"{label} roofline  ({bw_peak:.1f} GB/s, {cpt_peak:.0f} GOPS)")


def plot_roofline_fast():
    fig, ax = plt.subplots(figsize=(7.2, 5.0))
    _overlay_ceilings(ax, fast_rl, COLORS["fast"], "fast")
    _plot_roofline_frame(ax, fast_rl, "fast", COLORS["fast"], "o", "")
    ax.set_xlabel("arithmetic intensity (ops/byte)")
    ax.set_ylabel("performance (GOPS, chip)")
    ax.set_title("Roofline — fast clock (chip-wide)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="lower right")
    save(fig, "14_roofline_fast", "Roofline",
         "Chip-wide roofline at the fast core clock. Each dot is one "
         "dummy/roofline sweep point; the dashed line is the BW/compute "
         "envelope built from the highest measured values.")


def plot_roofline_sim():
    fig, ax = plt.subplots(figsize=(7.2, 5.0))
    _overlay_ceilings(ax, sim_rl, COLORS["sim32bw"], "32× BW (sim)")
    _plot_roofline_frame(ax, sim_rl, "32× BW (sim)", COLORS["sim32bw"], "^", "")
    ax.set_xlabel("arithmetic intensity (ops/byte)")
    ax.set_ylabel("performance (GOPS, chip)")
    ax.set_title("Roofline — 32× BW projection (chip-wide)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="lower right")
    save(fig, "15_roofline_sim32bw", "Roofline",
         "Chip-wide roofline under the 32× bandwidth projection (the slow-"
         "clock sweep scaled by 32). The BW ceiling rises proportionally, "
         "so the ridge point shifts left — most workloads that were BW-"
         "bound on fast become compute-bound in this world.")


def plot_roofline_overlay():
    fig, ax = plt.subplots(figsize=(7.6, 5.2))
    _overlay_ceilings(ax, fast_rl, COLORS["fast"],     "fast")
    _overlay_ceilings(ax, sim_rl,  COLORS["sim32bw"],  "32× BW (sim)")
    _plot_roofline_frame(ax, fast_rl, "fast",          COLORS["fast"], "o", "")
    _plot_roofline_frame(ax, sim_rl,  "32× BW (sim)",  COLORS["sim32bw"], "^", "")
    ax.set_xlabel("arithmetic intensity (ops/byte)")
    ax.set_ylabel("performance (GOPS, chip)")
    ax.set_title("Roofline — fast vs 32× BW (chip-wide)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="lower right")
    save(fig, "16_roofline_overlay", "Roofline",
         "Both rooflines overlaid. Compute ceilings are nearly identical "
         "(the 32× projection preserves per-ALU throughput), while the BW "
         "ceiling is 32× higher in the projection — visible as the steeper "
         "dashed line shifting up and to the left.")


# ===========================================================================
# Section 7: Derived / summary
# ===========================================================================

def plot_density_summary():
    """Bar chart: GCUPS per mm² at the best-measured configs."""
    configs = [
        ("sw/1d cpg=1", best_per_config(
            fast_sw1d[(fast_sw1d.cpg == 1) & (fast_sw1d.pod_unique_data == 0)],
            ["seq_len", "cpg", "pod_unique_data"]).gcups.max()),
        ("sw/1d cpg=8", best_per_config(
            fast_sw1d[(fast_sw1d.cpg == 8) & (fast_sw1d.pod_unique_data == 0)],
            ["seq_len", "cpg", "pod_unique_data"]).gcups.max()),
        ("sw/1d cpg=16", best_per_config(
            fast_sw1d[(fast_sw1d.cpg == 16) & (fast_sw1d.pod_unique_data == 0)],
            ["seq_len", "cpg", "pod_unique_data"]).gcups.max()),
        ("sw/1d cpg=128", best_per_config(
            fast_sw1d[(fast_sw1d.cpg == 128) & (fast_sw1d.pod_unique_data == 0)],
            ["seq_len", "cpg", "pod_unique_data"]).gcups.max()),
        ("sw/2d", best_per_config(
            fast_sw2d[fast_sw2d.pod_unique_data == 0],
            ["seq_len", "cpg", "pod_unique_data"]).gcups.max()),
    ]
    labels = [c[0] for c in configs]
    gcups = np.array([c[1] for c in configs])
    density = gcups / POD_AREA_MM2

    fig, ax = plt.subplots(figsize=(6.8, 4.0))
    xpos = np.arange(len(configs))
    bars = ax.bar(xpos, density, color=[
        CPG_COLORS.get(1),   CPG_COLORS.get(8),   CPG_COLORS.get(16),
        CPG_COLORS.get(128), COLORS["sw/2d"]
    ])
    for bar, g, d_ in zip(bars, gcups, density):
        ax.text(bar.get_x() + bar.get_width() / 2,
                d_ + 0.01,
                f"{d_:.3f}\n({g:.1f} GCUPS)",
                ha="center", va="bottom", fontsize=8)
    ax.set_xticks(xpos, labels, rotation=15)
    ax.set_ylabel("GCUPS / mm²  (chip, 38.875 mm²)")
    ax.set_title("Throughput density — peak chip GCUPS per mm²")
    ax.set_ylim(top=density.max() * 1.25)
    save(fig, "17_gcups_density", "Summary",
         "Chip-wide peak GCUPS divided by total pod area (38.875 mm²). "
         "cpg=1 narrowly wins on density because it runs 128 independent "
         "pipelines; cpg=8 is 98 % of that with simpler per-pipeline "
         "resource use. sw/2d matches within a few percent.")


# ===========================================================================
# Markdown index
# ===========================================================================

def write_markdown():
    by_section: dict[str, list[tuple[str, str]]] = {}
    for section, name, desc in PLOT_INDEX:
        by_section.setdefault(section, []).append((name, desc))

    intro = dedent("""\
        # Benchmark plots

        All throughputs are **chip-wide** (per-pod × 8 pods, since all pods
        share one DRAM connection). Data sources:

        - `results/results_20260423_233526_chipwide.csv` — fast-clock sweep
        - `results/results_20260424_072636_sim32bw_chipwide.csv` — slow-clock
          sweep scaled × 32, i.e. projected onto a hypothetical fast-clock
          machine with 32× more DRAM bandwidth

        Generated by `generate_plots.py`. Plots saved at 300 dpi.

        Chip area assumed: 38.875 mm² (all 8 pods together).

        """)

    body: list[str] = [intro]
    section_order = [
        "Single-algorithm scaling",
        "Algorithm comparison",
        "Cores-per-group effect",
        "Pod-unique-data effect",
        "Fast vs simulated high-BW",
        "Roofline",
        "Summary",
    ]
    for sec in section_order:
        entries = by_section.get(sec, [])
        if not entries:
            continue
        body.append(f"## {sec}\n")
        for name, desc in entries:
            body.append(f"### `{name}`\n")
            body.append(f"![{name}]({name}.png)\n")
            body.append(desc + "\n")

    out = PLOTS_DIR / "README.md"
    out.write_text("\n".join(body))
    print(f"wrote {out}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    plot_single_algo_scaling()
    plot_algorithm_comparison()
    plot_cpg_effect()
    plot_pod_unique_effect()
    plot_fast_vs_sim_bw()
    plot_roofline_fast()
    plot_roofline_sim()
    plot_roofline_overlay()
    plot_density_summary()
    write_markdown()


if __name__ == "__main__":
    main()
