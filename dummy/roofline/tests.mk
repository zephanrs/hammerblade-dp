# dummy/roofline tests — sweep OPS_PER_ELEM to trace the full roofline.
#
# Operational intensity = 2*OPS_PER_ELEM / 8  ops/byte
#   (2 int ops per inner iteration; 4B read + 4B write per element)
#
# Kernel now uses contiguous chunks per tile (not strided) for accurate BW.
# Each tile reads n_elems/128 contiguous ints — maximizes DRAM row-buffer hits.
#
# Two data variants (matching sw/ A/B pattern):
#   (default)          all pods get same input array  → tests BW with shared addresses
#   pod-unique-data_1  each pod gets distinct slice   → tests true independent BW
#
# n_elems=4194304 (16M ints = 64 MB) — well above cache; repeats tuned for ~10s
# per test based on measured per-repeat times from the prior run.
#
# Dense log-spaced ops sweep so the roofline knee and compute ceiling are
# well resolved.
#
#   ops    OI (ops/B)   region
#     1     0.25        BW anchor (pure memory)
#     2     0.5
#     4     1.0
#     8     2.0
#    16     4.0
#    32     8.0
#    64    16.0
#   128    32.0
#   256    64.0         approaching knee
#   512   128.0         knee region
#  1024   256.0
#  2048   512.0
#  4096  1024.0         compute-bound
#  8192  2048.0
# 16384  4096.0         deep compute roof

TESTS += ops_1__n-elems_4194304__repeat_128
TESTS += ops_2__n-elems_4194304__repeat_128
TESTS += ops_4__n-elems_4194304__repeat_128
TESTS += ops_8__n-elems_4194304__repeat_128
TESTS += ops_16__n-elems_4194304__repeat_128
TESTS += ops_32__n-elems_4194304__repeat_128
TESTS += ops_64__n-elems_4194304__repeat_128
TESTS += ops_128__n-elems_4194304__repeat_128
TESTS += ops_256__n-elems_4194304__repeat_96
TESTS += ops_512__n-elems_4194304__repeat_64
TESTS += ops_1024__n-elems_4194304__repeat_48
TESTS += ops_2048__n-elems_4194304__repeat_24
TESTS += ops_4096__n-elems_4194304__repeat_12
TESTS += ops_8192__n-elems_4194304__repeat_6
TESTS += ops_16384__n-elems_4194304__repeat_3
