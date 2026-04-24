# sw/2d: 2D systolic Smith-Waterman.
# Each tile owns a QRY_CORE × REF_CORE DMEM submatrix; DMEM holds TWO double-buffered
# buffer_t structs.  Budget = 4096 bytes.
#
#   seq-len  QRY  REF   1×buf    2×buf   status
#        32    4    2     ~80 B   ~160 B  ✓
#        64    8    4    ~180 B   ~360 B  ✓
#       128   16    8    ~612 B  ~1224 B  ✓
#       192   24   12   ~1300 B  ~2600 B  ✓
#       256   32   16   ~2340 B  ~4680 B  ✗ OVERFLOW
#
# Max seq_len = 192.
#
# Timing baseline (measured on hardware):
#   seq-len=128, num-seq=8192, repeat=16 → 0.759s
#   → time ∝ repeat × seq_len (with num_seq×seq_len = 1MB constant)
#   → to get ~20s: repeat = 20 / (0.759/16 × seq_len/128)
#
# FASTA constraint: num_seq × seq_len ≤ 1,048,576 bytes → num_seq = 1M / seq_len

# --- Sequence-length sweep (shared data) ---
#
# Target: num_seq × repeat × seq_len² ≈ 70 billion cells → ~20s at ~3.5 GCUPS (measured).
# num_seq = floor(1M / seq_len), capped to keep all pods within FASTA.
#
#   seq_len  num_seq  repeat   cells (×10⁹)  est_time
#        32    32768    2048       68.7        ~20s
#        64    16384    1024       68.7        ~20s
#       128     8192     512       68.7        ~20s  ← calibrated from hardware (18.7s at 4096rep/1024seq)
#       192     4096     512       77.3        ~22s
#
TESTS += seq-len_32__num-seq_32768__repeat_2048
TESTS += seq-len_64__num-seq_16384__repeat_1024
TESTS += seq-len_128__num-seq_8192__repeat_512
TESTS += seq-len_192__num-seq_4096__repeat_512

# --- Shared vs unique pod data (A/B pairs) ---
# num_seq chosen so pod-unique slice fits in FASTA: num_seq × num_pods × seq_len ≤ 1MB
#   seq-len=128: num_seq = 1024  (1024×8×128 = 1MB)
#   seq-len=192: num_seq = 640   (640×8×192 = 983040 ≤ 1MB)
# Repeat scaled to hit ~24s (fewer sequences → higher repeat).
TESTS += seq-len_128__num-seq_1024__repeat_4096
TESTS += seq-len_128__num-seq_1024__repeat_4096__pod-unique-data_1
TESTS += seq-len_192__num-seq_640__repeat_4096
TESTS += seq-len_192__num-seq_640__repeat_4096__pod-unique-data_1
