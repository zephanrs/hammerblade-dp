# test name
test-name = m_$(1)__n_$(2)__k_$(3)__blk_$(4)__tgx_$(5)__tgy_$(6)__dtype_$(7)
get-m = $(lastword $(subst _, ,$(filter m_%,$(subst __, ,$(1)))))
get-n = $(lastword $(subst _, ,$(filter n_%,$(subst __, ,$(1)))))
get-k = $(lastword $(subst _, ,$(filter k_%,$(subst __, ,$(1)))))
get-blk = $(lastword $(subst _, ,$(filter blk_%,$(subst __, ,$(1)))))
get-tgx = $(lastword $(subst _, ,$(filter tgx_%,$(subst __, ,$(1)))))
get-tgy = $(lastword $(subst _, ,$(filter tgy_%,$(subst __, ,$(1)))))
get-dtype = $(lastword $(subst _, ,$(filter dtype_%,$(subst __, ,$(1)))))

elem-is-float = $(if $(filter f32,$(1)),1,$(if $(filter i32,$(1)),0,$(error unknown dtype '$(1)': expected f32 or i32)))

native-defines-for-test = \
	-DMAT_M=$(call get-m,$(1)) \
	-DMAT_N=$(call get-n,$(1)) \
	-DMAT_K=$(call get-k,$(1)) \
	-DBLK=$(call get-blk,$(1)) \
	-DELEM_IS_FLOAT=$(call elem-is-float,$(call get-dtype,$(1)))

native-program-args-for-test = \
	hammer-sim-kernel
