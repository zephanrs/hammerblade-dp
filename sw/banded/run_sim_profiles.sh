#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename -- "${BASH_SOURCE[0]}")"
RUN_ROOT="${SCRIPT_DIR}/sim-profile-runs"
TIMEOUT_LIMIT=90m
MODULE_LOAD_CMD="${MODULE_LOAD_CMD:-module load hammerblade}"
MAX_PARALLEL=3

FIXED_SEQ_LEN=128
FIXED_NUM_SEQ=64
FIXED_COL=4
FIXED_BAND=64
BAND_SWEEP=(8 16 32 64 128)
COL_SWEEP=(1 2 8 16)

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

test_name() {
  local seq_len="$1"
  local band_size="$2"
  local num_seq="$3"
  local col="$4"
  printf 'seq-len_%s__band-size_%s__num-seq_%s__col_%s' \
    "${seq_len}" "${band_size}" "${num_seq}" "${col}"
}

make_test_dir() {
  local seq_len="$1"
  local band_size="$2"
  local num_seq="$3"
  local col="$4"
  local name
  name="$(test_name "${seq_len}" "${band_size}" "${num_seq}" "${col}")"
  local test_dir="${SCRIPT_DIR}/${name}"

  mkdir -p "${test_dir}"

  cat > "${test_dir}/Makefile" <<'EOF'
include ../template.mk
EOF

  cat > "${test_dir}/app_path.mk" <<'EOF'
include ../app_path.mk
EOF

  cat > "${test_dir}/parameters.mk" <<EOF
test-name = ${name}
seq-len = ${seq_len}
band-size = ${band_size}
num-seq = ${num_seq}
col = ${col}
EOF

  printf '%s\n' "${test_dir}"
}

run_with_module() {
  local command="$1"
  bash -lc "set -euo pipefail; ${MODULE_LOAD_CMD}; ${command}"
}

launch_profile_case() {
  local sweep="$1"
  local seq_len="$2"
  local band_size="$3"
  local num_seq="$4"
  local col="$5"
  local name
  name="$(test_name "${seq_len}" "${band_size}" "${num_seq}" "${col}")"
  local test_dir="${SCRIPT_DIR}/${name}"
  local log_dir="${RUN_DIR}/${sweep}"
  local log_file="${log_dir}/${name}.log"
  local status_file="${log_dir}/${name}.status"

  mkdir -p "${log_dir}"
  log "[${sweep}] started ${name}"

  (
    set +e
    bash -lc \
      "set -euo pipefail; ${MODULE_LOAD_CMD}; timeout --kill-after=5m ${TIMEOUT_LIMIT} make -C '${test_dir}' profile.log" \
      > "${log_file}" 2>&1
    local rc=$?
    local status

    if [[ "${rc}" -eq 0 ]]; then
      status="passed"
    elif [[ "${rc}" -eq 124 ]]; then
      status="timeout"
    else
      status="failed(${rc})"
    fi

    printf '%s\n' "${status}" > "${status_file}"
    exit "${rc}"
  ) &

  LAUNCHED_PID="$!"
  LAUNCHED_SWEEP="${sweep}"
  LAUNCHED_NAME="${name}"
  LAUNCHED_LOG_FILE="${log_file}"
  LAUNCHED_STATUS_FILE="${status_file}"
}

remove_active_job() {
  local remove_idx="$1"
  local new_pids=()
  local new_sweeps=()
  local new_names=()
  local new_logs=()
  local new_status_files=()
  local i

  for i in "${!ACTIVE_PIDS[@]}"; do
    if [[ "${i}" -ne "${remove_idx}" ]]; then
      new_pids+=("${ACTIVE_PIDS[$i]}")
      new_sweeps+=("${ACTIVE_SWEEPS[$i]}")
      new_names+=("${ACTIVE_NAMES[$i]}")
      new_logs+=("${ACTIVE_LOGS[$i]}")
      new_status_files+=("${ACTIVE_STATUS_FILES[$i]}")
    fi
  done

  ACTIVE_PIDS=("${new_pids[@]}")
  ACTIVE_SWEEPS=("${new_sweeps[@]}")
  ACTIVE_NAMES=("${new_names[@]}")
  ACTIVE_LOGS=("${new_logs[@]}")
  ACTIVE_STATUS_FILES=("${new_status_files[@]}")
}

