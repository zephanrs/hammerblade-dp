# sequence length, band size, num sequences, cols per core block
TESTS += $(call test-name,16,1,64,4)
TESTS += $(call test-name,16,8,64,1)
TESTS += $(call test-name,32,16,64,2)
TESTS += $(call test-name,64,16,64,4)
TESTS += $(call test-name,128,32,64,4)
TESTS += $(call test-name,192,32,64,8)
