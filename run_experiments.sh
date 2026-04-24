#!/usr/bin/env bash
# run_experiments.sh — build and run all HammerBlade SW/NW/roofline experiments.
#
# Runs on real ASIC hardware.  Before running, you MUST have already done:
#   cd /cluster_src/reset_half && make reset UNIT_ID=2
#
# Usage:
#   ./run_experiments.sh                        # run all apps, all tests
#   ./run_experiments.sh sw/1d sw/2d            # run specific apps
#   SLOW_MODE=1 ./run_experiments.sh            # label results as "slow" (run after cool_down)
#   TIMEOUT=600 ./run_experiments.sh            # per-test timeout in seconds (default 3600)
#   DRY_RUN=1 ./run_experiments.sh              # print what would run, don't execute
#   VERBOSE=1 ./run_experiments.sh              # show full make output inline
#
# Output:
#   results/results_<timestamp>.csv    — timing data
#   results/run_<timestamp>.log        — full log of this run
#
# CSV columns:
#   app, test_name, seq_len, num_seq, repeat, cpg, pod_unique_data,
#   speed, kernel_time_sec, gcups, arith_intensity_ops_per_byte,
#   ops_per_elem, n_elems, achieved_bw_GB_s, achieved_gops_s

set -uo pipefail

# Stop everything cleanly on Ctrl-C (kill the whole process group, including tee).
trap 'printf "\n[$(date +%H:%M:%S)] Interrupted — stopping all runs.\n"; kill 0; exit 130' INT TERM

# ─── Configuration ────────────────────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$REPO_ROOT/results"
CSV="$OUT_DIR/results_${TIMESTAMP}.csv"
LOG="$OUT_DIR/run_${TIMESTAMP}.log"
mkdir -p "$OUT_DIR"

# Your assigned hardware unit ID.
UNIT_ID="${UNIT_ID:-2}"

# Per-test timeout (seconds). If exceeded, the run is killed and the device reset.
TIMEOUT="${TIMEOUT:-60}"

# Speed label: "fast" for normal clock, "slow" for after cool_down (32x slower).
# Set SLOW_MODE=1 to label results as slow (you must run cool_down manually first).
SPEED="${SLOW_MODE:+slow}"; SPEED="${SPEED:-fast}"

# Reset command — executed automatically after any test failure/timeout.
# Uses the BSG cluster reset procedure.
RESET_CMD="${RESET_CMD:-cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}}"

# Apps and their directories relative to repo root.
declare -A APP_DIRS=(
  [sw/1d]="sw/1d"
  [sw/2d]="sw/2d"
  [nw/naive]="nw/naive"
  [nw/baseline]="nw/baseline"
  [nw/efficient]="nw/efficient"
  [dummy/roofline]="dummy/roofline"
  [radix_sort]="radix_sort"
)

# Default run order. nw/efficient is last because it's the most fragile
# (can still hang in Hirschberg inter-sequence sync); we don't want a hang
# there to block collection of the other apps' data.
# FULL_APPS is the normal full sweep:
FULL_APPS=(sw/1d sw/2d nw/naive nw/baseline dummy/roofline radix_sort nw/efficient)
# TEMPORARY: just run nw/naive to smoke-test repeat=1 and calibrate timings.
# Revert DEFAULT_APPS=("${FULL_APPS[@]}") once timings are known.
DEFAULT_APPS=(nw/naive)

