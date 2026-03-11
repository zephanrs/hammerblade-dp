# sequence length, num sequences
TESTS += $(call test-name,16,64)
TESTS += $(call test-name,32,64)
TESTS += $(call test-name,64,64)
TESTS += $(call test-name,128,64)
TESTS += $(call test-name,256,64)
TESTS += seq-len_512__num-seq_64__len-min_64__len-seed_1__len-quantum_8
