#!/usr/bin/env bash
# run_overnight.sh — sweep sw/1d, sw/2d, dummy/roofline at fast + slow clock.
#
# Designed to be launched with nohup and left running while you disconnect:
#
#     nohup ./run_overnight.sh > run_overnight.out 2>&1 &
#     disown
#     exit    # safe to log out
#
# Progress is tee'd into results/overnight_<timestamp>.log.
#
# Sequence:
#   1. Fast-clock sweep over APPS.
#   2. `make cool-down` — puts the board into slow-clock mode (~32x slower)
#      so SLOW_MODE=1 actually collects slow-clock data.
#   3. Slow-clock sweep over APPS (SLOW_MODE=1 auto-scales repeats 1/20).
#   4. `make cool-down` at the end as a final thermal/cleanup step, per the
#      overnight-run request. (On this board cool-down is what toggles into
#      the cool/slow state; re-running it after the slow sweep is the
#      requested closing action — adjust if your workflow needs something
#      different.)
#
# If a run hangs, just kill the nohup'd process — the script is best-effort.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_ROOT"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OVERNIGHT_LOG="$REPO_ROOT/results/overnight_${TIMESTAMP}.log"
mkdir -p "$REPO_ROOT/results"

# Tee everything — stdout and stderr — into the overnight log.
exec > >(tee -a "$OVERNIGHT_LOG") 2>&1

APPS=(sw/1d sw/2d dummy/roofline)

ts() { date '+%F %H:%M:%S'; }

printf '[%s] === Overnight sweep starting ===\n' "$(ts)"
printf '[%s] Repo root : %s\n'       "$(ts)" "$REPO_ROOT"
printf '[%s] Apps      : %s\n'       "$(ts)" "${APPS[*]}"
printf '[%s] Log file  : %s\n\n'     "$(ts)" "$OVERNIGHT_LOG"

# --- 1. Fast-clock sweep ----------------------------------------------------
printf '[%s] --- Phase 1/3: fast-clock sweep ---\n' "$(ts)"
./run_experiments.sh "${APPS[@]}"
printf '[%s] Fast-clock sweep finished (exit=%d).\n\n' "$(ts)" "$?"

# --- 2. cool-down (enter slow-clock mode) -----------------------------------
printf '[%s] --- Phase 2/3: make cool-down (enter slow-clock mode) ---\n' "$(ts)"
make cool-down
printf '[%s] cool-down complete (exit=%d).\n\n' "$(ts)" "$?"

# --- 3. Slow-clock sweep ----------------------------------------------------
printf '[%s] --- Phase 3/3: slow-clock sweep (SLOW_MODE=1) ---\n' "$(ts)"
SLOW_MODE=1 ./run_experiments.sh "${APPS[@]}"
printf '[%s] Slow-clock sweep finished (exit=%d).\n\n' "$(ts)" "$?"

# --- 4. Final cool-down -----------------------------------------------------
printf '[%s] --- Final: make cool-down ---\n' "$(ts)"
make cool-down
printf '[%s] Final cool-down complete (exit=%d).\n\n' "$(ts)" "$?"

printf '[%s] === Overnight sweep done ===\n' "$(ts)"
