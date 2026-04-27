# Radix-sort sweep. One pod (pod 0) sorts NUM_ARR distinct SIZE-int arrays
# per kernel call; per-sort time = kernel_time / NUM_ARR.
#
# SIZE constraint: must be a multiple of 2048. Per-tile len = SIZE/128 must
# be ≥ 16 and a multiple of 16 for scan/scatter cache-line stride.
#
# Calibration on hardware (1 pod, fast clock):
#
#   SIZE     measured       per-sort   per-element regime
#   16384    192 µs/sort    11.7 ns    vcache (linear)
#   32768    437 µs/sort    13.3 ns    vcache (linear)
#   65536   4823 µs/sort    73.6 ns    post-cliff (>32K ints overflows 128 KB vcache)
#
#   Below the cliff (SIZE ≤ 32K ints), sorting is fast because every chunk
#   fits in the per-pod vcache.  Above 64K ints the sort becomes
#   DRAM-bound and per-element cost jumps ~6×.  Both regimes appear linear
#   in SIZE within their own band.
#
# NUM_ARR strategy: keep the input buffer at ~1 GB per pod (NUM_ARR * SIZE =
# 256M ints) for every test.  This:
#   - lands the post-cliff sizes at ~20s (target) since per-sort cost
#     scales with SIZE and the elem count per buffer is constant;
#   - leaves vcache-regime sizes at ~3–4s, which is still 16K–131K sorts
#     averaged — plenty of statistical coverage.
#
#   SIZE         NUM_ARR     est_time  buffer
#   2048         131072       3.15 s   1 GB
#   4096          65536       3.15 s   1 GB
#   8192          32768       3.15 s   1 GB
#   16384         16384       3.15 s   1 GB
#   32768          8192       3.58 s   1 GB
#   65536          4096      19.76 s   1 GB     ← cliff
#   131072         2048      19.76 s   1 GB
#   262144         1024      19.76 s   1 GB
#   524288          512      19.76 s   1 GB
#   1048576         256      19.76 s   1 GB
#   2097152         128      19.76 s   1 GB
#   4194304          64      19.76 s   1 GB
#   8388608          32      19.76 s   1 GB
#   16777216         16      19.76 s   1 GB
#   33554432          8      19.76 s   1 GB
#   67108864          4      19.76 s   1 GB
#   134217728         2      19.76 s   1 GB
#   268435456         1      19.76 s   1 GB
#
# Total estimated wall time: ~4.5 min for the full 18-test sweep.
# If 1 GB per buffer is too tight for HBM, halve every NUM_ARR; if there's
# room to spare, double the vcache-regime rows to push them toward 20s
# (memory will rise to ~2 GB for those sizes).

TESTS += $(call test-name,2048,131072)
TESTS += $(call test-name,4096,65536)
TESTS += $(call test-name,8192,32768)
TESTS += $(call test-name,16384,16384)
TESTS += $(call test-name,32768,8192)
TESTS += $(call test-name,65536,4096)
TESTS += $(call test-name,131072,2048)
TESTS += $(call test-name,262144,1024)
TESTS += $(call test-name,524288,512)
TESTS += $(call test-name,1048576,256)
TESTS += $(call test-name,2097152,128)
TESTS += $(call test-name,4194304,64)
TESTS += $(call test-name,8388608,32)
TESTS += $(call test-name,16777216,16)
TESTS += $(call test-name,33554432,8)
TESTS += $(call test-name,67108864,4)
TESTS += $(call test-name,134217728,2)
TESTS += $(call test-name,268435456,1)
