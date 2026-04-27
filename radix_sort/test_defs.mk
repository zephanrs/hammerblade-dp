# test name format: radix_sort_<vector-size>__num-arr_<N>
# num-arr is the count of distinct SIZE-int arrays sorted per kernel call.
# Tune num-arr so each test runs ~20s and per-sort timing averages cleanly.
test-name = radix_sort_$(1)__num-arr_$(2)
get-vector-size = $(lastword $(subst _, ,$(filter radix_sort_%,$(subst __, ,$(1)))))
get-num-arr     = $(lastword $(subst _, ,$(filter num-arr_%,$(subst __, ,$(1)))))

native-defines-for-test = \
	$(if $(call get-vector-size,$(1)),-DSIZE=$(call get-vector-size,$(1))) \
	$(if $(call get-num-arr,$(1)),-DNUM_ARR=$(call get-num-arr,$(1)))

native-program-args-for-test = hammer-sim-kernel
