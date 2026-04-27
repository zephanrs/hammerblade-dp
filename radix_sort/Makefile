.PHONY: all
all: generate

include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../..)

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
	@echo vector-size = $(call get-vector-size,$*) >> $@
	@repeat="$(call get-repeat,$*)"; \
	if [ -n "$$repeat" ]; then echo repeat = "$$repeat" >> $@; fi


include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf radix_sort_*
