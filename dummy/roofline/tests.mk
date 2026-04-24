# dummy/roofline tests — sweep OPS_PER_ELEM to trace the full roofline.
#
# Operational intensity = 2*OPS_PER_ELEM / 8  ops/byte
#   (2 int ops per inner iteration; 4B read + 4B write per element)
#
# Kernel uses contiguous chunks per tile — maximizes DRAM row-buffer hits.
#
# n_elems=4194304 (16M ints = 64 MB) — well above cache.
#
# ~sqrt(2)-spaced sweep (28 points) so the curve is dense on a log axis AND
# has plenty of coverage near the knee on a linear axis.
#
# Repeats tuned for ~20s per test from measured per-repeat times:
#
#   ops   t(r=1)s   chosen repeat   est time s
#      1   0.156         128            20.0
#      2   0.153         128            19.6
#      3   0.145         128            18.5
#      4   0.153         128            19.6
#      6   0.154         128            19.7
#      8   0.154         128            19.8
#     12   0.158         128            20.2
#     16   0.155         128            19.8
#     24   0.158         128            20.2
#     32   0.157         128            20.1
#     48   0.158         128            20.3
#     64   0.156         128            20.0
#     96   0.162         128            20.7
#    128   0.161         128            20.7
#    192   0.168         120            20.1
#    256   0.178         112            19.9
#    384   0.192         104            20.0
#    512   0.217          92            19.9
#    768   0.259          77            19.9
#   1024   0.304          66            20.1
#   1536   0.389          52            20.2
#   2048   0.559          36            20.1
#   3072   0.670          30            20.1
#   4096   0.854          24            20.5
#   6144   1.226          16            19.6
#   8192   1.604          12            19.3
#  12288   2.345           9            21.1
#  16384   3.091           7            21.6

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
TESTS += ops_192__n-elems_4194304__repeat_120
TESTS += ops_256__n-elems_4194304__repeat_112
TESTS += ops_384__n-elems_4194304__repeat_104
TESTS += ops_512__n-elems_4194304__repeat_92
TESTS += ops_768__n-elems_4194304__repeat_77
TESTS += ops_1024__n-elems_4194304__repeat_66
TESTS += ops_1536__n-elems_4194304__repeat_52
TESTS += ops_2048__n-elems_4194304__repeat_36
TESTS += ops_3072__n-elems_4194304__repeat_30
TESTS += ops_4096__n-elems_4194304__repeat_24
TESTS += ops_6144__n-elems_4194304__repeat_16
TESTS += ops_8192__n-elems_4194304__repeat_12
TESTS += ops_12288__n-elems_4194304__repeat_9
TESTS += ops_16384__n-elems_4194304__repeat_7
