SHELL := /bin/bash
MAKEFLAGS += --no-print-directory

include applications.mk

HAMMER_SIM_DIR := hammer-sim
HAMMER_SIM_TARGET := hammer-sim
MODULE_LOAD_CMD ?= module load hammerblade
ALLOW_MISSING_TEST ?= 0

app-path = $(strip $(APP_PATH_$(1)))
selected-app-path = $(call app-path,$(APP))

.DEFAULT_GOAL := help
.PHONY: help list-apps native sim clean clean-app

help:
	@printf '%s\n' \
		'Usage:' \
		'  make native [APP=<app>] [TEST=<test>]' \
		'  make sim APP=<app> TEST=<test>' \
		'  make list-apps' \
		'' \
		'Applications:' \
		'  1d, dynamic, 2d, chaining, baseline, naive, efficient, step1, step2, step3, step4'

list-apps:
	@$(foreach app,$(APPLICATIONS),printf '%-10s %s\n' '$(app)' '$(APP_PATH_$(app))';)

native:
ifeq ($(strip $(APP)),)
	@set -euo pipefail; \
	for app in $(APPLICATIONS); do \
		$(MAKE) native APP=$$app TEST='$(TEST)' ALLOW_MISSING_TEST=$(if $(strip $(TEST)),1,0); \
	done
else
	@app_path='$(selected-app-path)'; \
	if [ -z "$$app_path" ]; then \
		printf "Unknown APP '%s'. Valid apps: $(APPLICATIONS)\n" '$(APP)' >&2; \
		exit 2; \
	fi; \
	app_dir="$(CURDIR)/$$app_path"; \
		$(MAKE) -C '$(HAMMER_SIM_DIR)' APP_DIR="$$app_dir" APP_NAME='$(APP)' all >/dev/null
ifneq ($(strip $(TEST)),)
	@app_bin_dir="$(CURDIR)/$(HAMMER_SIM_DIR)/bin/$(APP)"; \
	test_bin="$$app_bin_dir/$(TEST)/$(HAMMER_SIM_TARGET)"; \
	if [ -x "$$test_bin" ]; then \
		$(MAKE) -C "$$app_bin_dir" run-$(TEST); \
	elif [ "$(ALLOW_MISSING_TEST)" = "1" ]; then \
		printf "Skipping %s: test '%s' is not defined.\n" '$(APP)' '$(TEST)'; \
	else \
		printf "Test '%s' is not defined for APP '%s'.\n" '$(TEST)' '$(APP)' >&2; \
		exit 2; \
	fi
else
	@app_bin_dir="$(CURDIR)/$(HAMMER_SIM_DIR)/bin/$(APP)"; \
	$(MAKE) -C "$$app_bin_dir" run-all
endif
endif

sim:
	@set -euo pipefail; \
	if [ -z '$(APP)' ]; then \
		printf "APP is required for 'make sim'. Valid apps: $(APPLICATIONS)\n" >&2; \
		exit 2; \
	fi; \
	app_path='$(selected-app-path)'; \
	if [ -z "$$app_path" ]; then \
		printf "Unknown APP '%s'. Valid apps: $(APPLICATIONS)\n" '$(APP)' >&2; \
		exit 2; \
	fi; \
	if [ -z '$(TEST)' ]; then \
		printf "TEST is required for 'make sim'.\n" >&2; \
		exit 2; \
	fi; \
	APP_DIR="$(CURDIR)/$$app_path" TEST_NAME='$(TEST)' APP_NAME='$(APP)' \
		bash -lc 'set -euo pipefail; $(MODULE_LOAD_CMD); $(MAKE) -C "$$APP_DIR" generate; test_dir="$$APP_DIR/$$TEST_NAME"; [ -d "$$test_dir" ] || { printf "Test '\''%s'\'' is not defined for APP '\''%s'\''.\n" "$$TEST_NAME" "$$APP_NAME" >&2; exit 2; }; $(MAKE) -C "$$test_dir" profile.log'

clean:
ifeq ($(strip $(APP)),)
	@$(MAKE) -C '$(HAMMER_SIM_DIR)' clean
	@set -euo pipefail; \
	for app in $(APPLICATIONS); do \
		$(MAKE) clean-app APP=$$app; \
	done
else
	@$(MAKE) clean-app APP='$(APP)'
endif

clean-app:
	@app_path='$(selected-app-path)'; \
	if [ -z "$$app_path" ]; then \
		printf "Unknown APP '%s'. Valid apps: $(APPLICATIONS)\n" '$(APP)' >&2; \
		exit 2; \
	fi; \
	$(MAKE) -C "$$app_path" clean
