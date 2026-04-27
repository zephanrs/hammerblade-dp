# Two barrier implementations × a few iteration counts.  Per-barrier
# latency is the same regardless of N, so we mostly want one row that's
# long enough to dominate kernel-launch overhead and one short row to
# verify the longer ones scale linearly (sanity check).
#
# A library-tree barrier on the cluster is roughly 1 µs.  A linear one
# may be 1–8 µs.  At N=1,000,000 that's 1–8 seconds — comfortably above
# kernel-launch jitter, well below the 600 s test timeout.

# Sanity-check rows (verify per-barrier scaling is linear in N):
TESTS += $(call test-name,default,1000)
TESTS += $(call test-name,linear,1000)
TESTS += $(call test-name,default,10000)
TESTS += $(call test-name,linear,10000)
TESTS += $(call test-name,default,100000)
TESTS += $(call test-name,linear,100000)
# Main comparison rows:
TESTS += $(call test-name,default,1000000)
TESTS += $(call test-name,linear,1000000)
