#!/usr/bin/env bash
# run_experiments.sh — build and run a named HammerBlade experiment.
#
# Runs on real ASIC hardware.  Before running, you MUST have already done:
#   cd /cluster_src/reset_half && make reset UNIT_ID=2
#
# Usage:
#   ./run_experiments.sh <experiment-name>
#
# The experiment name dispatches to a fixed set of apps + a speed.  See
# EXPERIMENTS.md for the canonical list.  Slow experiments automatically
# invoke `make cool_down UNIT_ID=$UNIT_ID` from /cluster_src/reset_half
# before launching tests; the script will prompt for sudo if cool_down
# requires it.
#
# Run with no arguments (or --help) to see the list of registered names.
#
# Output:
#   results/results_<timestamp>.csv    — timing data
#   results/run_<timestamp>.log        — full log of this run
#
# CSV columns:
#   app, test_name, seq_len, num_seq, repeat, cpg, pod_unique_data,
#   speed, kernel_time_sec, gcups, arith_intensity_ops_per_byte,
#   ops_per_elem, n_elems, achieved_bw_GB_s, achieved_gops_s
#
# In SLOW rows the throughput columns (gcups, achieved_bw_GB_s,
# achieved_gops_s) are already sim32bw-projected — i.e. slow time scaled
# down by 32× (and for gcups, additionally accounting for the /16 repeat
# divisor) so they're directly comparable to fast rows.  kernel_time_sec
# remains the raw measured slow wall time.

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

# Per-test timeout (seconds).  All experiments target ~20 s wall (slow runs
# scale repeat to land in the same range), so 60 s gives 3× headroom.
# Timed-out rows are retried once before being recorded as TIMEOUT.
TIMEOUT="${TIMEOUT:-60}"

# SPEED is set by the experiment registry above.  Slow experiments
# automatically invoke cool_down (see "Pre-flight cool_down" below).

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
  [dummy/barrier_bench]="dummy/barrier_bench"
  [radix_sort]="radix_sort"
)

# ─── Experiment registry ──────────────────────────────────────────────────────
# Each named experiment maps to a set of apps + a speed label.  EXPERIMENTS.md
# is the source of truth for what each one runs and how its tests.mk is
# calibrated.  Slow experiments automatically run cool_down before launch.
declare -A EXPERIMENT_APPS=(
  [sw1d_cpg_fast]="sw/1d"
  [sw1d_cpg_slow]="sw/1d"
  [sw1d_preflight]="sw/1d"
  [sw2d_seqlen_fast]="sw/2d"
  [sw2d_seqlen_slow]="sw/2d"
  [nw_seqlen_fast]="nw/baseline nw/efficient"
  [nw_naive_fast]="nw/naive"
  [radix_sort_fast]="radix_sort"
  [radix_sort_slow]="radix_sort"
  [roofline_fast]="dummy/roofline"
  [roofline_slow]="dummy/roofline"
  [barrier_fast]="dummy/barrier_bench"
)

declare -A EXPERIMENT_SPEED=(
  [sw1d_cpg_fast]=fast
  [sw1d_cpg_slow]=slow
  [sw1d_preflight]=fast
  [sw2d_seqlen_fast]=fast
  [sw2d_seqlen_slow]=slow
  [nw_seqlen_fast]=fast
  [nw_naive_fast]=fast
  [radix_sort_fast]=fast
  [radix_sort_slow]=slow
  [roofline_fast]=fast
  [roofline_slow]=slow
  [barrier_fast]=fast
)

# Per-experiment row whitelist.  Rows whose test_name doesn't match an entry
# in the whitelist are skipped.  Unset → all rows in tests.mk run.
#
# Used for slow experiments that subset the fast tests.mk (e.g. sw1d_cpg_slow
# keeps only the largest seq_len per CPG — 8 rows out of 50).
declare -A EXPERIMENT_ROW_FILTER=(
  [sw1d_cpg_slow]="seq-len_256__num-seq_4080__repeat_256__cpg_1
seq-len_512__num-seq_2032__repeat_128__cpg_2
seq-len_1024__num-seq_1008__repeat_64__cpg_4
seq-len_2048__num-seq_496__repeat_32
seq-len_4096__num-seq_240__repeat_16__cpg_16
seq-len_8192__num-seq_112__repeat_8__cpg_32
seq-len_16384__num-seq_48__repeat_4__cpg_64
seq-len_32768__num-seq_24__repeat_3__cpg_128"
)

