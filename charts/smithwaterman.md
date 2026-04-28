# Smith-Waterman charts

All charts at 300 dpi.  Default size 10×8 inches; 1D plots also export
a 21×9 wide variant for the multi-CPG view.  GCUPs values are scaled
×8 to reflect all 8 pods (chip-wide; 1024 cores total).  Time/sequence
is in microseconds, log-scale.  cpg=2 omitted (most rows timed out at
launch scale; only one passed, not enough to draw a curve).

## 2D systolic array

### `2d_time.png`
![2D time/sequence](2d_time.png)

### `2d_gcups.png`
![2D GCUPs](2d_gcups.png)

## 1D systolic array — best CPG per sequence length

### `1d_best_time.png` / `1d_best_time_wide.png`
![1D best time/sequence — 10×8](1d_best_time.png)
![1D best time/sequence — 21×9 wide](1d_best_time_wide.png)

### `1d_best_gcups.png` / `1d_best_gcups_wide.png`
![1D best GCUPs — 10×8](1d_best_gcups.png)
![1D best GCUPs — 21×9 wide](1d_best_gcups_wide.png)

## 1D systolic array — all CPG values

Each CPG plotted in its own color.

### `1d_allcpg_time.png` / `1d_allcpg_time_wide.png`
![1D all-CPG time/sequence — 10×8](1d_allcpg_time.png)
![1D all-CPG time/sequence — 21×9 wide](1d_allcpg_time_wide.png)

### `1d_allcpg_gcups.png` / `1d_allcpg_gcups_wide.png`
![1D all-CPG GCUPs — 10×8](1d_allcpg_gcups.png)
![1D all-CPG GCUPs — 21×9 wide](1d_allcpg_gcups_wide.png)

### `1d_allcpg_gcups_max.png` / `1d_allcpg_gcups_max_wide.png`

Same plot with a horizontal dashed line at the peak GCUPs across cpg ≥ 4
— every cpg ≥ 4 hits the same chip-wide peak (~31 GCUPs) at its own seq_len.

![1D all-CPG GCUPs with shared max — 10×8](1d_allcpg_gcups_max.png)
![1D all-CPG GCUPs with shared max — 21×9 wide](1d_allcpg_gcups_max_wide.png)

## 1D vs 2D — best per sequence length

Common sequence-length range only (sw/2d ceiling = 1024).

### `compare_time.png`
![1D vs 2D time/sequence](compare_time.png)

### `compare_gcups.png`
![1D vs 2D GCUPs](compare_gcups.png)

## Effect of high bandwidth

Slow-clock measurements scaled ×32 in time (project to a fast clock with
proportionally more DRAM bandwidth) → "high bandwidth".  Compute-bound
rows look identical to regular; memory-bound rows lift.

### `2d_effect_hibw.png`

Clear lift at small seq_len (memory-bound regime) — at seq_len=32 the
chip-wide GCUPs jumps from 7.1 → 16.3 (2.3× lift).  By seq_len=256 the
two curves overlap (compute-bound).

![2D effect of high bandwidth](2d_effect_hibw.png)

### `1d_effect_hibw.png`

Best CPG-per-seq_len comparisons across the four (cpg, seq_len) pairs
that have both fast and slow data: regular and high-bandwidth land on
top of each other — sw/1d is compute-bound at every measured config,
so the projected BW lift gives ≈ no benefit.

![1D effect of high bandwidth](1d_effect_hibw.png)
