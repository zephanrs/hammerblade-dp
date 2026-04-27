# sw/1ddb tests.mk — double-buffered 1d, cpg=8 sweep for the comparison run.
# repeat numbers mirror sw/1d so total work matches sw/1d at the same cpg/seq-len.

TESTS += seq-len_32__num-seq_32768__repeat_2048
TESTS += seq-len_64__num-seq_16384__repeat_1024
TESTS += seq-len_128__num-seq_8192__repeat_512
TESTS += seq-len_256__num-seq_4096__repeat_256
TESTS += seq-len_512__num-seq_2048__repeat_128
TESTS += seq-len_1024__num-seq_1024__repeat_64
