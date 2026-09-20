# m, n, k, dtype
TESTS += $(call test-name,8,8,8,f32)
TESTS += $(call test-name,8,8,8,i32)
TESTS += $(call test-name,32,32,32,f32)
TESTS += $(call test-name,32,32,32,i32)
TESTS += $(call test-name,64,64,64,f32)
TESTS += $(call test-name,64,64,64,i32)
# non-square, so a transposed index is caught rather than hidden by m==n==k
TESTS += $(call test-name,32,16,64,f32)
TESTS += $(call test-name,32,16,64,i32)
