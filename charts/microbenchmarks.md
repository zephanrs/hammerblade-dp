# Microbenchmark charts

Diagnostic kernels: roofline (compute/BW envelope) and vvadd (memory
bandwidth probe).  Same conventions as `smithwaterman.md`: 10×8 inches,
300 dpi.  Regular vs high-bandwidth comparisons use **blue = regular**,
**orange = high bandwidth** consistently.

## vvadd

Per-pod vvadd kernel (`c = a + b`).  Bytes touched chip-wide per call
= 24 × N (2 reads + 1 write × 4 bytes × 8 pods, shared inputs).  High
bandwidth is the slow-clock measurement scaled ×32 (sim32bw projection,
i.e., what the chip would look like with proportionally more DRAM BW).

### `vvadd_bw.png`
![vvadd bandwidth](vvadd_bw.png)

### `vvadd_time.png`
Raw kernel time on both clocks; high-bandwidth shown unprojected so the
~4× slow/fast ratio is visible.

![vvadd time](vvadd_time.png)

## Roofline

_Pending high-bandwidth (slow-clock) run; will add when `roofline_slow`
results are pushed._
