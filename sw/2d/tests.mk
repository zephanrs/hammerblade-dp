# sw/2d: 2D systolic Smith-Waterman.
# All 16×8 = 128 tiles operate as one anti-diagonal wavefront; each tile owns a
# QRY_CORE×REF_CORE submatrix.  dp array lives in DMEM: (QRY_CORE+1)*(REF_CORE+1)*4 bytes.
#
# DMEM budget (4 KB):
#   seq-len=32:  QRY=4,  REF=2  → 60 B   ✓
#   seq-len=64:  QRY=8,  REF=4  → 180 B  ✓
#   seq-len=128: QRY=16, REF=8  → 612 B  ✓
#   seq-len=256: QRY=32, REF=16 → 2244 B ✓
#   seq-len=512: QRY=64, REF=32 → 8580 B ✗
#
# Parameters:
#   seq-len_L         = sequence length; max 256 (DMEM limit)
#   num-seq_N         = unique sequences per pod; max = 1048576 / seq-len
#   repeat_R          = repeat factor for longer runtime
#   pod-unique-data_1 = each pod gets a distinct 1/num_pods data slice

# --- Sequence-length sweep (shared data, N*L = 1 MB) ---
TESTS += seq-len_32__num-seq_32768__repeat_64
TESTS += seq-len_64__num-seq_16384__repeat_32
TESTS += seq-len_128__num-seq_8192__repeat_16
TESTS += seq-len_256__num-seq_4096__repeat_8

# --- Shared vs unique pod data (A/B pairs) ---
# num-seq=512 at seq-len=256 keeps each pod's slice to 128 kB (< 1 MB / 8 pods).
TESTS += seq-len_128__num-seq_1024__repeat_16
TESTS += seq-len_128__num-seq_1024__repeat_16__pod-unique-data_1
TESTS += seq-len_256__num-seq_512__repeat_8
TESTS += seq-len_256__num-seq_512__repeat_8__pod-unique-data_1
