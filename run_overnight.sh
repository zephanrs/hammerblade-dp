#!/usr/bin/env bash
# run_overnight.sh — slow-clock sweep of sw/1d, sw/2d, dummy/roofline.
#
# This variant ONLY runs the slow-clock sweep. Prerequisite: you must have
# already put the board into cool/slow-clock mode manually, e.g.:
#
#     make cool-down
#
# (The previous version tried to run `make cool-down` from inside nohup,
# which did not actually take effect on the hardware. So the script now
# stays out of that business — you drive the clock state.)
#
# Designed to be launched with nohup and left running overnight:
#
#     make cool-down                    # YOU run this first, interactively
#     nohup ./run_overnight.sh > run_overnight.out 2>&1 &
#     disown
#     exit                              # safe to log out
#
# If anything hangs, just kill the nohup'd process.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_ROOT"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OVERNIGHT_LOG="$REPO_ROOT/results/overnight_${TIMESTAMP}.log"
mkdir -p "$REPO_ROOT/results"

exec > >(tee -a "$OVERNIGHT_LOG") 2>&1

APPS=(sw/1d sw/2d dummy/roofline)

ts() { date '+%F %H:%M:%S'; }

printf '[%s] === Overnight slow-clock sweep starting ===\n' "$(ts)"
printf '[%s] Repo root : %s\n'   "$(ts)" "$REPO_ROOT"
printf '[%s] Apps      : %s\n'   "$(ts)" "${APPS[*]}"
printf '[%s] Log file  : %s\n'   "$(ts)" "$OVERNIGHT_LOG"
printf '[%s] Assumes   : `make cool-down` was already run interactively.\n\n' "$(ts)"

SLOW_MODE=1 ./run_experiments.sh "${APPS[@]}"
printf '\n[%s] Slow-clock sweep finished (exit=%d).\n' "$(ts)" "$?"
printf '[%s] === Done ===\n' "$(ts)"
