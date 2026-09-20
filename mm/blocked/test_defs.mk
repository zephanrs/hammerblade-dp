# test name
test-name = m_$(1)__n_$(2)__k_$(3)__bn_$(4)__dtype_$(5)
get-m = $(lastword $(subst _, ,$(filter m_%,$(subst __, ,$(1)))))
get-n = $(lastword $(subst _, ,$(filter n_%,$(subst __, ,$(1)))))
get-k = $(lastword $(subst _, ,$(filter k_%,$(subst __, ,$(1)))))
get-bn = $(lastword $(subst _, ,$(filter bn_%,$(subst __, ,$(1)))))
get-dtype = $(lastword $(subst _, ,$(filter dtype_%,$(subst __, ,$(1)))))

# dtype_f32 -> ELEM_IS_FLOAT=1, dtype_i32 -> ELEM_IS_FLOAT=0
elem-is-float = $(if $(filter f32,$(1)),1,$(if $(filter i32,$(1)),0,$(error unknown dtype '$(1)': expected f32 or i32)))

# Native simulator compile-time defines for one test name.
native-defines-for-test = \
	-DMAT_M=$(call get-m,$(1)) \
	-DMAT_N=$(call get-n,$(1)) \
	-DMAT_K=$(call get-k,$(1)) \
	-DBLK_N=$(call get-bn,$(1)) \
	-DELEM_IS_FLOAT=$(call elem-is-float,$(call get-dtype,$(1)))

# Native simulator runtime arguments for one test name.
native-program-args-for-test = \
	hammer-sim-kernel
