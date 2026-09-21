# m, n, k, blk_k, tgx, tgy, dtype
#
# MB = M/tgy and NB = N/tgx, so tgy must divide M and tgx must divide N.
# Scratchpad budget (768 words): MB*NB + MB*BLK_K + BLK_K*NB.
#
# Small tile groups exist so the multi-tile decomposition can be validated
# under RTL in minutes. The 16x8 rows are the real pod and are meant for
# silicon.

# single tile: control, should track mm/blocked
TESTS += $(call test-name,8,8,8,8,1,1,f32)

# 2x2 tile group, RTL-sized          MB=4  NB=4  -> 16+32+32  =  80 words
TESTS += $(call test-name,8,8,8,8,2,2,f32)
TESTS += $(call test-name,8,8,8,8,2,2,i32)

# 4x2 tile group, RTL-sized          MB=8  NB=4  -> 32+128+64 = 224 words
TESTS += $(call test-name,16,16,16,16,4,2,f32)
TESTS += $(call test-name,16,16,16,16,4,2,i32)

# non-square on 4x2                  MB=16 NB=4  -> 64+256+64 = 384 words
TESTS += $(call test-name,32,16,64,16,4,2,f32)

# full pod, silicon-sized            MB=16 NB=8  -> 128+256+128 = 512 words
TESTS += $(call test-name,128,128,128,16,16,8,f32)
TESTS += $(call test-name,128,128,128,16,16,8,i32)
