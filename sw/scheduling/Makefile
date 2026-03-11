.PHONY: all
all: generate


include test_defs.mk


# tests;
TESTS =
include tests.mk

TESTS_DIRS = $(TESTS)


$(addsuffix /parameters.mk,$(TESTS_DIRS)): %/parameters.mk:
	@echo Creating $@
	@mkdir -p $(dir $@)
	@touch $@
	@echo test-name  = $* >> $@
	@echo num-seq = $(call get-num-seq,$*) >> $@
	@echo seq-len = $(call get-seq-len,$*) >> $@
	@threshold="$(call get-threshold,$*)"; \
	if [ -n "$$threshold" ]; then echo threshold = "$$threshold" >> $@; fi
	@len_min="$(call get-len-min,$*)"; \
	if [ -n "$$len_min" ]; then echo len-min = "$$len_min" >> $@; fi
	@len_seed="$(call get-len-seed,$*)"; \
	if [ -n "$$len_seed" ]; then echo len-seed = "$$len_seed" >> $@; fi
	@len_quantum="$(call get-len-quantum,$*)"; \
	if [ -n "$$len_quantum" ]; then echo len-quantum = "$$len_quantum" >> $@; fi


include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf seq-len*
