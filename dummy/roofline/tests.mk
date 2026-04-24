# dummy/roofline tests — sweep OPS_PER_ELEM to trace the full roofline.
#
# Operational intensity = 2*OPS_PER_ELEM / 8  ops/byte
#   (2 int ops per inner iteration; 4B read + 4B write per element)
#
# Kernel uses contiguous chunks per tile — maximizes DRAM row-buffer hits.
#
# n_elems=4194304 (16M ints = 64 MB) — well above cache.
# Repeats tuned for ~10s per test from measured per-repeat times.
#
# ~sqrt(2)-spaced sweep (28 points) so the curve is dense on a log axis
# AND has plenty of coverage near the knee (~ops=400) on a linear axis.
# Rooflines are conventionally plotted log-log; plot_roofline.py already
# does. If plotting linearly, the extra density near the knee is useful.

TESTS += ops_1__n-elems_4194304__repeat_128
TESTS += ops_2__n-elems_4194304__repeat_128
TESTS += ops_3__n-elems_4194304__repeat_128
TESTS += ops_4__n-elems_4194304__repeat_128
TESTS += ops_6__n-elems_4194304__repeat_128
TESTS += ops_8__n-elems_4194304__repeat_128
TESTS += ops_12__n-elems_4194304__repeat_128
TESTS += ops_16__n-elems_4194304__repeat_128
TESTS += ops_24__n-elems_4194304__repeat_128
TESTS += ops_32__n-elems_4194304__repeat_128
TESTS += ops_48__n-elems_4194304__repeat_128
TESTS += ops_64__n-elems_4194304__repeat_128
TESTS += ops_96__n-elems_4194304__repeat_128
TESTS += ops_128__n-elems_4194304__repeat_128
TESTS += ops_192__n-elems_4194304__repeat_96
TESTS += ops_256__n-elems_4194304__repeat_96
TESTS += ops_384__n-elems_4194304__repeat_96
TESTS += ops_512__n-elems_4194304__repeat_64
TESTS += ops_768__n-elems_4194304__repeat_64
TESTS += ops_1024__n-elems_4194304__repeat_48
TESTS += ops_1536__n-elems_4194304__repeat_32
TESTS += ops_2048__n-elems_4194304__repeat_24
TESTS += ops_3072__n-elems_4194304__repeat_16
TESTS += ops_4096__n-elems_4194304__repeat_12
TESTS += ops_6144__n-elems_4194304__repeat_8
TESTS += ops_8192__n-elems_4194304__repeat_6
TESTS += ops_12288__n-elems_4194304__repeat_4
TESTS += ops_16384__n-elems_4194304__repeat_3
