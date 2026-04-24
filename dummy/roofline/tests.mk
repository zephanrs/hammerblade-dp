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
# n_elems=4194304 (16M ints = 64 MB) — well above cache; repeat scaled for ~20s.
# High-ops tests (4096, 8192, 16384) to find compute roof.
#
#   ops   OI (ops/B)   purpose
#     1     0.25       BW anchor (pure memory)
#     4     1.0
#    16     4.0
#    64    16.0
#   256    64.0
#  1024   256.0
#  4096  1024.0        expect compute-bound here
# 16384  4096.0        deep compute-bound ceiling

TESTS += ops_1__n-elems_4194304__repeat_64
TESTS += ops_4__n-elems_4194304__repeat_64
TESTS += ops_16__n-elems_4194304__repeat_64
TESTS += ops_64__n-elems_4194304__repeat_64
TESTS += ops_256__n-elems_4194304__repeat_64
TESTS += ops_1024__n-elems_4194304__repeat_16
TESTS += ops_4096__n-elems_4194304__repeat_4
TESTS += ops_16384__n-elems_4194304__repeat_1
