# m, n, k, blk_k, blk_c, tgx, tgy, dtype
#
# BLK_C is the message window: how many k steps travel per handshake, and how
# many the 4x4 register tile accumulates over. Scratchpad (768 words):
#   MB*NB + 2*BLK_C*MB + 2*BLK_C*NB + BLK_C*MB + MB*BLK_K + BLK_K*NB
#
# This is a new handshake, so walk the ladder again before anything large.
TESTS += $(call test-name,8,8,8,8,4,1,1,f32)
TESTS += $(call test-name,8,8,8,8,4,2,1,f32)
TESTS += $(call test-name,8,8,8,8,4,1,2,f32)
TESTS += $(call test-name,8,8,8,8,4,2,2,f32)
TESTS += $(call test-name,8,8,8,8,4,2,2,i32)
TESTS += $(call test-name,16,16,16,16,4,4,2,f32)
TESTS += $(call test-name,16,16,16,16,4,4,2,i32)

# window sweep at a fixed shape: how much does batching actually buy?
TESTS += $(call test-name,16,16,16,16,8,4,2,f32)
TESTS += $(call test-name,16,16,16,16,16,4,2,f32)

# full pod. kb=8 not 16: at kb=16 the budget lands exactly on 768 with no
# room for the stack.          128+128+64+64+128+64 = 576 words
TESTS += $(call test-name,128,128,128,8,4,16,8,f32)

# Crossover point, matched to regblock's 64^3/8x4 row (same kb=8) so the two
# differ only in dataflow.      128+128+64+64+128+64 = 576 words
TESTS += $(call test-name,64,64,64,8,4,8,4,f32)
