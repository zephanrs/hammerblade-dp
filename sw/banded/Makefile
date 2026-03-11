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
	@echo seq-len = $(call get-seq-len,$*) >> $@
	@echo band-size = $(call get-band-size,$*) >> $@
	@echo num-seq = $(call get-num-seq,$*) >> $@
	@echo col = $(call get-col,$*) >> $@


include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf seq-len*
