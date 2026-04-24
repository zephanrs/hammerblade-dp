# sw/dynamic: Smith-Waterman with variable-length sequences and a dynamic
# atomic work queue.  Each group of 16 cores pulls sequences from a shared
# counter and processes them end-to-end.
#
# Constraint: FASTA file has 32768 32-bp chunks, so max_num_seq = 1048576/seq_len.
# 4 KB DMEM limit: refbuf + H1 + H2 = 9*(seq_len/8) bytes; safe up to seq_len=2048.
#
# Test naming: seq-len_L  = max (and here, fixed) sequence length
#              num-seq_N  = unique sequences per pod (reads cycle when N*L > file size)
#              repeat_R   = kernel loops over the N sequences R times for longer runtime
#              len-min_M  = minimum sequence length for variable-length tests
#
# Performance sweep: hold N*L = 1 MB (full FASTA) and scale repeat to get ~5 s runtime.
TESTS += seq-len_32__num-seq_32768__repeat_64
TESTS += seq-len_64__num-seq_16384__repeat_32
TESTS += seq-len_128__num-seq_8192__repeat_16
TESTS += seq-len_256__num-seq_4096__repeat_8
TESTS += seq-len_512__num-seq_2048__repeat_4
TESTS += seq-len_1024__num-seq_1024__repeat_2
TESTS += seq-len_2048__num-seq_512__repeat_1

# Variable-length sweep: same max length and total work, but per-sequence lengths
# vary uniformly from len-min to seq-len.  Tests dynamic load balancing.
TESTS += seq-len_512__num-seq_2048__len-min_64__len-seed_1__len-quantum_8__repeat_4
TESTS += seq-len_1024__num-seq_1024__len-min_64__len-seed_1__len-quantum_8__repeat_2