reap_one_active_job() {
  while true; do
    local i
    for i in "${!ACTIVE_PIDS[@]}"; do
      if [[ -f "${ACTIVE_STATUS_FILES[$i]}" ]]; then
        local pid="${ACTIVE_PIDS[$i]}"
        local sweep="${ACTIVE_SWEEPS[$i]}"
        local name="${ACTIVE_NAMES[$i]}"
        local log_file="${ACTIVE_LOGS[$i]}"
        local status
        local rc=0

        wait "${pid}" || rc=$?
        status="$(<"${ACTIVE_STATUS_FILES[$i]}")"

        printf '%s\t%s\t%s\t%s\n' "${sweep}" "${name}" "${status}" "${log_file}" >> "${RUN_DIR}/summary.tsv"
        log "[${sweep}] finished ${name} with ${status}"

        if [[ "${rc}" -ne 0 ]]; then
          OVERALL_RC=1
        fi

        remove_active_job "${i}"
        return 0
      fi
    done

    sleep 1
  done
}

queue_profile_case() {
  local sweep="$1"
  local seq_len="$2"
  local band_size="$3"
  local num_seq="$4"
  local col="$5"

  while [[ "${#ACTIVE_PIDS[@]}" -ge "${MAX_PARALLEL}" ]]; do
    reap_one_active_job
  done

  launch_profile_case "${sweep}" "${seq_len}" "${band_size}" "${num_seq}" "${col}"
  ACTIVE_PIDS+=("${LAUNCHED_PID}")
  ACTIVE_SWEEPS+=("${LAUNCHED_SWEEP}")
  ACTIVE_NAMES+=("${LAUNCHED_NAME}")
  ACTIVE_LOGS+=("${LAUNCHED_LOG_FILE}")
  ACTIVE_STATUS_FILES+=("${LAUNCHED_STATUS_FILE}")
}

prepare_app() {
  log "clean"
  run_with_module "make -C '${SCRIPT_DIR}' clean"
  log "generate"
  run_with_module "make -C '${SCRIPT_DIR}' generate"
}

prepare_cases() {
  local band_size
  local col

  for band_size in "${BAND_SWEEP[@]}"; do
    make_test_dir "${FIXED_SEQ_LEN}" "${band_size}" "${FIXED_NUM_SEQ}" "${FIXED_COL}" > /dev/null
  done

  for col in "${COL_SWEEP[@]}"; do
    make_test_dir "${FIXED_SEQ_LEN}" "${FIXED_BAND}" "${FIXED_NUM_SEQ}" "${col}" > /dev/null
  done
}

launch_detached() {
  mkdir -p "${RUN_ROOT}"
  local run_id
  run_id="$(date '+%Y%m%d_%H%M%S')"
  local run_dir="${RUN_ROOT}/${run_id}"
  local orchestrator_log="${run_dir}/orchestrator.log"

  mkdir -p "${run_dir}"
  ln -sfn "${run_dir}" "${RUN_ROOT}/latest"

  nohup env RUN_DIR="${run_dir}" bash "${SCRIPT_PATH}" --worker \
    > "${orchestrator_log}" 2>&1 < /dev/null &

  local pid=$!
  printf 'started detached run\n'
  printf 'pid: %s\n' "${pid}"
  printf 'run dir: %s\n' "${run_dir}"
  printf 'log: %s\n' "${orchestrator_log}"
}

worker_main() {
  : "${RUN_DIR:?RUN_DIR must be set for worker mode}"
  mkdir -p "${RUN_DIR}"
  : > "${RUN_DIR}/summary.tsv"
  printf 'sweep\ttest\tstatus\tlog\n' >> "${RUN_DIR}/summary.tsv"

  OVERALL_RC=0
  ACTIVE_PIDS=()
  ACTIVE_SWEEPS=()
  ACTIVE_NAMES=()
  ACTIVE_LOGS=()
  ACTIVE_STATUS_FILES=()
  log "run dir: ${RUN_DIR}"
  prepare_app
  prepare_cases

  for band_size in "${BAND_SWEEP[@]}"; do
    queue_profile_case "band-size" "${FIXED_SEQ_LEN}" "${band_size}" "${FIXED_NUM_SEQ}" "${FIXED_COL}"
  done

  for col in "${COL_SWEEP[@]}"; do
    queue_profile_case "col" "${FIXED_SEQ_LEN}" "${FIXED_BAND}" "${FIXED_NUM_SEQ}" "${col}"
  done

  while [[ "${#ACTIVE_PIDS[@]}" -gt 0 ]]; do
    reap_one_active_job
  done

  log "all profile runs complete"
  exit "${OVERALL_RC}"
}

main() {
  if [[ "${1:-}" == "--worker" ]]; then
    worker_main
  else
    launch_detached
  fi
}

main "${@}"
