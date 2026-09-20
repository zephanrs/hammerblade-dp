# m, n, k, dtype
#
# RTL simulation is slow, so keep to the small shapes there: 8x8x8 is 512 MACs
# and finishes quickly, 16x16x16 is 4096 and is the practical ceiling for a
# quick turnaround. The 32^3 and 64^3 shapes are sized for silicon.
TESTS += $(call test-name,8,8,8,f32)
TESTS += $(call test-name,8,8,8,i32)
TESTS += $(call test-name,16,16,16,f32)
TESTS += $(call test-name,16,16,16,i32)
TESTS += $(call test-name,32,32,32,f32)
TESTS += $(call test-name,32,32,32,i32)
TESTS += $(call test-name,64,64,64,f32)
TESTS += $(call test-name,64,64,64,i32)
# non-square, so a transposed index is caught rather than hidden by m==n==k
TESTS += $(call test-name,32,16,64,f32)
TESTS += $(call test-name,32,16,64,i32)
