# m, n, k, blk_n, dtype
#
# BLK_N must divide N, and MAT_K*BLK_N + BLK_N must fit the scratchpad budget
# (768 words), so the panel narrows as K grows:
#   k=8  bn=8  ->  72 words      k=32 bn=16 -> 528 words
#   k=16 bn=16 -> 272 words      k=64 bn=8  -> 520 words
TESTS += $(call test-name,8,8,8,8,f32)
TESTS += $(call test-name,8,8,8,8,i32)
TESTS += $(call test-name,16,16,16,16,f32)
TESTS += $(call test-name,16,16,16,16,i32)
TESTS += $(call test-name,32,32,32,16,f32)
TESTS += $(call test-name,32,32,32,16,i32)
TESTS += $(call test-name,64,64,64,8,f32)
TESTS += $(call test-name,64,64,64,8,i32)
# non-square, so a transposed index is caught rather than hidden by m==n==k
TESTS += $(call test-name,32,16,64,8,f32)
TESTS += $(call test-name,32,16,64,8,i32)
# panel-width sweep at a fixed shape, to see where A re-reads start to bite
TESTS += $(call test-name,16,16,16,4,f32)
TESTS += $(call test-name,16,16,16,8,f32)
