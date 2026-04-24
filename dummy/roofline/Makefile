.PHONY: all
all: generate

include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)

include test_defs.mk

TESTS =
include tests.mk

TESTS_DIRS = $(TESTS)

$(addsuffix /parameters.mk,$(TESTS_DIRS)): %/parameters.mk:
	@echo Creating $@
	@mkdir -p $(dir $@)
	@touch $@
	@echo test-name = $* >> $@
	@ops="$(call get-ops,$*)"; \
	if [ -n "$$ops" ]; then echo ops = "$$ops" >> $@; fi
	@n_elems="$(call get-n-elems,$*)"; \
	if [ -n "$$n_elems" ]; then echo n-elems = "$$n_elems" >> $@; fi
	@repeat="$(call get-repeat,$*)"; \
	if [ -n "$$repeat" ]; then echo repeat = "$$repeat" >> $@; fi

include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf ops_*
