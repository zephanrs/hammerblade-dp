SHELL := /bin/bash
MAKEFLAGS += --no-print-directory

include applications.mk

HAMMER_SIM_DIR := hammer-sim
HAMMER_SIM_TARGET := hammer-sim
MODULE_LOAD_CMD ?= module load hammerblade
ALLOW_MISSING_TEST ?= 0

# Experiment apps (used by clean-generated and status targets).
EXPERIMENT_APPS := sw/1d sw/2d nw/naive nw/baseline nw/efficient dummy/roofline

# Your assigned hardware unit ID.
UNIT_ID ?= 2
RESET_DIR := /cluster_src/reset_half

app-path = $(strip $(APP_PATH_$(1)))
selected-app-path = $(call app-path,$(APP))

.DEFAULT_GOAL := help
.PHONY: help list-apps native sim clean clean-app \
        clean-results clean-generated clean-all \
        reset cool-down reset-device status experiments

help:
	@printf '%s\n' \
		'Usage:' \
		'  make native [APP=<app>] [TEST=<test>]' \
		'  make sim APP=<app> TEST=<test>' \
		'  make list-apps' \
		'  make experiments              # run all experiments (calls run_experiments.sh)' \
		'' \
		'Cleanup:' \
		'  make clean                    # clean hammer-sim build output' \
		'  make clean-results            # delete results/ CSV and log files' \
		'  make clean-generated          # delete all generated test directories in experiment apps' \
		'  make clean-all                # all of the above' \
		'' \
		'Hardware control (UNIT_ID=2 by default):' \
		'  make reset                    # reset the hardware unit (do before every run)' \
		'  make cool-down                # cool down after work / slow clock for slow-mode data' \
		'  make reset-device             # kill any running processes + prompt to reset' \
		'  make status                   # show generated test dirs and running processes' \
		'' \
		'Applications:'
	@printf '  %s\n' '$(APPLICATIONS)'

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

# Delete results/ CSV and log files.
clean-results:
	@echo "Removing results/ directory..."
	@rm -rf results/
	@echo "Done."

# Delete all generated test directories (the ops_*/, seq-len_*/ etc. dirs) in experiment apps.
clean-generated:
	@echo "Cleaning generated test directories..."
	@for app_dir in $(EXPERIMENT_APPS); do \
		if [ -d "$$app_dir" ]; then \
			printf "  %-30s " "$$app_dir"; \
			n=$$(find "$$app_dir" -maxdepth 1 -name "parameters.mk" 2>/dev/null | wc -l); \
			find "$$app_dir" -maxdepth 1 -mindepth 1 -type d \
				\( -name "seq-len_*" -o -name "ops_*" -o -name "num-elems_*" -o -name "cpg_*" \) \
				-exec rm -rf {} + 2>/dev/null || true; \
			echo "removed $$n test dirs"; \
		fi; \
	done
	@echo "Done."

# All cleanup combined.
clean-all: clean clean-results clean-generated

# Reset the hardware unit (run before every experiment, and after any failure).
reset:
	cd $(RESET_DIR) && make reset UNIT_ID=$(UNIT_ID)

# Cool down the hardware unit (run after finishing work, or before slow-clock experiments).
# cool_down also slows the core clock by 32x — use it to collect slow-clock roofline data.
cool-down:
	cd $(RESET_DIR) && make cool_down UNIT_ID=$(UNIT_ID)

# Kill any in-flight experiment processes, then remind you to reset.
reset-device:
	@echo "Killing experiment processes..."
	@pkill -f "run_experiments\|bsg_manycore" 2>/dev/null \
		&& echo "  Processes killed." \
		|| echo "  No experiment processes found."
	@echo ""
	@echo "Now reset the device:"
	@echo "  make reset   (or: cd $(RESET_DIR) && make reset UNIT_ID=$(UNIT_ID))"

# Show status: generated test dirs and running processes.
status:
	@echo "Generated test directories:"
	@for app_dir in $(EXPERIMENT_APPS); do \
		if [ -d "$$app_dir" ]; then \
			n_tests=$$(find "$$app_dir" -maxdepth 2 -name "parameters.mk" 2>/dev/null | wc -l); \
			printf "  %-30s %d test dirs\n" "$$app_dir" "$$n_tests"; \
		fi; \
	done
	@echo ""
	@echo "Running experiment processes:"
	@pgrep -la "run_experiments\|hammer-sim\|bsg_manycore" 2>/dev/null || echo "  (none)"

# Run all experiments using run_experiments.sh.
experiments:
	@bash run_experiments.sh
