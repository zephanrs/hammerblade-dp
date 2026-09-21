# m, n, k, blk, tgx, tgy, dtype
#
# BLK is the fixed output block edge; the working set is 3*BLK^2 words at any
# matrix size. BLK=16 is 768 words, the same 3 KB of 4 KB Lin Cheng's thesis
# uses, and is the maximum that fits.

# small, for correctness
TESTS += $(call test-name,16,16,16,16,1,1,f32)
TESTS += $(call test-name,32,32,32,16,2,2,f32)
TESTS += $(call test-name,32,32,32,16,2,2,i32)
TESTS += $(call test-name,32,32,32,8,2,2,f32)

# blocks that do not divide evenly over the tile group, so tiles take
# different numbers of output blocks
TESTS += $(call test-name,64,64,64,16,4,2,f32)
TESTS += $(call test-name,48,32,64,16,4,2,f32)

# matched against regblock's 64^3 / 8x4 run
TESTS += $(call test-name,64,64,64,16,8,4,f32)

# full pod. At BLK=16, 128^3 is only 64 output blocks over 128 tiles, so half
# the pod idles -- BLK=8 gives 256 blocks, 2 per tile, at less compute per
# staged word. Worth measuring both.
TESTS += $(call test-name,128,128,128,16,16,8,f32)
TESTS += $(call test-name,128,128,128,8,16,8,f32)

# the headline: same workload and same hardware as the thesis
TESTS += $(call test-name,256,256,256,16,16,8,f32)
