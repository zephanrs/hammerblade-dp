# dummy/dram_read — pure-read DRAM bandwidth probe.
#
# Per-pod working set sizes:
#   65536   ints =   256 KB  (2× the 128 KB vcache — forces evictions)
#   262144  ints =   1   MB  (8× vcache — every pass cold)
#   1048576 ints =   4   MB
#   4194304 ints =  16   MB  (matches roofline for direct comparison)
#
# REPEAT picked so each test ≈ 2–5 s on hardware (we don't need 20 s here;
# the noise floor is small once kernel-launch overhead is amortized).
# Goal: compare per-pod bw_GB_s fast vs slow.  If bw is constant
# fast→slow, DRAM is the true ceiling.  If bw scales with clock
# (≈ 5–6× drop), the bottleneck is per-core NoC injection or vcache MSHR
# depth, not DRAM itself, and software can't fix it.

# 256 KB working set, sweep repeat
TESTS += $(call test-name,65536,1000)
TESTS += $(call test-name,65536,10000)
TESTS += $(call test-name,65536,100000)

# 1 MB working set
TESTS += $(call test-name,262144,1000)
TESTS += $(call test-name,262144,10000)

# 4 MB working set
TESTS += $(call test-name,1048576,256)
TESTS += $(call test-name,1048576,2560)

# 16 MB working set (apples-to-apples vs roofline ops_1)
TESTS += $(call test-name,4194304,128)
TESTS += $(call test-name,4194304,1280)
