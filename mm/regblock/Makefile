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
	@echo test-name = $* >> $@
	@echo mat-m = $(call get-m,$*) >> $@
	@echo mat-n = $(call get-n,$*) >> $@
	@echo mat-k = $(call get-k,$*) >> $@
	@echo blk-k = $(call get-kb,$*) >> $@
	@echo tile-x = $(call get-tgx,$*) >> $@
	@echo tile-y = $(call get-tgy,$*) >> $@
	@echo dtype = $(call get-dtype,$*) >> $@


include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf m_*__n_*__k_*
