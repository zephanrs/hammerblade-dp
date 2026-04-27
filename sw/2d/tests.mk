# sw/2d: 2D systolic Smith-Waterman, boundary-only DP (single buffer).
#
# Each tile only stores its rightmost dp column + bottommost dp row;
# the interior is computed on the fly with a 1D rolling row.  DMEM is
# now O(QRY + REF) per tile instead of O(QRY × REF), so seq_len caps at
# ~2048 (4 KB DMEM budget) instead of 192:
#
#   seq-len  QRY  REF   buf    +working+left_col   total
#        32    4    2    ~96 B    ~36 B            ~132 B   ✓
#       128   16    8    ~292 B   ~136 B           ~428 B   ✓
#       192   24   12    ~432 B   ~204 B           ~636 B   ✓
#       512   64   32   ~1144 B   ~528 B          ~1672 B   ✓
#      1024  128   64   ~2244 B  ~1024 B          ~3268 B   ✓
#      2048  256  128   ~4444 B          —          ~3520 B  ← single buffer
#      2304  288  144                                ~3952 B  borderline
#      2560  320  160                                ~4384 B  ✗ OVERFLOW
#
# Max seq_len ≈ 2048.  Pre-rewrite max was 192 (full submatrix
# double-buffered).
#
# Timing baseline (measured on hardware):
#   seq-len=128, num-seq=8192, repeat=16 → 0.759s
#   → time ∝ repeat × seq_len (with num_seq×seq_len = 1MB constant)
#   → to get ~20s: repeat = 20 / (0.759/16 × seq_len/128)
#
# FASTA constraint: num_seq × seq_len ≤ 1,048,576 bytes → num_seq = 1M / seq_len

# --- Sequence-length sweep (shared data) ---
#
# Timing NOTE: O(n²) scaling ONLY holds when compute-bound (seq_len ≥ 128).
# For short sequences the systolic pipeline startup/drain dominates:
#   overhead ≈ QRY_CORE + REF_CORE steps vs QRY_CORE × REF_CORE useful cells.
#   At seq_len=32: 4×2=8 cells vs 6 wasted steps → 75% overhead → 4× GCUPS drop.
#
# Repeats for seq_len=32,64 are calibrated from MEASURED hardware runs:
#   seq_len=32: repeat=2048 → 85.4s → repeat=512 → 85.4×(512/2048)=21.4s
#   seq_len=64: repeat=1024 → 36.3s → repeat=512 → 36.3×(512/1024)=18.2s
#
#   seq_len  num_seq  repeat   cells (×10⁹)  measured/est
#        32    32768     512       17.2        ~21s  (measured: 85.4s at rep=2048)
#        64    16384     512       34.4        ~18s  (measured: 36.3s at rep=1024)
#       128     8192     512       68.7        ~22s  (measured: 22.2s)
#       192     4096     512       77.3        ~22s  (measured: 22.1s)
#
TESTS += seq-len_32__num-seq_32768__repeat_512
TESTS += seq-len_64__num-seq_16384__repeat_512
TESTS += seq-len_128__num-seq_8192__repeat_512
TESTS += seq-len_192__num-seq_4096__repeat_512

# --- Boundary-only ceiling smoke tests ---
# repeat=1, num_seq=16 (one per X-column).  Tiny runs to verify
# correctness at each new seq_len.  Once these pass, scale repeat /
# num_seq up to ~20 s constant-cells per row (see sw/1d's tests.mk
# for the formula: cells = num_seq × repeat × seq_len² → target ~70 G).
# FASTA constraint: num_seq × seq_len ≤ 1 MB, so num_seq = 1M / seq_len.
TESTS += seq-len_256__num-seq_16__repeat_1
TESTS += seq-len_512__num-seq_16__repeat_1
TESTS += seq-len_1024__num-seq_16__repeat_1
TESTS += seq-len_1536__num-seq_16__repeat_1
TESTS += seq-len_2048__num-seq_16__repeat_1

# --- Shared vs unique pod data (A/B pairs) ---
# num_seq chosen so pod-unique slice fits in FASTA: num_seq × num_pods × seq_len ≤ 1MB
#   seq-len=128: num_seq = 1024  (1024×8×128 = 1MB)
#   seq-len=192: num_seq = 640   (640×8×192 = 983040 ≤ 1MB)
# Repeat scaled to hit ~24s (fewer sequences → higher repeat).
TESTS += seq-len_128__num-seq_1024__repeat_4096
TESTS += seq-len_128__num-seq_1024__repeat_4096__pod-unique-data_1
TESTS += seq-len_192__num-seq_640__repeat_4096
TESTS += seq-len_192__num-seq_640__repeat_4096__pod-unique-data_1
