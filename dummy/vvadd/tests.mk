# dummy/vvadd — canonical memory-bound BW test.
#
# C[i] = A[i] + B[i] over N_ELEMS, REPEAT times.  3 arrays so working
# set = 12 × N_ELEMS bytes per pod.  Pick N_ELEMS so working set
# >> vcache (128 KB / pod) and REPEAT so each row runs ~5–15 s fast
# (slow at ~32× will land below the 600 s timeout).
#
#   N_ELEMS    array_kb  working_set_kb_per_pod
#   65536       256 KB     768 KB     ← already 6× vcache
#   262144      1   MB     3   MB
#   1048576     4   MB    12   MB     ← matches roofline scale
#   4194304    16   MB    48   MB
#
# Goal of this sweep is one fast + slow run per row to read off the
# fast/slow BW ratio.  REPEAT below targets ~5 s fast on a guess of
# 1–2 GB/s per pod; adjust if the first run reports something far off.

TESTS += $(call test-name,65536,8000)        # 1.5 GB traffic / pod
TESTS += $(call test-name,262144,2000)       # 1.5 GB
TESTS += $(call test-name,1048576,500)       # 1.5 GB — apples-to-apples vs roofline ops_1
TESTS += $(call test-name,4194304,128)       # 1.5 GB
