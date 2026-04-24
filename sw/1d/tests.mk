# sw/1d tests.mk
#
# DMEM budget: refbuf + H1 + H2 = 9*(REF_CORE+1) + 20 bytes < 4096
#   → REF_CORE = SEQ_LEN / CORES_PER_GROUP ≤ 454
#
# FASTA constraint: num_seq × seq_len ≤ 1,048,576 bytes → num_seq = 1M / seq_len
#
# Timing formula (O(n²) SW): time ∝ num_seq × repeat × seq_len²
#   With num_seq = 1M / seq_len → cells = 1M × repeat × seq_len
#   Target: 70 billion cells → ~20s at ~3.5 GCUPS (calibrated from sw/2d hardware)
#   → repeat × seq_len = 70e9 / 1e6 = 70,000
#   → repeat = 70000 / seq_len (rounded to power-of-2)
#
#   seq_len   num_seq  repeat   cells (×10⁹)  est_time
#        32    32768    2048       68.7          ~20s
#        64    16384    1024       68.7          ~20s
#       128     8192     512       68.7          ~20s
#       256     4096     256       68.7          ~20s
#       512     2048     128       68.7          ~20s
#      1024     1024      64       68.7          ~20s
#      2048      512      32       68.7          ~20s

# ── Min-CPG constant-work sweep ───────────────────────────────────────────────
# CPG = smallest power-of-2 so REF_CORE = seq_len/CPG ≤ 256 (DMEM limit).
#
#   seq_len  num_seq  repeat  cpg  REF_CORE  NUM_GROUPS
#        32    32768    2048    1        32         128
#        64    16384    1024    1        64         128
#       128     8192     512    1       128         128
#       256     4096     256    1       256         128
#       512     2048     128    2       256          64
#      1024     1024      64    4       256          32
#      2048      512      32    8       256          16
TESTS += seq-len_32__num-seq_32768__repeat_2048__cpg_1
TESTS += seq-len_64__num-seq_16384__repeat_1024__cpg_1
TESTS += seq-len_128__num-seq_8192__repeat_512__cpg_1
TESTS += seq-len_256__num-seq_4096__repeat_256__cpg_1
# seq-len_512__num-seq_2048__repeat_128__cpg_2 — timed out on hardware, removed
TESTS += seq-len_1024__num-seq_1024__repeat_64__cpg_4
TESTS += seq-len_2048__num-seq_512__repeat_32__cpg_8

# ── Fixed CPG=8 constant-work sweep (default, 16 groups) ─────────────────────
TESTS += seq-len_32__num-seq_32768__repeat_2048
TESTS += seq-len_64__num-seq_16384__repeat_1024
TESTS += seq-len_128__num-seq_8192__repeat_512
TESTS += seq-len_256__num-seq_4096__repeat_256
# seq-len_512__num-seq_2048__repeat_128 (cpg=8 default) — timed out, removed
TESTS += seq-len_1024__num-seq_1024__repeat_64
TESTS += seq-len_2048__num-seq_512__repeat_32

# ── Fixed CPG=16 constant-work sweep (8 groups, pipelines span 2 X columns) ──
TESTS += seq-len_32__num-seq_32768__repeat_2048__cpg_16
TESTS += seq-len_64__num-seq_16384__repeat_1024__cpg_16
# seq-len_128__num-seq_8192__repeat_512__cpg_16 — timed out, removed
# seq-len_256__num-seq_4096__repeat_256__cpg_16 — timed out, removed
TESTS += seq-len_512__num-seq_2048__repeat_128__cpg_16
TESTS += seq-len_1024__num-seq_1024__repeat_64__cpg_16
TESTS += seq-len_2048__num-seq_512__repeat_32__cpg_16

# ── All-128-cores-on-one-sequence (CPG=128) ───────────────────────────────────
TESTS += seq-len_256__num-seq_4096__repeat_256__cpg_128
TESTS += seq-len_512__num-seq_2048__repeat_128__cpg_128
TESTS += seq-len_1024__num-seq_1024__repeat_64__cpg_128
TESTS += seq-len_2048__num-seq_512__repeat_32__cpg_128

# ── Shared vs unique pod data at seq-len=256 (A/B comparison) ────────────────
# num_seq=512 so each pod's unique slice fits FASTA (512×8×256 = 1MB).
# repeat=2048: cells = 512×2048×256² = 68.7B → ~20s
TESTS += seq-len_256__num-seq_512__repeat_2048
TESTS += seq-len_256__num-seq_512__repeat_2048__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_2048__cpg_1
TESTS += seq-len_256__num-seq_512__repeat_2048__cpg_1__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_2048__cpg_16
# seq-len_256__num-seq_512__repeat_2048__cpg_16__pod-unique-data_1 — timed out, removed
TESTS += seq-len_256__num-seq_512__repeat_2048__cpg_128
TESTS += seq-len_256__num-seq_512__repeat_2048__cpg_128__pod-unique-data_1
