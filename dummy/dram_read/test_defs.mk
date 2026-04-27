# test name: dram_read_<n_elems>__repeat_<R>
test-name = dram_read_$(1)__repeat_$(2)
get-n-elems = $(lastword $(subst _, ,$(filter dram_read_%,$(subst __, ,$(1)))))
get-repeat  = $(lastword $(subst _, ,$(filter repeat_%,$(subst __, ,$(1)))))

native-defines-for-test = \
	$(if $(call get-n-elems,$(1)),-DN_ELEMS=$(call get-n-elems,$(1))) \
	$(if $(call get-repeat,$(1)),-DREPEAT=$(call get-repeat,$(1)))

native-program-args-for-test = hammer-sim-kernel
