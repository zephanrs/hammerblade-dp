# nw/baseline: row-by-row 1D systolic NW.  Output is one int per sequence.
#
# DMEM budget = 4096 bytes.  Per-core allocations (identical to sw/1d):
#   refbuf[REF_CORE+1] + H1[REF_CORE+1] + H2[REF_CORE+1]  → 9×(REF_CORE+1) bytes
#   mailbox + ready flag + overhead                          → ~50 bytes
#
#   REF_CORE = SEQ_LEN / 8
#
# ── HARDWARE CLIFF ───────────────────────────────────────────────────────────
# Per the nw/efficient bisect: kernels with per-iteration DRAM writes hang
# when iters_per_column (= num_seq / 16) is a multiple of 32.  nw/baseline's
# only DRAM write is output[output_idx], one int per sequence per pod, which
# does NOT seem to trigger the cliff (we ran num_seq=32768 = iters/col=2048
# = multiple of 32, and it passed).  Still, picking anti-cliff num_seq costs
# nothing and keeps the three nw/* apps comparable.
#
# Sizes per seq_len match nw/efficient (so this is a clean baseline):
#
#   seq_len  small   medium   large
#        32     16     4080    32752
#        64     16     4080    16368
#       128     16     1008     8176
#       256     16     1008     4080
#
# repeat=1 for the calibration pass; scale up to ~20s/test once kernel_us
# is measured.

# ── seq_len=32 ────────────────────────────────────────────────────────────────
TESTS += seq-len_32__num-seq_16__repeat_1
TESTS += seq-len_32__num-seq_4080__repeat_1
TESTS += seq-len_32__num-seq_32752__repeat_1

# ── seq_len=64 ────────────────────────────────────────────────────────────────
TESTS += seq-len_64__num-seq_16__repeat_1
TESTS += seq-len_64__num-seq_4080__repeat_1
TESTS += seq-len_64__num-seq_16368__repeat_1

# ── seq_len=128 ───────────────────────────────────────────────────────────────
TESTS += seq-len_128__num-seq_16__repeat_1
TESTS += seq-len_128__num-seq_1008__repeat_1
TESTS += seq-len_128__num-seq_8176__repeat_1

# ── seq_len=256 ───────────────────────────────────────────────────────────────
TESTS += seq-len_256__num-seq_16__repeat_1
TESTS += seq-len_256__num-seq_1008__repeat_1
TESTS += seq-len_256__num-seq_4080__repeat_1
