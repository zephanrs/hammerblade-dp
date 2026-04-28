# dummy/barrier_bench tests.mk — barrier_fast launch (2 rows)
#
# One run per barrier.  HW measurement at N=1M: both barriers come in at
# ~1.34 µs/barrier (identical) — the simulation showed linear ~6× faster,
# but on real silicon both implementations bottleneck on the same
# bsg_barrier_amoadd atomic unit, so the algorithmic post-wait difference
# (tree vs linear) is masked.
#
# N=15M targets ~20 s wall at the measured 1.34 µs/barrier.
#   time-per-barrier = wall_time / N

TESTS += $(call test-name,default,15000000)
TESTS += $(call test-name,linear,15000000)
