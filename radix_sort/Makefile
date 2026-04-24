.PHONY: all
all: generate

include app_path.mk
# radix_sort sits one level shallower than sw/1d, nw/*, dummy/roofline
# (which are at programs/<family>/<app>), so the walk to hb_hammerbench
# is 3 levels up — not 4. template.mk already has this right.
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../..)

# call to generate a test name
test-name = radix_sort_$(1)

# call to get parameter from test name
get-vector-size = $(lastword $(subst _, ,$(filter radix_sort_%,$(subst __, ,$(1)))))

# defines tests
TESTS =
include tests.mk

TESTS_DIRS = $(TESTS)

$(addsuffix /parameters.mk,$(TESTS_DIRS)): %/parameters.mk:
	@echo Creating $@
	@mkdir -p $(dir $@)
	@touch $@
	@echo test-name  = $* >> $@
	@echo vector-size = $(call get-vector-size,$*) >> $@

include $(HB_HAMMERBENCH_PATH)/mk/testbench_common.mk

clean:
	rm -rf radix_sort_*
