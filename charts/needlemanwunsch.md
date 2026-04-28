# Needleman-Wunsch charts

Same conventions as `smithwaterman.md`: 10×8 inches default (21×9 wide
variant), 300 dpi.  GCUPs scaled ×8 to reflect the chip (1024 cores).
Time/sequence in microseconds, log scale.  Sequence length on log base 2.

Three implementations:

| App | Notes |
|---|---|
| `nw/baseline` | Row-by-row 1D systolic NW; only writes one int (final score) per sequence to DRAM. |
| `nw/efficient` | Hirschberg divide-and-conquer; produces a path, not just a score.  Per-iter DRAM writes. |
| `nw/naive` | Stores the full O(n²) DP matrix to DRAM per sequence — heavily memory-bound; needed `repeat=10` at seq_len=128 to dodge a hang. |

seq_len=256 was dropped from the launch — `nw/efficient` hangs at that size on HW (out of debugging time).

## NW — GCUPs

### `nw_gcups.png` / `nw_gcups_wide.png`

![NW GCUPs — 10×8](nw_gcups.png)
![NW GCUPs — 21×9 wide](nw_gcups_wide.png)

## NW — time per sequence

### `nw_time.png` / `nw_time_wide.png`

![NW time/sequence — 10×8](nw_time.png)
![NW time/sequence — 21×9 wide](nw_time_wide.png)
