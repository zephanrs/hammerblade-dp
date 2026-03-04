# test name
test-name = chain-len_$(1)__lookback_$(2)
get-lookback = $(lastword $(subst _, ,$(filter lookback_%,$(subst __, ,$(1)))))
get-chain-len = $(lastword $(subst _, ,$(filter chain-len_%,$(subst __, ,$(1)))))

# Native simulator compile-time defines for one test name.
native-defines-for-test = \
	-DCHAIN_LEN=$(call get-chain-len,$(1)) \
	-DLOOKBACK=$(call get-lookback,$(1))

# Native simulator runtime arguments for one test name.
native-program-args-for-test = \
	hammer-sim-kernel
