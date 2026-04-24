# Sizes for correctness checks (small, fast)
TESTS += $(call test-name,16384)
TESTS += $(call test-name,65536)

# Sizes for DRAM bandwidth / performance measurement.
# 1M ints = 4 MB (>128 kB per pod, exercises DRAM bandwidth)
TESTS += $(call test-name,1048576)
# 4M ints = 16 MB
TESTS += $(call test-name,4194304)
