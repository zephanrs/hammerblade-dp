# m, n, k, blk_k, tgx, tgy, dtype
#
# MB = M/tgy, NB = N/tgx. Scratchpad (768 words):
#   MB*NB + 2*MB + 2*NB + MB + NB + MB*BLK_K + BLK_K*NB
#
# The small tile groups are not optional here. Offline checking can verify the
# index arithmetic and that C is fully covered, but it cannot verify the
# handshake: credits, slot reuse and forward ordering only show up on real
# hardware. Walk the tile group up one step at a time.

# degenerate: no neighbours at all, forwards nothing. Should match mm/parallel.
TESTS += $(call test-name,8,8,8,8,1,1,f32)

# 2x1 and 1x2: exercise one flow in isolation before both at once
TESTS += $(call test-name,8,8,8,8,2,1,f32)
TESTS += $(call test-name,8,8,8,8,1,2,f32)

# 2x2: both flows, every tile role present (feeder, forwarder, sink)
TESTS += $(call test-name,8,8,8,8,2,2,f32)
TESTS += $(call test-name,8,8,8,8,2,2,i32)

# 4x2: interior tiles that neither feed nor sink
TESTS += $(call test-name,16,16,16,16,4,2,f32)
TESTS += $(call test-name,16,16,16,16,4,2,i32)

# non-square on 4x2
TESTS += $(call test-name,32,16,64,16,4,2,f32)

# full pod, silicon-sized      MB=16 NB=8 -> 128+32+16+16+8+256+128 = 584 words
TESTS += $(call test-name,128,128,128,16,16,8,f32)
TESTS += $(call test-name,128,128,128,16,16,8,i32)
