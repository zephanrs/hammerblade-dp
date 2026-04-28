# nw/naive tests.mk — nw_seqlen_fast launch (4 rows)
#
# Aligns with EXPERIMENTS.md §3 (nw_seqlen_fast): seq_len ∈ {32, 64, 128, 256}.
# Row-by-row 1D systolic NW that stores the FULL O(n²) DP matrix in DRAM
# per sequence per repeat — heavily memory-bound.
#
# ── Calibrated from HW (2026-04-27) ──────────────────────────────────────────
# Two-point measurement on real hardware:
#   seq_len=32,  num_seq=8176, repeat=16 → 1.030 s
#   seq_len=128, num_seq=2032, repeat=1  → 0.297 s
# Linear fit  T(R) = T_o + R × c × cells_per_rep,  cells = num_seq × (seq_len+1)²:
#   c ≈ 6.75 ns/cell (per-cell cost: DP compute + per-iter DRAM write)
#   T_o ≈ 0.07 s (per-test fixed kernel overhead)
#
# The earlier vvadd-based BW estimate (0.76 GB/s → ~0.18 ns/byte) was way too
# optimistic — nw/naive's per-cell DRAM writes hit vcache misses, not the
# streaming pattern vvadd measures.
#
# ── Cliff (per-iter DRAM writes) ─────────────────────────────────────────────
# num_seq × repeat must NOT be a multiple of 512.  Odd repeats × num_seq with
# only a 2^4 factor → product never reaches 2^9.  All four rows below verified
# cliff-safe.
#
#   seq_len  num_seq  cells/rep  T_rep    repeat   est wall
#        32     8176      8.9M    60 ms      331    19.9 s
#        64     4080     17.2M   116 ms      171    19.9 s
#       128     2032     33.8M   228 ms       87    19.9 s
#       256     1008     66.6M   449 ms       43    19.4 s

TESTS += seq-len_32__num-seq_8176__repeat_331
TESTS += seq-len_64__num-seq_4080__repeat_171
TESTS += seq-len_128__num-seq_2032__repeat_87
TESTS += seq-len_256__num-seq_1008__repeat_43
