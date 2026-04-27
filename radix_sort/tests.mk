# Radix-sort sweep. One pod (pod 0) sorts NUM_ARR distinct SIZE-int arrays
# per kernel call; per-sort time = kernel_time / NUM_ARR.
#
# SIZE constraint: must be a multiple of 2048. Per-tile len = SIZE/128 must
# be ≥ 16 and a multiple of 16 for scan/scatter cache-line stride. SIZE <
# 2048 over-reads/writes 2× per tile and corrupts DRAM.
#
# Memory budget per pod: A = NUM_ARR * SIZE * 4 bytes, B = SIZE * 4 bytes.
# Keep NUM_ARR * SIZE ≲ 1e9 ints (≈4 GB per pod) until we know the HBM cap.
#
# Tuning loop: pick the smallest enabled SIZE, run with NUM_ARR = 1,
# observe wall time T. Set NUM_ARR ← round(20 / T). Re-run, confirm ~20s.
# For each next size up, scale NUM_ARR ← prev_NUM_ARR / 2 (radix sort is
# O(N), so doubling SIZE roughly halves how many fit in 20s).

# ── Active test (start here) ──────────────────────────────────────────────────
TESTS += $(call test-name,16384,1)

# ── Calibration ladder (uncomment one at a time, retune NUM_ARR per row) ─────
# Initial NUM_ARR values are placeholders — confirm on hardware before
# trusting them.
#
# TESTS += $(call test-name,2048,1)
# TESTS += $(call test-name,4096,1)
# TESTS += $(call test-name,8192,1)
# TESTS += $(call test-name,16384,1)
# TESTS += $(call test-name,32768,1)
# TESTS += $(call test-name,65536,1)
# TESTS += $(call test-name,131072,1)
# TESTS += $(call test-name,262144,1)
# TESTS += $(call test-name,524288,1)
# TESTS += $(call test-name,1048576,1)
# TESTS += $(call test-name,2097152,1)
# TESTS += $(call test-name,4194304,1)
# TESTS += $(call test-name,8388608,1)
# TESTS += $(call test-name,16777216,1)
# TESTS += $(call test-name,33554432,1)
# TESTS += $(call test-name,67108864,1)
# TESTS += $(call test-name,134217728,1)
# TESTS += $(call test-name,268435456,1)
# TESTS += $(call test-name,536870912,1)
# TESTS += $(call test-name,1073741824,1)
