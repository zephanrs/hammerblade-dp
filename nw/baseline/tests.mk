# nw/baseline: row-by-row 1D systolic NW.
#
# DMEM budget = 4096 bytes.  Per-core allocations (identical to sw/1d):
#   refbuf[REF_CORE+1] + H1[REF_CORE+1] + H2[REF_CORE+1]  → 9×(REF_CORE+1) bytes
#   mailbox + ready flag + overhead                          → ~50 bytes
#
#   REF_CORE = SEQ_LEN / 8
#   At seq_len=2048: 9×257 + 50 = 2363 bytes < 4096  →  all seq_len safe.
#
# Timing: O(n²) scaling.  Target 70 billion cells → ~20s at ~3.5 GCUPS.
#   repeat = 70000 / seq_len   (num_seq × seq_len = 1MB fixed via FASTA)
#
#   seq_len  num_seq  repeat   cells (×10⁹)  est_time
#        32    32768    2048       68.7        ~20s
#        64    16384    1024       68.7        ~20s
#       128     8192     512       68.7        ~20s
#       256     4096     256       68.7        ~20s
#       512     2048     128       68.7        ~20s
#      1024     1024      64       68.7        ~20s
#      2048      512      32       68.7        ~20s
#
TESTS += seq-len_32__num-seq_32768__repeat_2048
TESTS += seq-len_64__num-seq_16384__repeat_1024
TESTS += seq-len_128__num-seq_8192__repeat_512
TESTS += seq-len_256__num-seq_4096__repeat_256
TESTS += seq-len_512__num-seq_2048__repeat_128
TESTS += seq-len_1024__num-seq_1024__repeat_64
TESTS += seq-len_2048__num-seq_512__repeat_32
