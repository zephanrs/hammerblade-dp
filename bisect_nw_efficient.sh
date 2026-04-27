#!/usr/bin/env bash
# bisect_nw_efficient.sh — narrow down the num_seq cliff for nw/efficient on hardware.
#
# We know: num_seq=256 passes, num_seq=512 hangs (with seq_len=32, repeat=1).
# This script runs a ladder of multiples-of-16 sizes between them and prints
# a one-line PASS/FAIL summary per size.
#
# Reset the device once before running this, then let it run to completion.
# It auto-resets after each timeout.
#
# Usage (on the cluster):
#   cd /home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs
#   git pull
#   bash bisect_nw_efficient.sh > /tmp/nw_eff_bisect.log 2>&1

set -uo pipefail

REPO_ROOT="/home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs"
APP_DIR="$REPO_ROOT/nw/efficient"
UNIT_ID="${UNIT_ID:-2}"
TIMEOUT="${TIMEOUT:-60}"
RESET_CMD="cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}"

# Multiples of 16 between the known-pass (256) and known-fail (512) sizes,
# plus a bit beyond to confirm the cliff.
SIZES=(256 320 384 448 480 496 512 768 1024)

cd "$APP_DIR"

# Generate one-off test dirs for each size.
for n in "${SIZES[@]}"; do
  d="seq-len_32__num-seq_${n}__repeat_1"
  mkdir -p "$d"
  echo 'include ../template.mk' > "$d/Makefile"
  echo 'include ../app_path.mk' > "$d/app_path.mk"
  cat > "$d/parameters.mk" <<EOF
test-name = $d
num-seq = $n
seq-len = 32
repeat = 1
EOF
done

printf 'num_seq\tresult\twall_s\tkernel_us\tdetail\n'

for n in "${SIZES[@]}"; do
  d="$APP_DIR/seq-len_32__num-seq_${n}__repeat_1"
  cd "$d"
  make clean > /dev/null 2>&1

  start_s=$(date +%s)
  rc=0
  timeout "$TIMEOUT" make exec.log "HB_MC_DEVICE_ID=${UNIT_ID}" > run.log 2>&1 || rc=$?
  end_s=$(date +%s)
  wall=$(( end_s - start_s ))

  # BSG-library-reported kernel time (more accurate than host clock_gettime).
  # Search both run.log (everything) and exec.log (test program's tee'd output).
  kus=$(grep -ohP '(?<=Cudalite kernels execution time = )[0-9]+' run.log exec.log 2>/dev/null | tail -1)
  kus="${kus:-NA}"

  # Snapshot of any validation/error message — first matching line.
  detail=$(grep -m1 -hE "Mismatch:|Invalid path point:|Path score mismatch:|REGRESSION TEST (FAILED|PASSED)|Segmentation fault|Aborted|core dumped" run.log exec.log 2>/dev/null | sed 's/[[:space:]]*$//' | head -c 120)
  detail="${detail:-(none)}"

  result=""
  if [ "$rc" -eq 124 ]; then
    result="TIMEOUT"
    bash -c "$RESET_CMD" > /dev/null 2>&1
  elif grep -qE "REGRESSION TEST PASSED" run.log exec.log 2>/dev/null; then
    result="PASS"
  elif grep -qE "REGRESSION TEST FAILED" run.log exec.log 2>/dev/null; then
    result="FAIL_VALIDATION"
  elif [ -n "$kus" ] && [ "$kus" != "NA" ]; then
    # Kernel ran (we have kernel_us) but no PASSED/FAILED marker — host crashed
    # mid-validation, OR the regression marker went to a place we didn't search.
    result="HOST_CRASH_OR_NO_MARKER"
  else
    result="UNKNOWN(rc=${rc})"
    bash -c "$RESET_CMD" > /dev/null 2>&1
  fi

  # Always preserve last 20 lines of the combined output so we can see what
  # really happened on weird outcomes — these stay in the per-test dir.
  {
    echo "=== run.log (last 20) ==="
    tail -20 run.log 2>/dev/null
    echo "=== exec.log (last 20) ==="
    tail -20 exec.log 2>/dev/null
  } > diag.log

  printf '%d\t%s\t%d\t%s\t%s\n' "$n" "$result" "$wall" "$kus" "$detail"
done
