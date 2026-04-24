# sw/scheduling: Smith-Waterman with a centralized dynamic scheduler that
# forms variable-size teams of cores proportional to sequence length.
# Short sequences get 1 core; long ones get up to CORE_THRESHOLD cores.
#
# Constraint: FASTA has 32768 32-bp chunks → max_num_seq = 1048576/seq_len.
# 4 KB DMEM: refbuf + H1 + H2 = 9*CORE_THRESHOLD bytes; safe for threshold ≤ 455.
#
# Test naming: seq-len_L    = max sequence length
#              num-seq_N    = sequences per pod
#              threshold_T  = max cores per team (= CORE_THRESHOLD define)
#              len-min_M    = min length for variable-length tests
#
# Correctness sweep: small N to keep runtime short; vary threshold to test scheduler.
TESTS += seq-len_128__num-seq_64__threshold_64
TESTS += seq-len_256__num-seq_64__threshold_64
TESTS += seq-len_512__num-seq_64__threshold_32
TESTS += seq-len_512__num-seq_64__threshold_64

# Variable-length correctness: sequences range from len-min to seq-len.
TESTS += seq-len_512__num-seq_64__len-min_64__len-seed_1__len-quantum_8__threshold_32
TESTS += seq-len_512__num-seq_64__len-min_64__len-seed_1__len-quantum_8__threshold_64

# Performance sweep: use full FASTA dataset (N*L = 1 MB); no repeat needed —
# large N alone keeps 128 tiles busy for several seconds.
TESTS += seq-len_128__num-seq_8192__threshold_64
TESTS += seq-len_256__num-seq_4096__threshold_64
TESTS += seq-len_512__num-seq_2048__threshold_32
TESTS += seq-len_512__num-seq_2048__threshold_64

# Variable-length performance: full dataset with random lengths.
TESTS += seq-len_512__num-seq_2048__len-min_64__len-seed_1__len-quantum_8__threshold_32
TESTS += seq-len_1024__num-seq_1024__len-min_64__len-seed_1__len-quantum_8__threshold_64
