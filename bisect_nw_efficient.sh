#!/usr/bin/env bash
# bisect_nw_efficient.sh — narrow down the num_seq cliff for nw/efficient on hardware.

set -uo pipefail

# Kill the whole process group on Ctrl-C so timeout/make children die too.
trap 'echo; echo "interrupted"; kill 0 2>/dev/null; exit 130' INT TERM

REPO_ROOT="/home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs"
APP_DIR="$REPO_ROOT/nw/efficient"
UNIT_ID="${UNIT_ID:-2}"
TIMEOUT="${TIMEOUT:-60}"
RESET_CMD="cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}"

SIZES=(256 320 384 448 480 496 512 768 1024 1280 1536 1792 2048 2560 3072 4096 5120 6144)

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

# Strip ANSI color escapes (e.g. \033[0;32m) so grep matches "PASSED"
# regardless of whether the BSG framework wrote color codes.
strip_ansi() { sed -r 's/\x1B\[[0-9;]*[a-zA-Z]//g' "$@" 2>/dev/null; }

printf 'num_seq\tresult\twall_s\tkernel_us\n'

for n in "${SIZES[@]}"; do
  d="$APP_DIR/seq-len_32__num-seq_${n}__repeat_1"
  cd "$d"
  make clean > /dev/null 2>&1

  start_s=$(date +%s)
  rc=0
  # `timeout --foreground` lets Ctrl-C in the parent shell propagate.
  timeout --foreground "$TIMEOUT" make exec.log "HB_MC_DEVICE_ID=${UNIT_ID}" \
    > run.log 2>&1 || rc=$?
  end_s=$(date +%s)
  wall=$(( end_s - start_s ))

  # Strip ANSI before grepping.
  combined=$(strip_ansi run.log exec.log)

  kus=$(printf '%s' "$combined" | grep -oP '(?<=Cudalite kernels execution time = )[0-9]+' | tail -1)
  kus="${kus:-NA}"

  if [ "$rc" -eq 124 ]; then
    result="TIMEOUT"
  elif printf '%s' "$combined" | grep -q "REGRESSION TEST PASSED"; then
    result="PASS"
  else
    result="FAIL"
  fi

  printf '%d\t%s\t%d\t%s\n' "$n" "$result" "$wall" "$kus"

  # Reset between any non-PASS so a stuck device doesn't cascade. Reset
  # output goes straight to the terminal so any password prompt is visible.
  if [ "$result" != "PASS" ]; then
    echo "  resetting device after ${result}..."
    bash -c "$RESET_CMD"
    sleep 2
  fi
done
