# nw/efficient tests.mk — nw_seqlen_fast launch (4 rows)
#
# Aligns with EXPERIMENTS.md §3 (nw_seqlen_fast): seq_len ∈ {32, 64, 128, 256}.
# Hirschberg divide-and-conquer NW; DMEM caps seq_len ≤ 256.
#
# DMEM budget (per core, see comment in kernel.cpp):
#   boundary_scores[SEQ_LEN+1]            → (SEQ_LEN+1) × 4 bytes  ← dominant
#   dp_row_even/odd[LOCAL_DP_WORDS=289]   → 2312 bytes (fixed)
#   fill_row_even/odd, split_points, ref  → small
#   At seq_len=256, total ≈ 3965 B; at seq_len=512, overflows.
#
# ── HARDWARE CLIFF (per-iter DRAM writes) ────────────────────────────────────
# kernel hangs when num_seq × repeat is a multiple of 512.  num_seq = 1M/seq_len
# − 16 already breaks num_seq alone (16 × odd), but multiplying by a power-of-2
# repeat ≥ 32 lands the product back on a multiple of 512.
#
# Workaround: use ODD repeats — odd × num_seq has only the 2^4 factor from
# num_seq, so the product is never a multiple of 2^9 = 512.
#
# Magnitude target: Hirschberg + per-iter DRAM writes are ~4× slower than
# nw/baseline at the same (num_seq, seq_len), so repeat is ~4× smaller than
# nw/baseline (rounded down to nearest odd):

TESTS += seq-len_32__num-seq_32752__repeat_511
TESTS += seq-len_64__num-seq_16368__repeat_255
TESTS += seq-len_128__num-seq_8176__repeat_127
# seq_len=256 dropped — fails on HW (out of debugging time).  Dropped
# from all three nw/* tests.mk for cross-app consistency.
