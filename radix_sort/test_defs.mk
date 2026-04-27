# test name format: radix_sort_<vector-size>__repeat_<R>  (repeat optional)
test-name = radix_sort_$(1)
get-vector-size = $(lastword $(subst _, ,$(filter radix_sort_%,$(subst __, ,$(1)))))
get-repeat      = $(lastword $(subst _, ,$(filter repeat_%,$(subst __, ,$(1)))))

native-defines-for-test = \
	$(if $(call get-vector-size,$(1)),-DSIZE=$(call get-vector-size,$(1))) \
	$(if $(call get-repeat,$(1)),-DREPEAT=$(call get-repeat,$(1)))

native-program-args-for-test = hammer-sim-kernel
