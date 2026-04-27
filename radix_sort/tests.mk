# radix_sort has no repeat knob — the analog of "smoke test at repeat=1" is
# to start with just the smallest correctness size to verify the kernel runs
# at all within the 60s timeout. Uncomment the larger sizes after.

# SIZE must be a multiple of 2048: scan/scatter stride by 16-int cache lines,
# so per-tile len = SIZE/128 must be ≥ 16 and a multiple of 16. SIZE < 2048
# causes scatter to over-read and over-write 2× per tile → DRAM corruption.
TESTS += $(call test-name,2048)
TESTS += $(call test-name,4096)
TESTS += $(call test-name,16384)
# TESTS += $(call test-name,65536)

# Sizes for DRAM bandwidth / performance measurement.
# 1M ints = 4 MB (>128 kB per pod, exercises DRAM bandwidth)
# TESTS += $(call test-name,1048576)
# 4M ints = 16 MB
# TESTS += $(call test-name,4194304)