print_experiments() {
  cat >&2 <<EOF
Usage: $0 <experiment-name>

Available experiments (see EXPERIMENTS.md):
  sw1d_cpg_fast       sw/1d CPG × seq_len sweep, fast clock        (50 runs)
  sw1d_cpg_slow       sw/1d, largest seq_len per CPG, slow clock   ( 8 runs)
  sw1d_preflight      sw/1d suspect-row probes, repeat=1 (use preflight.sh)
  sw2d_seqlen_fast    sw/2d seq_len sweep, fast clock              ( 6 runs)
  sw2d_seqlen_slow    sw/2d seq_len sweep, slow clock              ( 6 runs)
  nw_seqlen_fast      nw/{baseline,efficient} seq_len, fast        ( 6 runs)
  nw_naive_fast       nw/naive seq_len, fast (split: hangs at scale)( 3 runs)
  radix_sort_fast     radix_sort SIZE × num_arr sweep, fast        (18 runs)
  radix_sort_slow     radix_sort, slow clock                       (18 runs)
  roofline_fast       dummy/roofline OPS sweep, fast               (32 runs)
  roofline_slow       dummy/roofline OPS sweep, slow               (32 runs)
  barrier_fast        dummy/barrier_bench (default vs linear), fast ( 2 runs)
EOF
}

