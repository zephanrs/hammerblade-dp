# test name: barrier_bench_<barrier>__n_<N>
#   barrier ∈ { default, linear }   — picks bsg_barrier_amoadd impl
#   n      = number of barrier iterations inside the kernel timing region
test-name = barrier_bench_$(1)__n_$(2)

get-barrier = $(lastword $(subst _, ,$(filter barrier_bench_%,$(subst __, ,$(1)))))
get-n       = $(lastword $(subst _, ,$(filter n_%,$(subst __, ,$(1)))))

native-defines-for-test = \
	$(if $(call get-n,$(1)),-DN_BARRIERS=$(call get-n,$(1))) \
	$(if $(call get-barrier,$(1)),-DBARRIER_LABEL=\"$(call get-barrier,$(1))\")

native-program-args-for-test = hammer-sim-kernel
