# test name
test-name = seq-len_$(1)__num-seq_$(2)
get-num-seq = $(lastword $(subst _, ,$(filter num-seq_%,$(subst __, ,$(1)))))
get-seq-len = $(lastword $(subst _, ,$(filter seq-len_%,$(subst __, ,$(1)))))

# Native simulator compile-time defines for one test name.
native-defines-for-test = \
	-DNUM_SEQ=$(call get-num-seq,$(1)) \
	-DSEQ_LEN=$(call get-seq-len,$(1)) \
	-DMAX_SEQ_LEN=$(call get-seq-len,$(1))

# Native simulator runtime arguments for one test name.
native-program-args-for-test = \
	hammer-sim-kernel \
	$(APP_DIR)/dna-query32.fasta \
	$(APP_DIR)/dna-reference32.fasta
