# test name
test-name = seq-len_$(1)__band-size_$(2)__num-seq_$(3)__col_$(4)
get-seq-len = $(lastword $(subst _, ,$(filter seq-len_%,$(subst __, ,$(1)))))
get-band-size = $(lastword $(subst _, ,$(filter band-size_%,$(subst __, ,$(1)))))
get-num-seq = $(lastword $(subst _, ,$(filter num-seq_%,$(subst __, ,$(1)))))
get-col = $(lastword $(subst _, ,$(filter col_%,$(subst __, ,$(1)))))
get-repeat = $(lastword $(subst _, ,$(filter repeat_%,$(subst __, ,$(1)))))

# Native simulator compile-time defines for one test name.
# Apps with a different naming scheme should redefine this here.
native-defines-for-test = \
	-DSEQ_LEN=$(call get-seq-len,$(1)) \
	-DBAND_SIZE=$(call get-band-size,$(1)) \
	-DNUM_SEQ=$(call get-num-seq,$(1)) \
	-DCOL=$(call get-col,$(1)) \
	$(if $(call get-repeat,$(1)),-DINPUT_REPEAT_FACTOR=$(call get-repeat,$(1)))

# Native simulator runtime arguments for one test name.
# The first argument is a placeholder for host code that expects a device
# binary path in argv[1], even though the native runtime does not load one.
native-program-args-for-test = \
	hammer-sim-kernel \
	$(APP_DIR)/dna-query32.fasta \
	$(APP_DIR)/dna-reference32.fasta
