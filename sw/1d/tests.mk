# sw/1d tests.mk
#
# DMEM budget: refbuf + H1 + H2 = 9*(REF_CORE+1) + 20 bytes < 4096
#   → REF_CORE = SEQ_LEN / CORES_PER_GROUP ≤ 454
#
# FASTA constraint: num_seq × seq_len ≤ 1,048,576 bytes → num_seq = 1M / seq_len
#
# Timing baseline (sw/2d measured ~0.76s with 16 repeats at seq-len=128/num-seq=8192).
# sw/1d has similar cell throughput.  To reach ≥15s: multiply repeats by ~20×.
# Constant-work formula: time ∝ repeat × seq_len (num_seq × seq_len = 1MB fixed).
# Targeting ~20-25s per test.
#
# Repeat table (repeat = 2048/seq_len × 20, rounded to power of 2):
#   seq_len   old_repeat   new_repeat   est_time
#        32         64        1024        ~20s
#        64         32         512        ~20s
#       128         16         256        ~20s
#       256          8         128        ~20s
#       512          4          64        ~20s
#      1024          2          32        ~20s
#      2048          1          16        ~20s

# ── Min-CPG constant-work sweep ───────────────────────────────────────────────
# CPG = smallest power-of-2 so REF_CORE = seq_len/CPG ≤ 256 (DMEM limit).
#
#   seq_len  num_seq  repeat  cpg  REF_CORE  NUM_GROUPS
#        32    32768    1024    1        32         128
#        64    16384     512    1        64         128
#       128     8192     256    1       128         128
#       256     4096     128    1       256         128
#       512     2048      64    2       256          64
#      1024     1024      32    4       256          32
#      2048      512      16    8       256          16
TESTS += seq-len_32__num-seq_32768__repeat_1024__cpg_1
TESTS += seq-len_64__num-seq_16384__repeat_512__cpg_1
TESTS += seq-len_128__num-seq_8192__repeat_256__cpg_1
TESTS += seq-len_256__num-seq_4096__repeat_128__cpg_1
TESTS += seq-len_512__num-seq_2048__repeat_64__cpg_2
TESTS += seq-len_1024__num-seq_1024__repeat_32__cpg_4
TESTS += seq-len_2048__num-seq_512__repeat_16__cpg_8

# ── Fixed CPG=8 constant-work sweep (default, 16 groups) ─────────────────────
TESTS += seq-len_32__num-seq_32768__repeat_1024
TESTS += seq-len_64__num-seq_16384__repeat_512
TESTS += seq-len_128__num-seq_8192__repeat_256
TESTS += seq-len_256__num-seq_4096__repeat_128
TESTS += seq-len_512__num-seq_2048__repeat_64
TESTS += seq-len_1024__num-seq_1024__repeat_32
TESTS += seq-len_2048__num-seq_512__repeat_16

# ── Fixed CPG=16 constant-work sweep (8 groups, pipelines span 2 X columns) ──
TESTS += seq-len_32__num-seq_32768__repeat_1024__cpg_16
TESTS += seq-len_64__num-seq_16384__repeat_512__cpg_16
TESTS += seq-len_128__num-seq_8192__repeat_256__cpg_16
TESTS += seq-len_256__num-seq_4096__repeat_128__cpg_16
TESTS += seq-len_512__num-seq_2048__repeat_64__cpg_16
TESTS += seq-len_1024__num-seq_1024__repeat_32__cpg_16
TESTS += seq-len_2048__num-seq_512__repeat_16__cpg_16

# ── All-128-cores-on-one-sequence (CPG=128) ───────────────────────────────────
TESTS += seq-len_256__num-seq_4096__repeat_128__cpg_128
TESTS += seq-len_512__num-seq_2048__repeat_64__cpg_128
TESTS += seq-len_1024__num-seq_1024__repeat_32__cpg_128
TESTS += seq-len_2048__num-seq_512__repeat_16__cpg_128

# ── Shared vs unique pod data at seq-len=256 (A/B comparison) ────────────────
# num_seq=512 so each pod's unique slice fits FASTA (512×8×256 = 1MB).
TESTS += seq-len_256__num-seq_512__repeat_128
TESTS += seq-len_256__num-seq_512__repeat_128__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_128__cpg_1
TESTS += seq-len_256__num-seq_512__repeat_128__cpg_1__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_128__cpg_16
TESTS += seq-len_256__num-seq_512__repeat_128__cpg_16__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_128__cpg_128
TESTS += seq-len_256__num-seq_512__repeat_128__cpg_128__pod-unique-data_1
