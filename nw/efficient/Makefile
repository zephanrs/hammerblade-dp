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
	@repeat="$(call get-repeat,$*)"; \
	if [ -n "$$repeat" ]; then echo repeat = "$$repeat" >> $@; fi


include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf seq-len*