if [ $# -ne 1 ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  print_experiments
  exit 1
fi

EXPERIMENT="$1"
if [ -z "${EXPERIMENT_APPS[$EXPERIMENT]:-}" ]; then
  printf "Unknown experiment: %s\n\n" "$EXPERIMENT" >&2
  print_experiments
  exit 1
fi

# shellcheck disable=SC2206
RUN_APPS=( ${EXPERIMENT_APPS[$EXPERIMENT]} )
SPEED="${EXPERIMENT_SPEED[$EXPERIMENT]}"

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
log "Experiment  : $EXPERIMENT"
log "CSV output  : $CSV"
log "Log output  : $LOG"
log "Apps to run : ${RUN_APPS[*]}"
log "UNIT_ID     : $UNIT_ID"
log "Speed label : $SPEED"
log "Timeout     : ${TIMEOUT}s per test"
[ "${DRY_RUN:-0}" = "1" ] && log_warn "DRY_RUN=1 — no tests will actually execute"
echo ""

# ─── Pre-flight reset (always) + cool_down (slow only) ───────────────────────
# Always reset so the device starts in a known fast-clock state, regardless
# of what previous runs left it in.  For slow experiments, chain cool_down
# after the reset (cool_down engages the 32× slow clock).  Both commands run
# in the visible terminal — sudo prompts surface to the user (per project
# guidance: never silence reset/cool_down output).
if [ "${DRY_RUN:-0}" != "1" ]; then
  log "Pre-flight reset (may prompt for sudo password)"
  if ( cd /cluster_src/reset_half && make reset UNIT_ID="$UNIT_ID" ); then
    log "reset complete"
  else
    log_err "reset failed — aborting"
    exit 1
  fi
  echo ""

  if [ "$SPEED" = "slow" ]; then
    log "Slow experiment — running cool_down (may prompt for sudo password)"
    if ( cd /cluster_src/reset_half && make cool_down UNIT_ID="$UNIT_ID" ); then
      log "cool_down complete"
    else
      log_err "cool_down failed — aborting"
      exit 1
    fi
    echo ""
  fi
fi

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

# Parse the Cudalite kernel execution time from exec.log.
# Format:  INFO:    Cudalite kernels execution time = 5763366 us
# Returns the value in seconds (µs/1e6) for higher precision than
# kernel_launch_time_sec, especially for short kernels (<5 s) where
# host-side wall-clock timing has a noticeable jitter floor.
parse_cudalite_sec() {
  local file="$1"
  local us
  us=$(grep -oP 'Cudalite kernels execution time\s*=\s*\K[0-9]+' "$file" 2>/dev/null | tail -1 || true)
  [ -z "$us" ] && return 0
  python3 -c "print(${us}/1e6)" 2>/dev/null
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

  # Slow-mode repeat scaling — per experiment, since "compute-bound" vs
  # "memory-bound" determines how much real-time slow steals.
  #
  #   sw1d_cpg_slow / sw2d_seqlen_slow:  compute-bound, /16
  #   roofline_slow:                      compute-bound at high AI, /16
  #                                       (low-AI rows under-shoot, fine)
  #   radix_sort_slow:                    SIZE < 65 K compute-bound (/16),
  #                                       SIZE ≥ 65 K DRAM-bound (/2);
  #                                       knob is num-arr, not repeat.
  if [ "$SPEED" = "slow" ]; then
    case "$EXPERIMENT" in
      radix_sort_slow)
        # Test name format: radix_sort_<SIZE>__num-arr_<N>
        local vec_size num_arr
        vec_size="$(extract_param radix_sort "$test_name")"
        num_arr="$(extract_param num-arr     "$test_name")"
        if [ -n "$num_arr" ] && [ "$num_arr" -gt 1 ]; then
          local slow_num_arr
          if [ -n "$vec_size" ] && [ "$vec_size" -ge 65536 ]; then
            slow_num_arr=$(( num_arr / 2 ))
          else
            slow_num_arr=$(( num_arr / 16 ))
          fi
          [ "$slow_num_arr" -lt 1 ] && slow_num_arr=1
          make_args+=("num-arr=${slow_num_arr}")
        fi
        ;;
      *)
        if [ -n "$repeat" ] && [ "$repeat" -gt 1 ]; then
          local slow_repeat=$(( repeat / 16 ))
          [ "$slow_repeat" -lt 1 ] && slow_repeat=1
          make_args+=("repeat=${slow_repeat}")
        fi
        ;;
    esac
  fi

  # ── Execute (with retry-once on timeout) ─────────────────────────────────
  # `make clean` runs at the start of every attempt — without it, the second
  # attempt sees the stale exec.log from the timed-out run and short-
  # circuits, leaving us with empty timing lines.
  local run_log="$test_dir/run.log"
  local exec_log="$test_dir/exec.log"
  local make_cmd=(make -C "$test_dir" exec.log "${make_args[@]}")

  local run_failed=0
  local attempt
  for attempt in 1 2; do
    run_failed=0
    make -C "$test_dir" clean > /dev/null 2>&1 || true
    if [ "${VERBOSE:-0}" = "1" ]; then
      timeout "$TIMEOUT" "${make_cmd[@]}" || run_failed=$?
    else
      timeout "$TIMEOUT" "${make_cmd[@]}" > "$run_log" 2>&1 || run_failed=$?
    fi
    # Anything other than timeout → don't retry, drop into the dispatcher.
    [ "$run_failed" -ne 124 ] && break
    # First timeout → reset and try again (real-ASIC non-determinism in
    # vcache/wormhole/NoC state can push a borderline row over once).
    if [ "$attempt" -eq 1 ]; then
      printf "[%s] ${YELLOW}TIMEOUT${RESET} %s / %s (after ${TIMEOUT}s, attempt 1) — resetting and retrying once\n" \
        "$(ts)" "$app" "$test_name"
      reset_device "$app / $test_name timed out (attempt 1)"
    fi
  done

  if [ "$run_failed" -ne 0 ]; then
    if [ "$run_failed" -eq 124 ]; then
      printf "[%s] ${YELLOW}TIMEOUT${RESET} %s / %s (after ${TIMEOUT}s, retry also timed out) — recording and continuing\n" "$(ts)" "$app" "$test_name"
      echo "$app,$test_name,$seq_len,$num_seq,$repeat,$cpg,$pod_unique,$speed,TIMEOUT,,,,,," >> "$CSV"
      reset_device "$app / $test_name timed out (both attempts)"
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
  # Prefer the device-side Cudalite µs counter (accurate to 1 µs); fall back
  # to the host-side kernel_launch_time_sec if the µs line is missing OR the
  # parsed value isn't a sensible positive number (e.g. parser returned blank
  # / zero / non-numeric).  The host print is from print_kernel_launch_time
  # in main.cpp and is essentially always present on a successful kernel run.
  local timing_cuda timing_host timing="" timing_src=""
  timing_cuda="$(parse_cudalite_sec "$exec_log")"
  timing_host="$(parse_exec_val kernel_launch_time_sec "$exec_log")"
  if [[ "$timing_cuda" =~ ^[0-9]+\.?[0-9]*$ ]] && [ "${timing_cuda%.*}" != "0" -o "${timing_cuda#0.}" != "0" ]; then
    timing="$timing_cuda"
    timing_src="cudalite_us"
  elif [[ "$timing_host" =~ ^[0-9]+\.?[0-9]*([eE][+-]?[0-9]+)?$ ]]; then
    timing="$timing_host"
    timing_src="host_print"
  fi
  if [ -z "$timing" ]; then
    printf "[%s] ${YELLOW}WARN${RESET}    %s / %s  no kernel timing in exec.log (cuda='%s' host='%s')\n" \
      "$(ts)" "$app" "$test_name" "$timing_cuda" "$timing_host"
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

  # ── Slow-mode → sim32bw projection ────────────────────────────────────────
  # Slow clock is 32× slower (compute) / 5.6× slower (memory).  We report
  # numbers projected onto a "fast clock + 32× memory BW" world: scale
  # measured slow time by 1/32.  Equivalent: multiply throughput by 32.
  #
  # GCUPS additionally needs the slow-mode repeat divisor accounted for —
  # we passed repeat=$repeat/16 to make, so actual cells executed were
  # 1/16 of what the test_name encodes.  Net factor for GCUPS in slow:
  #   gcups_proj = (cells_actual × 32) / t = (cells_csv / 16) × 32 / t
  #              = 2 × (cells_csv / t)
  # radix_sort_slow is excluded — it scales num-arr (a different work knob)
  # and doesn't compute GCUPS.
  local gcups_factor=1   # multiplied into the cells_csv/t formula
  local bw_factor=1      # multiplied into the kernel's reported BW / GOPS
  if [ "$SPEED" = "slow" ] && [ "$EXPERIMENT" != "radix_sort_slow" ]; then
    gcups_factor=2       # = 32/16 (sim32bw / repeat-div)
    bw_factor=32         # roofline already reflects actual repeat in its
                         # internal measurement, so just project ×32
  fi

  # ── Compute derived metrics ────────────────────────────────────────────────
  local gcups="N/A" ai="N/A"
  if [[ "$timing" =~ ^[0-9] ]]; then
    if [ -n "$seq_len" ] && [ -n "$num_seq" ]; then
      gcups=$(python3 -c "
t=float('$timing'); sl=int('$seq_len'); ns=int('$num_seq'); rp=int('$repeat')
f=${gcups_factor}
print(f'{f*ns*rp*sl*sl/t/1e9:.4f}')" 2>/dev/null || echo "N/A")
      ai=$(python3 -c "print(f'{5*int(\"$seq_len\")/2:.1f}')" 2>/dev/null || echo "N/A")
    elif [ -n "$ops_per_elem" ]; then
      # Roofline kernel: OI = 2*ops_per_elem / 8
      ai=$(python3 -c "print(f'{2*int(\"$ops_per_elem\")/8:.4f}')" 2>/dev/null || echo "N/A")
    fi
  fi

  # Project roofline BW/GOPS for slow runs (sim32bw).
  if [ "$bw_factor" != "1" ]; then
    [ -n "$bw_GB_s" ] && bw_GB_s=$(python3 -c "print(f'{${bw_factor}*float(\"$bw_GB_s\"):.4f}')" 2>/dev/null || echo "$bw_GB_s")
    [ -n "$gops_s"  ] && gops_s=$(python3 -c "print(f'{${bw_factor}*float(\"$gops_s\"):.4f}')" 2>/dev/null || echo "$gops_s")
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
  gen_log=$(mktemp)
  if ! make -C "$app_dir" generate > "$gen_log" 2>&1; then
    log_err "  'make generate' failed for $app — output below:"
    tail -20 "$gen_log" | while IFS= read -r line; do printf "         %s\n" "$line"; done
    rm -f "$gen_log"
    continue
  fi
  rm -f "$gen_log"

  # Collect test directories
  mapfile -t test_dirs < <(find "$app_dir" -maxdepth 2 -name "parameters.mk" -exec dirname {} \; | sort)

  if [ ${#test_dirs[@]} -eq 0 ]; then
    log_warn "  No test directories found (parameters.mk missing)"
    continue
  fi

  # Apply per-experiment row filter (if any) — used by slow experiments that
  # subset the fast tests.mk.  An unset/empty filter keeps all rows.
  row_filter="${EXPERIMENT_ROW_FILTER[$EXPERIMENT]:-}"
  if [ -n "$row_filter" ]; then
    filtered_dirs=()
    for tdir in "${test_dirs[@]}"; do
      tname="$(basename "$tdir")"
      if echo "$row_filter" | grep -qx "$tname"; then
        filtered_dirs+=("$tdir")
      else
        log "  Skipping $tname (not in $EXPERIMENT row filter)"
      fi
    done
    test_dirs=("${filtered_dirs[@]}")
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

# ─── Archive on user confirmation ────────────────────────────────────────────
# Prompts the user to mark this run as successful.  On "y", moves CSV + log
# into results/success/, so failed/aborted runs stay easy to ignore in
# results/.  Skipped automatically when stdin isn't a TTY (nohup, CI) — in
# that case mark by hand later.
SUCCESS_DIR="$OUT_DIR/success"
if [ -t 0 ] && [ "${DRY_RUN:-0}" != "1" ]; then
  echo ""
  printf "[%s] Mark this run as successful and archive to %s? [y/N] " "$(ts)" "$SUCCESS_DIR"
  read -r reply
  case "$reply" in
    y|Y|yes|YES)
      mkdir -p "$SUCCESS_DIR"
      mv "$CSV" "$SUCCESS_DIR/"
      mv "$LOG" "$SUCCESS_DIR/"
      log_ok "Archived $(basename "$CSV") and $(basename "$LOG") to $SUCCESS_DIR"
      ;;
    *)
      log "Left in $OUT_DIR (not archived)"
      ;;
  esac
fi
