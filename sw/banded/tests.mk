# sw/banded: Banded Smith-Waterman.  Only cells within band_size of the diagonal
# are computed, reducing work per sequence from O(L^2) to O(L*band_size).
# col = number of query positions each tile advances per anti-diagonal step.
#
# Constraint: FASTA has 32768 32-bp chunks → max_num_seq = 1048576/seq_len.
# 4 KB DMEM: each tile holds two DP rows of width band_size; safe for band_size ≤ 512.
#
# Test naming: seq-len_L   = sequence length
#              band-size_B = half-band width (cells computed per row = 2*B+1)
#              num-seq_N   = sequences per pod
#              col_C       = query advance per step (pipeline width)
#              repeat_R    = repeat factor for longer runtime
#
# Band-size sweep at fixed seq_len=256, num_seq=4096, col=4.
# Shows how GCUPS scales as less work is skipped.
TESTS += seq-len_256__band-size_16__num-seq_4096__col_4__repeat_8
TESTS += seq-len_256__band-size_32__num-seq_4096__col_4__repeat_8
TESTS += seq-len_256__band-size_64__num-seq_4096__col_4__repeat_8
TESTS += seq-len_256__band-size_128__num-seq_4096__col_4__repeat_8

# Col sweep at fixed seq_len=256, band_size=64, num_seq=4096.
# Tests pipeline width effect on performance.
TESTS += seq-len_256__band-size_64__num-seq_4096__col_1__repeat_8
TESTS += seq-len_256__band-size_64__num-seq_4096__col_2__repeat_8
TESTS += seq-len_256__band-size_64__num-seq_4096__col_8__repeat_8
TESTS += seq-len_256__band-size_64__num-seq_4096__col_16__repeat_8

# Sequence-length sweep at fixed band_size=64, num_seq*seq_len = 1 MB.
TESTS += seq-len_128__band-size_64__num-seq_8192__col_4__repeat_16
TESTS += seq-len_256__band-size_64__num-seq_4096__col_4__repeat_8
TESTS += seq-len_512__band-size_64__num-seq_2048__col_4__repeat_4
TESTS += seq-len_1024__band-size_64__num-seq_1024__col_4__repeat_2
