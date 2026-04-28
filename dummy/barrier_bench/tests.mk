# dummy/barrier_bench tests.mk — barrier_fast launch (2 rows)
#
# Per EXPERIMENTS.md §6: one run per barrier, tuned for ~20 s wall.
#   time-per-barrier = wall_time / N
#
# N picked from the rough cluster latencies:
#   default barrier ≈ 1 µs  → N = 20,000,000 → ~20 s
#   linear  barrier ≈ 5 µs  → N =  4,000,000 → ~20 s
# (If actual µs/barrier is materially different, re-tune N from the
# first row's CSV and run again.)

TESTS += $(call test-name,default,20000000)
TESTS += $(call test-name,linear,4000000)
