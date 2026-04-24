.PHONY: all
all: generate

include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)

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
	@repeat="$(call get-repeat,$*)"; \
	if [ -n "$$repeat" ]; then echo repeat = "$$repeat" >> $@; fi
	@active_groups="$(call get-active-groups,$*)"; \
	if [ -n "$$active_groups" ]; then echo active-groups = "$$active_groups" >> $@; fi
	@cpg="$(call get-cpg,$*)"; \
	if [ -n "$$cpg" ]; then echo cpg = "$$cpg" >> $@; fi
	@pod_unique="$(call get-pod-unique-data,$*)"; \
	if [ -n "$$pod_unique" ]; then echo pod-unique-data = "$$pod_unique" >> $@; fi
	@len_min="$(call get-len-min,$*)"; \
	if [ -n "$$len_min" ]; then echo len-min = "$$len_min" >> $@; fi
	@len_seed="$(call get-len-seed,$*)"; \
	if [ -n "$$len_seed" ]; then echo len-seed = "$$len_seed" >> $@; fi
	@len_quantum="$(call get-len-quantum,$*)"; \
	if [ -n "$$len_quantum" ]; then echo len-quantum = "$$len_quantum" >> $@; fi


include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf seq-len*
