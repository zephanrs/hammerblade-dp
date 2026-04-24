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
# Timing: O(n²) scaling holds.  Target 70 billion cells → ~20s at ~3.5 GCUPS.
#   repeat = 70000 / seq_len   (num_seq × seq_len = 1MB fixed via FASTA)
#
#   seq_len  num_seq  repeat   cells (×10⁹)  est_time
#        32    32768    2048       68.7        ~20s
#        64    16384    1024       68.7        ~20s
#       128     8192     512       68.7        ~20s
#       256     4096     256       68.7        ~20s
#
# Smoke-test first: repeat=1 to verify each size runs (and that the
# inter-sequence hang fix actually holds on hardware). Scale up after.
TESTS += seq-len_32__num-seq_32768__repeat_1
TESTS += seq-len_64__num-seq_16384__repeat_1
TESTS += seq-len_128__num-seq_8192__repeat_1
TESTS += seq-len_256__num-seq_4096__repeat_1
