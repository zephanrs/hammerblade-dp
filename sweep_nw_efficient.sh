#!/usr/bin/env bash
# sweep_nw_efficient.sh — scale num_seq up to FASTA limits for seq_len 32/64/128,
# choosing num_seq values that avoid the d_path 64-KB-multiple cache cliff we
# discovered, plus a couple of cliff sentinels per seq_len to keep the
# hypothesis honest.
#
# d_path size in bytes = num_seq * seq_len * 4. Hangs were observed when this
# is an integer multiple of 65536 (= 64 KB = half the vcache). For seq_len:
#   32  → cliff every num_seq=512
#   64  → cliff every num_seq=256
#  128  → cliff every num_seq=128
#
# We pick num_seq values that are multiples of 16 (barrier requirement) but
# offset from the cliff multiples. Cliff sentinels are tagged in the output.
#
# Output: tsv with seq_len, num_seq, expected (SAFE/CLIFF), result, wall_s,
# kernel_us. PASS for SAFE means the cliff is the only issue; FAIL/TIMEOUT
# for CLIFF confirms the theory.

set -uo pipefail

trap 'echo; echo "interrupted"; kill 0 2>/dev/null; exit 130' INT TERM

REPO_ROOT="/home/zephans/bsg_bladerunner/bsg_replicant/examples/hb_hammerbench/apps/programs"
APP_DIR="$REPO_ROOT/nw/efficient"
UNIT_ID="${UNIT_ID:-2}"
TIMEOUT="${TIMEOUT:-90}"
RESET_CMD="cd /cluster_src/reset_half && make reset UNIT_ID=${UNIT_ID}"

# (seq_len, num_seq, label) tuples — label is SAFE or CLIFF.
# FASTA caps: num_seq*seq_len <= 1,048,576.
TESTS=(
  # seq_len=32 (FASTA cap: num_seq <= 32768; cliff every 512)
  "32 16 SAFE"
  "32 256 SAFE"
  "32 1008 SAFE"
  "32 4080 SAFE"
  "32 16368 SAFE"
  "32 32752 SAFE"
  "32 512 CLIFF"
  "32 16384 CLIFF"
  # seq_len=64 (FASTA cap: num_seq <= 16384; cliff every 256)
  "64 16 SAFE"
  "64 240 SAFE"
  "64 1008 SAFE"
  "64 4080 SAFE"
  "64 16368 SAFE"
  "64 256 CLIFF"
  "64 4096 CLIFF"
  # seq_len=128 (FASTA cap: num_seq <= 8192; cliff every 128)
  "128 16 SAFE"
  "128 112 SAFE"
  "128 1008 SAFE"
  "128 4080 SAFE"
  "128 8176 SAFE"
  "128 128 CLIFF"
  "128 4096 CLIFF"
)

cd "$APP_DIR"

# Generate test dirs.
for tup in "${TESTS[@]}"; do
  read -r sl ns _ <<<"$tup"
  d="seq-len_${sl}__num-seq_${ns}__repeat_1"
  mkdir -p "$d"
  echo 'include ../template.mk' > "$d/Makefile"
  echo 'include ../app_path.mk' > "$d/app_path.mk"
  cat > "$d/parameters.mk" <<EOF
test-name = $d
num-seq = $ns
seq-len = $sl
repeat = 1
EOF
done

strip_ansi() { sed -r 's/\x1B\[[0-9;]*[a-zA-Z]//g' "$@" 2>/dev/null; }

printf 'seq_len\tnum_seq\texpect\tresult\twall_s\tkernel_us\tdpath_kb\n'

for tup in "${TESTS[@]}"; do
  read -r sl ns label <<<"$tup"
  d="$APP_DIR/seq-len_${sl}__num-seq_${ns}__repeat_1"
  cd "$d"
  make clean > /dev/null 2>&1

  start_s=$(date +%s)
  rc=0
  timeout --foreground -k 5 "$TIMEOUT" make exec.log "HB_MC_DEVICE_ID=${UNIT_ID}" \
    > run.log 2>&1 || rc=$?
  end_s=$(date +%s)
  wall=$(( end_s - start_s ))

  pkill -9 -f test_loader 2>/dev/null
  pkill -9 -f 'make exec.log' 2>/dev/null
  sleep 1

  combined=$(strip_ansi run.log exec.log)
  kus=$(printf '%s' "$combined" | grep -oP '(?<=Cudalite kernels execution time = )[0-9]+' | tail -1)
  kus="${kus:-NA}"
  dpath_kb=$(( ns * sl * 4 / 1024 ))

  if [ "$rc" -eq 124 ]; then
    result="TIMEOUT"
  elif printf '%s' "$combined" | grep -q "REGRESSION TEST PASSED"; then
    result="PASS"
  else
    result="FAIL"
  fi

  printf '%d\t%d\t%s\t%s\t%d\t%s\t%d\n' \
    "$sl" "$ns" "$label" "$result" "$wall" "$kus" "$dpath_kb"

  if [ "$result" != "PASS" ]; then
    echo "  resetting device after ${result}..."
    bash -c "$RESET_CMD"
    sleep 2
  fi
done
