# radix_sort has no repeat knob — the analog of "smoke test at repeat=1" is
# to start with just the smallest correctness size to verify the kernel runs
# at all within the 60s timeout. Uncomment the larger sizes after.

TESTS += $(call test-name,16384)
# TESTS += $(call test-name,65536)

# Sizes for DRAM bandwidth / performance measurement.
# 1M ints = 4 MB (>128 kB per pod, exercises DRAM bandwidth)
# TESTS += $(call test-name,1048576)
# 4M ints = 16 MB
# TESTS += $(call test-name,4194304)
