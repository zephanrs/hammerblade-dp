#!/usr/bin/env bash
# bisect_nw_efficient.sh — narrow down the num_seq cliff for nw/efficient on hardware.
#
# Classification: simple grep for "BSG REGRESSION TEST PASSED" anywhere in the
# captured output. PASS if found, FAIL otherwise. TIMEOUT if 60s elapsed.
#
# Usage (on the cluster):
#   cd /home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs
#   git pull
#   bash bisect_nw_efficient.sh

set -uo pipefail

REPO_ROOT="/home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs"
APP_DIR="$REPO_ROOT/nw/efficient"
UNIT_ID="${UNIT_ID:-2}"
TIMEOUT="${TIMEOUT:-60}"
RESET_CMD="cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}"

SIZES=(256 320 384 448 480 496 512 768 1024)

cd "$APP_DIR"

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

  kus=$(grep -ohP '(?<=Cudalite kernels execution time = )[0-9]+' run.log exec.log 2>/dev/null | tail -1)
  kus="${kus:-NA}"

  if [ "$rc" -eq 124 ]; then
    result="TIMEOUT"
    bash -c "$RESET_CMD" > /dev/null 2>&1
  elif grep -q "BSG REGRESSION TEST PASSED" run.log exec.log 2>/dev/null; then
    result="PASS"
  else
    result="FAIL"
  fi

  printf '%d\t%s\t%d\t%s\n' "$n" "$result" "$wall" "$kus"
done
