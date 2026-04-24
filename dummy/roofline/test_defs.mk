# test name: ops_N  (N = OPS_PER_ELEM)
test-name = ops_$(1)
get-ops      = $(lastword $(subst _, ,$(filter ops_%,$(subst __, ,$(1)))))
get-n-elems  = $(lastword $(subst _, ,$(filter n-elems_%,$(subst __, ,$(1)))))
get-repeat   = $(lastword $(subst _, ,$(filter repeat_%,$(subst __, ,$(1)))))

native-defines-for-test = \
	$(if $(call get-ops,$(1)),-DOPS_PER_ELEM=$(call get-ops,$(1))) \
	$(if $(call get-n-elems,$(1)),-DN_ELEMS=$(call get-n-elems,$(1))) \
	$(if $(call get-repeat,$(1)),-DREPEAT=$(call get-repeat,$(1)))

native-program-args-for-test = hammer-sim-kernel
