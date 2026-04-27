# nw/baseline tests.mk — nw_seqlen_fast launch (4 rows)
#
# Aligns with EXPERIMENTS.md §3 (nw_seqlen_fast): seq_len ∈ {32, 64, 128, 256}.
#
# DMEM budget (per core):
#   refbuf[REF_CORE+1] + H1[REF_CORE+1] + H2[REF_CORE+1]  → 9×(REF_CORE+1) bytes
#   mailbox + ready flag + overhead                       → ~50 bytes
#   REF_CORE = SEQ_LEN / bsg_tiles_Y = SEQ_LEN / 8
#
# nw/baseline writes only output[output_idx] (one int per sequence per repeat),
# so it does NOT trigger the per-iter-DRAM-write cliff that hits nw/efficient.
# Power-of-2 repeats are safe.  Anti-cliff num_seq (1M/seq_len − 16) is used
# anyway for cross-app consistency.
#
# Repeats copied from sw/1d (compute pattern is identical):
#   cells/pod = num_seq × repeat × seq_len² ≈ 68.6 G → ~20 s at ~3.5 GCUPS/pod.

TESTS += seq-len_32__num-seq_32752__repeat_2048
TESTS += seq-len_64__num-seq_16368__repeat_1024
TESTS += seq-len_128__num-seq_8176__repeat_512
TESTS += seq-len_256__num-seq_4080__repeat_256
