# Smith-Waterman charts

All charts at 300 dpi, 3.5×2 inches default (some 1D plots also exported
at 6×2 wide).  GCUPs values are scaled ×8 to reflect all 8 pods (chip-wide).
Time/sequence is in microseconds, log-scale.

## 2D systolic array

### `2d_time.png`
![2D time/sequence](2d_time.png)

### `2d_gcups.png`
![2D GCUPs](2d_gcups.png)

## 1D systolic array — best CPG per sequence length

### `1d_best_time.png` / `1d_best_time_wide.png`
![1D best time/sequence — 3.5×2](1d_best_time.png)
![1D best time/sequence — 6×2 wide](1d_best_time_wide.png)

### `1d_best_gcups.png` / `1d_best_gcups_wide.png`
![1D best GCUPs — 3.5×2](1d_best_gcups.png)
![1D best GCUPs — 6×2 wide](1d_best_gcups_wide.png)

## 1D systolic array — all CPG values

Each CPG plotted in its own color.

### `1d_allcpg_time.png` / `1d_allcpg_time_wide.png`
![1D all-CPG time/sequence — 3.5×2](1d_allcpg_time.png)
![1D all-CPG time/sequence — 6×2 wide](1d_allcpg_time_wide.png)

### `1d_allcpg_gcups.png` / `1d_allcpg_gcups_wide.png`
![1D all-CPG GCUPs — 3.5×2](1d_allcpg_gcups.png)
![1D all-CPG GCUPs — 6×2 wide](1d_allcpg_gcups_wide.png)

## 1D vs 2D — best per sequence length

Common sequence-length range only (sw/2d ceiling = 1024).

### `compare_time.png`
![1D vs 2D time/sequence](compare_time.png)

### `compare_gcups.png`
![1D vs 2D GCUPs](compare_gcups.png)
