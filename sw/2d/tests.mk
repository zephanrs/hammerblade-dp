# sw/2d tests.mk — sw2d_seqlen_fast launch (6 rows)
#
# Aligns with EXPERIMENTS.md §2 (sw2d_seqlen_fast): seq_len ∈ {32, 64, 128,
# 256, 512, 1024} after the boundary-only DP rewrite raised the ceiling
# from 192 → 1024 (verified on HW).  seq_len=1536/2048 fail.
#
# DMEM (boundary-only single buffer, see kernel.cpp):
#   seq-len  QRY  REF   buf+working+left_col   total       status
#        32    4    2          ~132 B          ~132 B      ✓
#       128   16    8          ~428 B          ~428 B      ✓
#       192   24   12          ~636 B          ~636 B      ✓
#       512   64   32         ~1672 B         ~1672 B      ✓
#      1024  128   64         ~3268 B         ~3268 B      ✓ (verified)
#      1536/2048                                            ✗ HW failure
#
# FASTA cap: num_seq × seq_len ≤ 1 MB
#
# num_seq = (1M / seq_len) − 16 — defensive, never power of 2; same anti-
# stride pattern used across all sw/1d and nw/* rows.
#
# Repeat formula: cells/pod = num_seq × repeat × seq_len² ≈ 70 G → ~20 s.
#   repeat = round_pow2(70000 / seq_len)
#
# Note: the boundary-only kernel's per-cell cost is similar to but not
# identical to sw/1d.  If the launch's per-row wall time is materially
# off from 20 s, retune repeat with the same cells-target formula but
# scaled to the measured GCUPS.

TESTS += seq-len_32__num-seq_32752__repeat_2048
TESTS += seq-len_64__num-seq_16368__repeat_1024
TESTS += seq-len_128__num-seq_8176__repeat_512
TESTS += seq-len_256__num-seq_4080__repeat_256
TESTS += seq-len_512__num-seq_2032__repeat_128
TESTS += seq-len_1024__num-seq_1008__repeat_64