# Which apps to run (default: DEFAULT_APPS in order).
if [ $# -gt 0 ]; then
  RUN_APPS=("$@")
else
  RUN_APPS=("${DEFAULT_APPS[@]}")
fi

# ─── Logging helpers ──────────────────────────────────────────────────────────
# All output goes to stdout AND the log file simultaneously.
exec > >(tee -a "$LOG") 2>&1

RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; RESET='\033[0m'
NC='\033[0m'

ts()      { date '+%H:%M:%S'; }
log()     { printf "[%s] %s\n"      "$(ts)" "$*"; }
log_ok()  { printf "[%s] ${GREEN}OK${RESET}  %s\n"   "$(ts)" "$*"; }
log_warn(){ printf "[%s] ${YELLOW}WARN${RESET} %s\n" "$(ts)" "$*"; }
log_err() { printf "[%s] ${RED}ERR${RESET}  %s\n"    "$(ts)" "$*" >&2; }
log_section() { printf "\n[%s] ${CYAN}=== %s ===${RESET}\n" "$(ts)" "$*"; }

# ─── CSV header ───────────────────────────────────────────────────────────────
echo "app,test_name,seq_len,num_seq,repeat,cpg,pod_unique_data,speed,kernel_time_sec,gcups,arith_intensity_ops_per_byte,ops_per_elem,n_elems,achieved_bw_GB_s,achieved_gops_s" > "$CSV"

log "Starting experiment run at $(date)"
log "CSV output  : $CSV"
log "Log output  : $LOG"
log "Apps to run : ${RUN_APPS[*]}"
log "UNIT_ID     : $UNIT_ID"
log "Speed label : $SPEED"
log "Timeout     : ${TIMEOUT}s per test"
[ "${DRY_RUN:-0}" = "1" ] && log_warn "DRY_RUN=1 — no tests will actually execute"
[ "$SPEED" = "slow" ] && log_warn "SLOW_MODE=1: labeling results as 'slow' (assumes cool_down was already run)"
echo ""

# ─── Device reset ─────────────────────────────────────────────────────────────
# Called after any test failure.  Runs RESET_CMD if set, otherwise prints a
# prominent warning so the user knows to reset manually before the next test.
reset_device() {
  local reason="$1"
  printf "\n"
  log_err "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  log_err "  DEVICE RESET REQUIRED — $reason"
  if [ -n "$RESET_CMD" ]; then
    log_err "  Running: $RESET_CMD"
    if eval "$RESET_CMD"; then
      log_warn "  Reset command completed."
    else
      log_err "  Reset command failed!  You may need to reset manually."
    fi
  else
    log_err "  RESET_CMD is not set.  Reset the device manually:"
    log_err "    cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}"
  fi
  log_err "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  printf "\n"
}

# ─── Helpers ─────────────────────────────────────────────────────────────────
extract_param() {
  local name="$1" str="$2"
  # Match   name_VALUE  in a double-underscore-delimited test name
  echo "$str" | grep -oP "(?<=${name}_)[0-9]+" | head -1 || true
}

# Parse a key=value line from exec.log
parse_exec_val() {
  local key="$1" file="$2"
  grep -oP "(?<=${key}=)[0-9.e+\-]+" "$file" 2>/dev/null | tail -1 || true
}

# ─── Run one test dir ─────────────────────────────────────────────────────────
run_test() {
  local app="$1" test_dir="$2"
  local speed="$SPEED"
  local test_name
  test_name="$(basename "$test_dir")"

  # ── Parse parameters from test name ────────────────────────────────────────
  local seq_len num_seq repeat cpg pod_unique ops_per_elem n_elems
  seq_len="$(    extract_param seq-len         "$test_name")"
  num_seq="$(    extract_param num-seq         "$test_name")"
  repeat="$(     extract_param repeat          "$test_name")";  repeat="${repeat:-1}"
  cpg="$(        extract_param cpg             "$test_name")";  cpg="${cpg:-8}"
  pod_unique="$( extract_param pod-unique-data "$test_name")";  pod_unique="${pod_unique:-0}"
  ops_per_elem="$(extract_param ops            "$test_name")"
  n_elems="$(    extract_param n-elems         "$test_name")"

  # ── Dry-run shortcut ───────────────────────────────────────────────────────
  if [ "${DRY_RUN:-0}" = "1" ]; then
    printf "[%s] DRY  %s / %s\n" "$(ts)" "$app" "$test_name"
    echo "$app,$test_name,$seq_len,$num_seq,$repeat,$cpg,$pod_unique,$speed,DRY_RUN,,,,,," >> "$CSV"
    return
  fi

  # ── Build make arguments ───────────────────────────────────────────────────
  local make_args=("HB_MC_DEVICE_ID=${UNIT_ID}")
  if [ "${USE_LINEAR_BARRIER:-0}" = "1" ]; then
    make_args+=("USE_LINEAR_BARRIER=1")
  fi
  # Slow mode: core clock is ~32x slower → reduce repeats ~20x to keep ~20s runs.
  if [ "$SPEED" = "slow" ] && [ -n "$repeat" ] && [ "$repeat" -gt 1 ]; then
    local slow_repeat=$(( repeat / 20 ))
    [ "$slow_repeat" -lt 1 ] && slow_repeat=1
    make_args+=("repeat=${slow_repeat}")
  fi

  # ── Clean before run (required by BSG cluster guide) ──────────────────────
  make -C "$test_dir" clean > /dev/null 2>&1 || true

  # ── Execute ────────────────────────────────────────────────────────────────
  local run_log="$test_dir/run.log"
  local exec_log="$test_dir/exec.log"
  local make_cmd=(make -C "$test_dir" exec.log "${make_args[@]}")

  local run_failed=0
  if [ "${VERBOSE:-0}" = "1" ]; then
    timeout "$TIMEOUT" "${make_cmd[@]}" || run_failed=$?
  else
    timeout "$TIMEOUT" "${make_cmd[@]}" > "$run_log" 2>&1 || run_failed=$?
  fi

  if [ "$run_failed" -ne 0 ]; then
    if [ "$run_failed" -eq 124 ]; then
      printf "[%s] ${YELLOW}TIMEOUT${RESET} %s / %s (after ${TIMEOUT}s) — resetting and continuing\n" "$(ts)" "$app" "$test_name"
      echo "$app,$test_name,$seq_len,$num_seq,$repeat,$cpg,$pod_unique,$speed,TIMEOUT,,,,,," >> "$CSV"
      reset_device "$app / $test_name timed out"
      return  # continue to next test
    elif [ ! -f "$exec_log" ]; then
      printf "[%s] ${RED}COMPILE${RESET} %s / %s (exit %d — check %s)\n" "$(ts)" "$app" "$test_name" "$run_failed" "$run_log"
      tail -10 "$run_log" | while IFS= read -r line; do printf "         %s\n" "$line"; done
      echo "$app,$test_name,$seq_len,$num_seq,$repeat,$cpg,$pod_unique,$speed,COMPILE_ERROR,,,,,," >> "$CSV"
    else
      printf "[%s] ${RED}FAILED ${RESET} %s / %s (exit %d — check %s)\n" "$(ts)" "$app" "$test_name" "$run_failed" "$run_log"
      tail -10 "$run_log" | while IFS= read -r line; do printf "         %s\n" "$line"; done
      echo "$app,$test_name,$seq_len,$num_seq,$repeat,$cpg,$pod_unique,$speed,FAILED,,,,,," >> "$CSV"
      reset_device "$app / $test_name runtime failure"
    fi
    exit 1
  fi

  # ── Extract timing ─────────────────────────────────────────────────────────
  local timing
  timing="$(parse_exec_val kernel_launch_time_sec "$exec_log")"
  if [ -z "$timing" ]; then
    printf "[%s] ${YELLOW}WARN${RESET}    %s / %s  no kernel_launch_time_sec in exec.log\n" \
      "$(ts)" "$app" "$test_name"
    timing="N/A"
  fi

  # ── Extract roofline-specific metrics ─────────────────────────────────────
  local bw_GB_s="" gops_s=""
  if [ "$app" = "dummy/roofline" ] && [ -f "$exec_log" ]; then
    bw_GB_s="$( parse_exec_val achieved_bw_GB_s  "$exec_log")"
    gops_s="$(  parse_exec_val achieved_gops_s   "$exec_log")"
    [ -z "$bw_GB_s"  ] && log_warn "    achieved_bw_GB_s not found in $exec_log"
    [ -z "$gops_s"   ] && log_warn "    achieved_gops_s not found in $exec_log"
  fi

  # ── Compute derived metrics ────────────────────────────────────────────────
  local gcups="N/A" ai="N/A"
  if [[ "$timing" =~ ^[0-9] ]]; then
    if [ -n "$seq_len" ] && [ -n "$num_seq" ]; then
      gcups=$(python3 -c "
t=float('$timing'); sl=int('$seq_len'); ns=int('$num_seq'); rp=int('$repeat')
print(f'{ns*rp*sl*sl/t/1e9:.4f}')" 2>/dev/null || echo "N/A")
      ai=$(python3 -c "print(f'{5*int(\"$seq_len\")/2:.1f}')" 2>/dev/null || echo "N/A")
    elif [ -n "$ops_per_elem" ]; then
      # Roofline kernel: OI = 2*ops_per_elem / 8
      ai=$(python3 -c "print(f'{2*int(\"$ops_per_elem\")/8:.4f}')" 2>/dev/null || echo "N/A")
    fi
  fi

  # ── Report (one line per test) ────────────────────────────────────────────
  if [ "$app" = "dummy/roofline" ]; then
    printf "[%s] ${GREEN}OK${RESET}      %s / %s  time=%ss  AI=%s ops/B  bw=%s GB/s  gops=%s\n" \
      "$(ts)" "$app" "$test_name" "$timing" "$ai" "${bw_GB_s:-N/A}" "${gops_s:-N/A}"
  else
    local warn=""
    if [[ "$timing" =~ ^[0-9] ]] && python3 -c "exit(0 if float('$timing') >= 15 else 1)" 2>/dev/null; then
      printf "[%s] ${GREEN}OK${RESET}      %s / %s  time=%ss  gcups=%s\n" \
        "$(ts)" "$app" "$test_name" "$timing" "$gcups"
    else
      printf "[%s] ${YELLOW}SHORT${RESET}   %s / %s  time=%ss  gcups=%s  (< 15s)\n" \
        "$(ts)" "$app" "$test_name" "$timing" "$gcups"
    fi
  fi

  echo "$app,$test_name,$seq_len,$num_seq,$repeat,$cpg,$pod_unique,$speed,$timing,$gcups,$ai,$ops_per_elem,$n_elems,${bw_GB_s:-},${gops_s:-}" >> "$CSV"
}

# ─── Main loop ────────────────────────────────────────────────────────────────
total_tests=0
passed_tests=0
failed_tests=0

for app in "${RUN_APPS[@]}"; do
  app_rel="${APP_DIRS[$app]:-$app}"
  app_dir="$REPO_ROOT/$app_rel"

  if [ ! -d "$app_dir" ]; then
    log_err "Directory not found for app '$app': $app_dir — skipping"
    continue
  fi

  log_section "$app"

  # Hard-remove ALL previously generated test dirs (those containing parameters.mk),
  # then regenerate from the current tests.mk.  Using find instead of 'make purge'
  # because purge only removes dirs listed in the current TESTS variable — it leaves
  # behind any dirs that were removed from tests.mk since the last generate.
  log "  Regenerating test directories for $app..."
  while IFS= read -r pfile; do
    rm -rf "$(dirname "$pfile")"
  done < <(find "$app_dir" -maxdepth 2 -name "parameters.mk" 2>/dev/null)
  if ! make -C "$app_dir" generate > /dev/null 2>&1; then
    log_err "  'make generate' failed for $app — check $app_dir/Makefile"
    continue
  fi

  # Collect test directories
  mapfile -t test_dirs < <(find "$app_dir" -maxdepth 2 -name "parameters.mk" -exec dirname {} \; | sort)

  if [ ${#test_dirs[@]} -eq 0 ]; then
    log_warn "  No test directories found (parameters.mk missing)"
    continue
  fi

  log "  Found ${#test_dirs[@]} test(s)"

  for tdir in "${test_dirs[@]}"; do
    total_tests=$((total_tests + 1))
    run_test "$app" "$tdir"
  done
done

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
log_section "Run complete"
log "Total tests    : $total_tests"
log "Results CSV    : $CSV"
log "Full log       : $LOG"

# Count failures from CSV (lines with FAILED or TIMEOUT).
# grep -c prints the count AND exits 1 when count==0, so `|| echo 0` would
# emit a second "0" → "0\n0", breaking arithmetic below. Use `|| true` and
# let the prefixed ${...:-0} supply the default.
n_fail=$(grep -c ',FAILED\|,TIMEOUT' "$CSV" 2>/dev/null || true)
n_fail=${n_fail:-0}
n_lines=$(wc -l < "$CSV" 2>/dev/null || echo 0)
n_pass=$(( n_lines - 1 - n_fail ))
log "Passed         : $n_pass"
if [ "$n_fail" -gt 0 ]; then
  log_err "Failed/Timeout : $n_fail"
else
  log "Failed/Timeout : 0"
fi
