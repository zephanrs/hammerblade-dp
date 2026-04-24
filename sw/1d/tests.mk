# sw/1d tests.mk
#
# DMEM budget: refbuf + H1 + H2 = 9*(REF_CORE+1) + 20 bytes < 4096
#   → REF_CORE = SEQ_LEN / CORES_PER_GROUP ≤ 454
#   → max REF_CORE used here is 256 (2333 bytes, comfortable margin)
#
# FASTA constraint: num_seq * seq_len ≤ 32768 * 32 = 1,048,576 bytes
#   → num_seq = 1048576 / seq_len  (use the full file every run)
#
# Constant-work scaling: time ∝ num_seq * repeat * seq_len²
#   With num_seq * seq_len = 1 MB fixed, time ∝ repeat * seq_len
#   → repeat = 64 * 32 / seq_len  (keeps wall time ≈ constant from the baseline)
#
# ── Min-CPG constant-work sweep ───────────────────────────────────────────────
# CPG = smallest power-of-2 so that REF_CORE = seq_len/CPG ≤ 256 (DMEM limit).
# This maximises parallelism (most groups) at each length.
#
#   seq_len  num_seq  repeat  cpg  REF_CORE  NUM_GROUPS
#        32    32768      64    1        32         128  (all independent, no mailboxes)
#        64    16384      32    1        64         128
#       128     8192      16    1       128         128
#       256     4096       8    1       256         128
#       512     2048       4    2       256          64
#      1024     1024       2    4       256          32
#      2048      512       1    8       256          16
TESTS += seq-len_32__num-seq_32768__repeat_64__cpg_1
TESTS += seq-len_64__num-seq_16384__repeat_32__cpg_1
TESTS += seq-len_128__num-seq_8192__repeat_16__cpg_1
TESTS += seq-len_256__num-seq_4096__repeat_8__cpg_1
TESTS += seq-len_512__num-seq_2048__repeat_4__cpg_2
TESTS += seq-len_1024__num-seq_1024__repeat_2__cpg_4
TESTS += seq-len_2048__num-seq_512__repeat_1__cpg_8

# ── Fixed CPG=8 constant-work sweep (default, 16 groups) ─────────────────────
# Baseline; each group = one Y column (8 cores). Pipeline stays local to one column.
TESTS += seq-len_32__num-seq_32768__repeat_64
TESTS += seq-len_64__num-seq_16384__repeat_32
TESTS += seq-len_128__num-seq_8192__repeat_16
TESTS += seq-len_256__num-seq_4096__repeat_8
TESTS += seq-len_512__num-seq_2048__repeat_4
TESTS += seq-len_1024__num-seq_1024__repeat_2
TESTS += seq-len_2048__num-seq_512__repeat_1

# ── Fixed CPG=16 constant-work sweep (8 groups, pipelines span 2 X columns) ──
TESTS += seq-len_32__num-seq_32768__repeat_64__cpg_16
TESTS += seq-len_64__num-seq_16384__repeat_32__cpg_16
TESTS += seq-len_128__num-seq_8192__repeat_16__cpg_16
TESTS += seq-len_256__num-seq_4096__repeat_8__cpg_16
TESTS += seq-len_512__num-seq_2048__repeat_4__cpg_16
TESTS += seq-len_1024__num-seq_1024__repeat_2__cpg_16
TESTS += seq-len_2048__num-seq_512__repeat_1__cpg_16

# ── All-128-cores-on-one-sequence (CPG=128) ───────────────────────────────────
# NUM_GROUPS=1: all 128 cores pipeline a single sequence at a time.
# REF_CORE = seq_len/128; only makes sense for seq_len ≥ 128.
# Repeat is the same constant-work value (total cell ops unchanged).
TESTS += seq-len_256__num-seq_4096__repeat_8__cpg_128
TESTS += seq-len_512__num-seq_2048__repeat_4__cpg_128
TESTS += seq-len_1024__num-seq_1024__repeat_2__cpg_128
TESTS += seq-len_2048__num-seq_512__repeat_1__cpg_128

# ── Shared vs unique pod data at seq-len=256 (A/B comparison) ────────────────
# num_seq=512 so each pod's unique slice fits within the FASTA file (512*256=128 kB < 1 MB/8).
TESTS += seq-len_256__num-seq_512__repeat_8
TESTS += seq-len_256__num-seq_512__repeat_8__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_8__cpg_1
TESTS += seq-len_256__num-seq_512__repeat_8__cpg_1__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_8__cpg_16
TESTS += seq-len_256__num-seq_512__repeat_8__cpg_16__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_8__cpg_128
TESTS += seq-len_256__num-seq_512__repeat_8__cpg_128__pod-unique-data_1
