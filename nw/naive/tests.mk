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
#   seq_len  REF_CORE  DMEM (bytes)  status
#       256        32           480    ✓
#       512        64           750    ✓
#      1024       128          1320    ✓
#      2048       256          2483    ✓ (max)
#
# DRAM constraint: dp_matrix = num_seq × (seq_len+1)² × 4 bytes per pod.
#   Keep ≤ ~256 MB per pod (conservative).
#
#   seq_len  max_num_seq  used_num_seq  dp_matrix_MB
#        32        57000          2048           8.9
#        64        15000          1024          17.3
#       128         3800           512          34.0
#       256          950           256          67.9
#       512          240           128         135.3
#      1024           60            64         268.9  (border — keep as-is)
#      2048           15            32         537.0  ← risky; may OOM on some configs
#
# NOTE: nw/naive is DRAM-bandwidth-bound (writes (seq_len+1)² ints per sequence).
#   Repeats below are estimated for ~20s; calibrate after first run.
#   Expected GCUPS will be much lower than nw/baseline due to BW cost.
#
# NOTE: host verification reads back the full dp_matrix (DMA cost grows as seq_len²).
#   For seq_len=1024/2048 the DMA read may take >1 min — this is a one-time cost.
#
# Smoke-test first: repeat=1 to verify each size runs at all within the 60s
# timeout. Once timings are known, scale up toward ~20s runs.
TESTS += seq-len_32__num-seq_2048__repeat_1
TESTS += seq-len_64__num-seq_1024__repeat_1
TESTS += seq-len_128__num-seq_512__repeat_1
TESTS += seq-len_256__num-seq_256__repeat_1
TESTS += seq-len_512__num-seq_128__repeat_1
TESTS += seq-len_1024__num-seq_64__repeat_1
TESTS += seq-len_2048__num-seq_32__repeat_1
