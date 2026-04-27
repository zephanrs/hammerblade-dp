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

printf 'num_seq\tresult\twall_s\tkernel_us\n'

for n in "${SIZES[@]}"; do
  d="$APP_DIR/seq-len_32__num-seq_${n}__repeat_1"
  cd "$d"
  make clean > /dev/null 2>&1

  start_s=$(date +%s)
  rc=0
  timeout "$TIMEOUT" make exec.log "HB_MC_DEVICE_ID=${UNIT_ID}" > run.log 2>&1 || rc=$?
  end_s=$(date +%s)
  wall=$(( end_s - start_s ))

  # Try to extract the BSG-library-reported kernel time (more accurate than
  # the host-side clock_gettime).
  kus=$(grep -oP '(?<=Cudalite kernels execution time = )[0-9]+' run.log | tail -1)
  kus="${kus:-NA}"

  if [ "$rc" -eq 124 ]; then
    printf '%d\tTIMEOUT\t%d\t%s\n' "$n" "$wall" "$kus"
    bash -c "$RESET_CMD" > /dev/null 2>&1
  elif grep -q "BSG REGRESSION TEST PASSED" run.log; then
    printf '%d\tPASS\t%d\t%s\n' "$n" "$wall" "$kus"
  elif grep -q "Mismatch" run.log; then
    # Kernel finished but produced wrong results — informative.
    printf '%d\tMISMATCH\t%d\t%s\n' "$n" "$wall" "$kus"
  else
    printf '%d\tFAIL(rc=%d)\t%d\t%s\n' "$n" "$rc" "$wall" "$kus"
    # Reset to be safe.
    bash -c "$RESET_CMD" > /dev/null 2>&1
  fi
done
