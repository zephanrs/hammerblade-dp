# nw/naive tests.mk — nw_seqlen_fast launch (4 rows)
#
# Aligns with EXPERIMENTS.md §3 (nw_seqlen_fast): seq_len ∈ {32, 64, 128, 256}.
# Row-by-row 1D systolic NW that stores the FULL O(n²) DP matrix in DRAM
# per sequence per repeat — heavily memory-bound.
#
# Per-pod DRAM dp_matrix per repeat = num_seq × (seq_len+1)² × 4 bytes.
# At the per-pod BW ceiling ≈ 0.76 GB/s (vvadd-measured), wall time is
# DRAM-write-dominated for these sizes.
#
# num_seq below uses the existing tests.mk "large anti-cliff under FASTA"
# row (smaller than nw/baseline / nw/efficient because dp_matrix size
# scales with seq_len² and per-pod DRAM working set caps it).
#
# ── HARDWARE CLIFF ───────────────────────────────────────────────────────────
# Same per-iter DRAM-write cliff as nw/efficient — apply ODD repeats so that
# num_seq × repeat is never a multiple of 512.
#
# Magnitudes are MODEL ESTIMATES (BW-bound: t_rep = bytes/0.76 GB/s):
#   seq_len  num_seq  bytes/rep   est t/rep   repeat   est wall
#        32     8176    35.6 MB    47 ms       511     ~24 s
#        64     4080    68.7 MB    90 ms       255     ~23 s
#       128     2032   134.8 MB   177 ms       127     ~22 s
#       256     1008   266.4 MB   350 ms        63     ~22 s
#
# If first HW run lands materially off (>40 s or <10 s), rescale repeat.
# Most likely cause of deviation: nw/naive's full-matrix write pattern
# may sustain less than the vvadd-measured 0.76 GB/s (different stride
# and working-set behavior).

TESTS += seq-len_32__num-seq_8176__repeat_511
TESTS += seq-len_64__num-seq_4080__repeat_255
TESTS += seq-len_128__num-seq_2032__repeat_127
TESTS += seq-len_256__num-seq_1008__repeat_63
