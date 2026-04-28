# dummy/barrier_bench tests.mk — barrier_fast launch (2 rows)
#
# One run per barrier.  N=1M each — short wall (~1 s expected) but the
# Cudalite µs counter gives sub-microsecond precision per barrier, so
# accuracy isn't the issue.  Re-tune N upward if we want longer runs.
#   time-per-barrier = wall_time / N

TESTS += $(call test-name,default,1000000)
TESTS += $(call test-name,linear,1000000)
