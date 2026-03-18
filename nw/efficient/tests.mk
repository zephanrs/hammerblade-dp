# sequence length, num sequences
TESTS += $(call test-name,16,64)
TESTS += $(call test-name,32,64)
TESTS += $(call test-name,64,64)
TESTS += $(call test-name,128,64)
TESTS += $(call test-name,192,64)
TESTS += seq-len_512__num-seq_16__repeat_16
TESTS += seq-len_1024__num-seq_8__repeat_8
