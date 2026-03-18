TESTS += seq-len_32__num-seq_32768__repeat_64
TESTS += seq-len_64__num-seq_16384__repeat_32
TESTS += seq-len_128__num-seq_8192__repeat_16
TESTS += seq-len_256__num-seq_4096__repeat_8
TESTS += seq-len_512__num-seq_2048__repeat_4
TESTS += seq-len_1024__num-seq_1024__repeat_2
TESTS += seq-len_2048__num-seq_512__repeat_1

# fixed-size core-scaling sweep: 256 bp, 256 sequences per active x-group
TESTS += seq-len_256__num-seq_256__repeat_8__active-groups_1
TESTS += seq-len_256__num-seq_512__repeat_8__active-groups_2
TESTS += seq-len_256__num-seq_1024__repeat_8__active-groups_4
TESTS += seq-len_256__num-seq_2048__repeat_8__active-groups_8
TESTS += seq-len_256__num-seq_4096__repeat_8__active-groups_16
