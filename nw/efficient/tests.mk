# nw/efficient: Hirschberg divide-and-conquer NW on 1D systolic array.
#
# DMEM budget = 4096 bytes.  Per-core allocations:
#   boundary_scores[SEQ_LEN+1]            → (SEQ_LEN+1) × 4 bytes  ← dominant
#   dp_row_even/odd[LOCAL_DP_WORDS=289]   → 2 × 289 × 4 = 2312 bytes  (fixed)
#   fill_row_even/odd[REF_CORE+1]         → 2 × (REF_CORE+1) × 4 bytes
#   split_points[REF_CORE]                → REF_CORE × 4 bytes
#   ref_segment[REF_CORE+1]               → REF_CORE+1 bytes
#   local_task_stack, mailboxes, overhead → ~200 bytes
#
#   REF_CORE = SEQ_LEN / bsg_tiles_Y = SEQ_LEN / 8
#
#   seq_len  REF_CORE  boundary  fill_rows  split  refbuf  fixed+other  TOTAL   status
#        32         4       132        40     16       5       ~2512      ~2705    ✓
#        64         8       260        72     32       9       ~2512      ~2885    ✓
#       128        16       516       136     64      17       ~2512      ~3245    ✓
#       256        32      1028       264    128      33       ~2512      ~3965    ✓
#       512        64      2052       520    256      65       ~2512      ~5405    ✗ OVERFLOW
#
# Max seq_len = 256.
#
# ── HARDWARE CLIFF: iters_per_column must NOT be a multiple of 32. ───────────
# iters_per_column = num_seq / bsg_tiles_X = num_seq / 16.
# So num_seq must be a multiple of 16 (barrier requirement) but NOT a multiple
# of 512.  Empirically every num_seq that's an integer multiple of 512 hangs;
# everything else passes regardless of total scale.
#
# FASTA constraint: num_seq × seq_len ≤ 1,048,576 → num_seq ≤ 1M / seq_len.
#
# Each seq_len gets a small, medium, and largest-anti-cliff-under-FASTA size:
#
#   seq_len  small    medium   largest (anti-cliff)   max iters/col
#        32     16      4080            32752 (= 32768-16)         2047
#        64     16      4080            16368 (= 16384-16)         1023
#       128     16      1008             8176 (=  8192-16)          511
#       256     16      1008             4080 (=  4096-16)          255

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

# ── seq_len=256 (DMEM max) ────────────────────────────────────────────────────
TESTS += seq-len_256__num-seq_16__repeat_1
TESTS += seq-len_256__num-seq_1008__repeat_1
TESTS += seq-len_256__num-seq_4080__repeat_1
