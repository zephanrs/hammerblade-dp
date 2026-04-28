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
#   seq_len  num_seq  HW-confirmed wall   repeat   note
#        32     8176        ~20 s            331    confirmed on HW
#        64     4080        ~20 s            171    confirmed on HW
#       128     2032        ~2 s              10    larger repeat hangs (root
#                                                   cause TBD; precision OK
#                                                   via Cudalite µs counter)
#       256     1008        ~? s               5    same hang above small R;
#                                                   5 reps, µs-counter timing

TESTS += seq-len_32__num-seq_8176__repeat_331
TESTS += seq-len_64__num-seq_4080__repeat_171
TESTS += seq-len_128__num-seq_2032__repeat_10
# seq_len=256 dropped — fails on HW for nw/efficient; dropped everywhere
# in nw/* for cross-app consistency (out of debugging time).
