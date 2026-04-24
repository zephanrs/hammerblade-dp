# dummy/roofline tests — sweep OPS_PER_ELEM to trace the full roofline.
#
# Operational intensity = 2*OPS_PER_ELEM / 8  ops/byte
#   (2 int ops per inner iteration; 4B DRAM read + 4B DRAM write per element)
#
#   ops=1:    OI=0.25   memory-bound anchor (pure streaming)
#   ops=2:    OI=0.5
#   ops=4:    OI=1.0
#   ops=8:    OI=2.0
#   ops=16:   OI=4.0
#   ops=32:   OI=8.0
#   ops=64:   OI=16.0
#   ops=128:  OI=32.0
#   ops=256:  OI=64.0
#   ops=512:  OI=128.0   well into compute-bound territory
#   ops=1024: OI=256.0   compute-bound ceiling anchor
#
# n_elems=1048576 (4 MB) keeps us well above the 64 kB cache at all OI points.
# repeat=4 extends low-OI (fast) runs so timing is accurate.
#
# SW/1d comparison: at seq_len=256 OI≈320 ops/byte, so ops=1280 matches it.
# Use the ops=512 and ops=1024 points to bracket the SW compute roof.

TESTS += ops_1__n-elems_1048576__repeat_4
TESTS += ops_2__n-elems_1048576__repeat_4
TESTS += ops_4__n-elems_1048576__repeat_4
TESTS += ops_8__n-elems_1048576__repeat_4
TESTS += ops_16__n-elems_1048576__repeat_4
TESTS += ops_32__n-elems_1048576__repeat_4
TESTS += ops_64__n-elems_1048576__repeat_4
TESTS += ops_128__n-elems_1048576__repeat_4
TESTS += ops_256__n-elems_1048576__repeat_4
TESTS += ops_512__n-elems_1048576__repeat_4
TESTS += ops_1024__n-elems_1048576__repeat_4
