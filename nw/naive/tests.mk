# nw/naive: row-by-row 1D systolic NW storing the full O(n²) DP matrix in DRAM.
#
# DMEM budget = 4096 bytes.  Per-core allocations:
#   refbuf[REF_CORE+1]   → REF_CORE+1 bytes
#   H1[REF_CORE+1]       → (REF_CORE+1) × 4 bytes
#   H2[REF_CORE+1]       → (REF_CORE+1) × 4 bytes
#   mailbox + ready flag → ~20 bytes
#   stack + overhead     → ~150 bytes
#   Total ≈ 9×(REF_CORE+1) + 170,  REF_CORE = SEQ_LEN/8
#
# Per-pod DRAM dp_matrix = num_seq × (seq_len+1)² × 4 bytes.
#
# ── HARDWARE CLIFF ───────────────────────────────────────────────────────────
# Per the nw/efficient bisect: kernels hang when iters_per_column (=
# num_seq / bsg_tiles_X = num_seq / 16) is a multiple of 32.  Equivalent
# rule: num_seq must be a multiple of 16 but NOT a multiple of 512.
# (nw/baseline does NOT seem to be affected — its only DRAM write is
# output[output_idx], one int per sequence — but the rule costs nothing
# to follow, so we apply it everywhere.)
#
# nw/naive is the heaviest of the three: it writes (seq_len+1)² ints to DRAM
# per sequence per pod.  At seq_len=32 that's 4356 bytes/seq/pod; at
# seq_len=256 it's 264196 bytes/seq/pod.  num_seq sized smaller than
# nw/efficient/nw/baseline so each test still finishes in seconds.
#
# Sizes per seq_len (small / medium / largest-anti-cliff under FASTA):
#
#   seq_len  small   medium   large    iter/col @ large   dp_matrix size @ large
#        32     16     1008     8176                 511                  ~32 MB
#        64     16     1008     4080                 255                  ~67 MB
#       128     16      240     2032                 127                  ~135 MB
#       256     16      240     1008                  63                  ~266 MB
#
# repeat=1 across the board for the first calibration run; we'll pick
# repeat values targeting ~20s/test from measured kernel_us in the next pass.

# ── seq_len=32 ────────────────────────────────────────────────────────────────
TESTS += seq-len_32__num-seq_16__repeat_1
TESTS += seq-len_32__num-seq_1008__repeat_1
TESTS += seq-len_32__num-seq_8176__repeat_1

# ── seq_len=64 ────────────────────────────────────────────────────────────────
TESTS += seq-len_64__num-seq_16__repeat_1
TESTS += seq-len_64__num-seq_1008__repeat_1
TESTS += seq-len_64__num-seq_4080__repeat_1

# ── seq_len=128 ───────────────────────────────────────────────────────────────
TESTS += seq-len_128__num-seq_16__repeat_1
TESTS += seq-len_128__num-seq_240__repeat_1
TESTS += seq-len_128__num-seq_2032__repeat_1

# ── seq_len=256 ───────────────────────────────────────────────────────────────
TESTS += seq-len_256__num-seq_16__repeat_1
TESTS += seq-len_256__num-seq_240__repeat_1
TESTS += seq-len_256__num-seq_1008__repeat_1
