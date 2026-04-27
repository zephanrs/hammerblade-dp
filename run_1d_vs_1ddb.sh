#!/usr/bin/env bash
# run_1d_vs_1ddb.sh — compare sw/1d (single-buffer mailbox) vs sw/1ddb
# (double-buffer mailbox) on real hardware at the regular (fast) clock,
# cpg=8, seq_len in {32, 64, 128, 256, 512, 1024}.
#
# Prereqs (same as run_experiments.sh):
#   cd /cluster_src/reset_half && make reset UNIT_ID=$UNIT_ID
#
# Usage:
#   ./run_1d_vs_1ddb.sh                  # run both apps, all sizes
#   UNIT_ID=2 ./run_1d_vs_1ddb.sh
#   TIMEOUT=900 ./run_1d_vs_1ddb.sh
#   DRY_RUN=1 ./run_1d_vs_1ddb.sh        # print commands, do nothing
#
# Output:
#   results/1d_vs_1ddb_<timestamp>.csv
#   results/1d_vs_1ddb_<timestamp>.log

set -uo pipefail
trap 'printf "\n[%s] interrupted\n" "$(date +%H:%M:%S)"; kill 0; exit 130' INT TERM

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$REPO_ROOT/results"
CSV="$OUT_DIR/1d_vs_1ddb_${TS}.csv"
LOG="$OUT_DIR/1d_vs_1ddb_${TS}.log"
mkdir -p "$OUT_DIR"

UNIT_ID="${UNIT_ID:-2}"
TIMEOUT="${TIMEOUT:-600}"
RESET_CMD="${RESET_CMD:-cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}}"

APPS=(sw/1d sw/1ddb)
# (seq_len, num_seq, repeat) — constant-work, ~70 B cells per test (~20s @ 3.5 GCUPS).
# Use 1d's table; cpg=8 is the default, no need to encode in the test name.
SIZES=(
  "32 32768 2048"
  "64 16384 1024"
  "128 8192 512"
  "256 4096 256"
  "512 2048 128"
  "1024 1024 64"
)

exec > >(tee -a "$LOG") 2>&1

ts(){ date '+%H:%M:%S'; }
log(){ printf "[%s] %s\n" "$(ts)" "$*"; }

reset_device(){
  local why="$1"
  log "DEVICE RESET — $why"
  if eval "$RESET_CMD"; then log "reset ok"; else log "reset FAILED"; fi
}

echo "app,seq_len,num_seq,repeat,cpg,kernel_time_sec,gcups" > "$CSV"
log "csv: $CSV"
log "log: $LOG"
log "unit: $UNIT_ID  timeout: ${TIMEOUT}s  apps: ${APPS[*]}"

# Generate a single test directory under <app>/<name>/ with a parameters.mk.
make_test_dir(){
  local app_dir="$1" name="$2" seq_len="$3" num_seq="$4" repeat="$5"
  local d="$app_dir/$name"
  mkdir -p "$d"
  cat > "$d/Makefile" <<'EOF'
include ../template.mk
EOF
  cat > "$d/app_path.mk" <<'EOF'
include ../app_path.mk
EOF
  cat > "$d/parameters.mk" <<EOF
test-name = ${name}
num-seq = ${num_seq}
seq-len = ${seq_len}
repeat = ${repeat}
EOF
}

run_one(){
  local app="$1" seq_len="$2" num_seq="$3" repeat="$4"
  local app_dir="$REPO_ROOT/$app"
  local name="seq-len_${seq_len}__num-seq_${num_seq}__repeat_${repeat}"
  local d="$app_dir/$name"

  make_test_dir "$app_dir" "$name" "$seq_len" "$num_seq" "$repeat"

  if [ "${DRY_RUN:-0}" = "1" ]; then
    log "DRY  $app/$name"
    echo "$app,$seq_len,$num_seq,$repeat,8,DRY_RUN," >> "$CSV"
    return
  fi

  make -C "$d" clean > /dev/null 2>&1 || true

  local run_log="$d/run.log"
  local exec_log="$d/exec.log"
  local rc=0
  timeout "$TIMEOUT" make -C "$d" exec.log "HB_MC_DEVICE_ID=${UNIT_ID}" \
    > "$run_log" 2>&1 || rc=$?

  if [ "$rc" -ne 0 ]; then
    if [ "$rc" -eq 124 ]; then
      log "TIMEOUT $app/$name"
      echo "$app,$seq_len,$num_seq,$repeat,8,TIMEOUT," >> "$CSV"
      reset_device "$app/$name timed out"
      return
    fi
    log "FAILED  $app/$name (rc=$rc, see $run_log)"
    tail -10 "$run_log" | sed 's/^/    /'
    echo "$app,$seq_len,$num_seq,$repeat,8,FAILED," >> "$CSV"
    reset_device "$app/$name failed"
    return
  fi

  local t
  t="$(grep -oP '(?<=kernel_launch_time_sec=)[0-9.eE+\-]+' "$exec_log" | tail -1 || true)"
  if [ -z "$t" ]; then
    log "WARN  $app/$name no kernel_launch_time_sec"
    echo "$app,$seq_len,$num_seq,$repeat,8,N/A," >> "$CSV"
    return
  fi
  local gcups
  gcups="$(python3 -c "
t=float('$t'); sl=$seq_len; ns=$num_seq; rp=$repeat
print(f'{ns*rp*sl*sl/t/1e9:.4f}')")"
  log "OK    $app/$name  t=${t}s  gcups=${gcups}"
  echo "$app,$seq_len,$num_seq,$repeat,8,$t,$gcups" >> "$CSV"
}

for app in "${APPS[@]}"; do
  log "=== $app ==="
  for s in "${SIZES[@]}"; do
    read -r seq_len num_seq repeat <<<"$s"
    run_one "$app" "$seq_len" "$num_seq" "$repeat"
  done
done

log "done — $CSV"
