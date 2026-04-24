# test name
test-name = seq-len_$(1)__num-seq_$(2)
get-num-seq = $(lastword $(subst _, ,$(filter num-seq_%,$(subst __, ,$(1)))))
get-seq-len = $(lastword $(subst _, ,$(filter seq-len_%,$(subst __, ,$(1)))))
get-repeat = $(lastword $(subst _, ,$(filter repeat_%,$(subst __, ,$(1)))))
get-active-groups = $(lastword $(subst _, ,$(filter active-groups_%,$(subst __, ,$(1)))))
get-cpg = $(lastword $(subst _, ,$(filter cpg_%,$(subst __, ,$(1)))))
get-pod-unique-data = $(lastword $(subst _, ,$(filter pod-unique-data_%,$(subst __, ,$(1)))))
get-len-min = $(lastword $(subst _, ,$(filter len-min_%,$(subst __, ,$(1)))))
get-len-seed = $(lastword $(subst _, ,$(filter len-seed_%,$(subst __, ,$(1)))))
get-len-quantum = $(lastword $(subst _, ,$(filter len-quantum_%,$(subst __, ,$(1)))))

# Native simulator compile-time defines for one test name.
# Apps with a different naming scheme should redefine this here.
native-defines-for-test = \
	-DNUM_SEQ=$(call get-num-seq,$(1)) \
	-DSEQ_LEN=$(call get-seq-len,$(1)) \
	$(if $(call get-repeat,$(1)),-DINPUT_REPEAT_FACTOR=$(call get-repeat,$(1))) \
	$(if $(call get-active-groups,$(1)),-DACTIVE_COMPUTE_GROUPS=$(call get-active-groups,$(1))) \
	$(if $(call get-len-min,$(1)),-DVAR_LEN_MIN=$(call get-len-min,$(1))) \
	$(if $(call get-len-seed,$(1)),-DLEN_SEED=$(call get-len-seed,$(1))) \
	$(if $(call get-len-quantum,$(1)),-DLEN_QUANTUM=$(call get-len-quantum,$(1)))

# Native simulator runtime arguments for one test name.
# The first argument is a placeholder for host code that expects a device
# binary path in argv[1], even though the native runtime does not load one.
native-program-args-for-test = \
	hammer-sim-kernel \
	$(APP_DIR)/dna-query32.fasta \
	$(APP_DIR)/dna-reference32.fasta
