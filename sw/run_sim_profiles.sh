#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename -- "${BASH_SOURCE[0]}")"
RUN_ROOT="${SCRIPT_DIR}/sim-profile-runs"
TIMEOUT_LIMIT=60m
MODULE_LOAD_CMD="${MODULE_LOAD_CMD:-module load hammerblade}"

FIXED_SEQ_LENS=(16 32 64 128 256)
FIXED_SCHED_THRESHOLDS=(8 16 32 64 64)

VAR_MAX_SEQ_LEN=512
VAR_MIN_SEQ_LEN=64
VAR_LEN_SEED=1
VAR_LEN_QUANTUM=8
VAR_THRESHOLDS=(8 16 32 64)

NUM_SEQ=64

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

run_with_module() {
  local command="$1"
  bash -lc "set -euo pipefail; ${MODULE_LOAD_CMD}; ${command}"
}

fixed_test_name() {
  local app="$1"
  local seq_len="$2"
  local threshold="$3"

  if [[ "${app}" == "scheduling" ]]; then
    printf 'seq-len_%s__num-seq_%s__threshold_%s' "${seq_len}" "${NUM_SEQ}" "${threshold}"
  else
    printf 'seq-len_%s__num-seq_%s' "${seq_len}" "${NUM_SEQ}"
  fi
}

variable_test_name() {
  local app="$1"
  local threshold="${2:-}"
  local base

  base="seq-len_${VAR_MAX_SEQ_LEN}__num-seq_${NUM_SEQ}__len-min_${VAR_MIN_SEQ_LEN}__len-seed_${VAR_LEN_SEED}__len-quantum_${VAR_LEN_QUANTUM}"
  if [[ "${app}" == "scheduling" ]]; then
    printf '%s__threshold_%s' "${base}" "${threshold}"
  else
    printf '%s' "${base}"
  fi
}

make_test_dir() {
  local app="$1"
  local name="$2"
  local seq_len="$3"
  local threshold="${4:-}"
  local len_min="${5:-}"
  local len_seed="${6:-}"
  local len_quantum="${7:-}"
  local test_dir="${SCRIPT_DIR}/${app}/${name}"

  mkdir -p "${test_dir}"

  cat > "${test_dir}/Makefile" <<'EOF'
include ../template.mk
EOF

  cat > "${test_dir}/app_path.mk" <<'EOF'
include ../app_path.mk
EOF

  cat > "${test_dir}/parameters.mk" <<EOF
test-name = ${name}
num-seq = ${NUM_SEQ}
seq-len = ${seq_len}
EOF

  if [[ -n "${threshold}" ]]; then
    printf 'threshold = %s\n' "${threshold}" >> "${test_dir}/parameters.mk"
  fi
  if [[ -n "${len_min}" ]]; then
    printf 'len-min = %s\n' "${len_min}" >> "${test_dir}/parameters.mk"
  fi
  if [[ -n "${len_seed}" ]]; then
    printf 'len-seed = %s\n' "${len_seed}" >> "${test_dir}/parameters.mk"
  fi
  if [[ -n "${len_quantum}" ]]; then
    printf 'len-quantum = %s\n' "${len_quantum}" >> "${test_dir}/parameters.mk"
  fi
}

prepare_app() {
  local app="$1"
  log "[${app}] clean"
  run_with_module "make -C '${SCRIPT_DIR}/${app}' clean"
  log "[${app}] generate"
  run_with_module "make -C '${SCRIPT_DIR}/${app}' generate"

  local i
  for i in "${!FIXED_SEQ_LENS[@]}"; do
    local seq_len="${FIXED_SEQ_LENS[$i]}"
    local threshold="${FIXED_SCHED_THRESHOLDS[$i]}"
    local name
    name="$(fixed_test_name "${app}" "${seq_len}" "${threshold}")"
    if [[ "${app}" == "scheduling" ]]; then
      make_test_dir "${app}" "${name}" "${seq_len}" "${threshold}"
    else
      make_test_dir "${app}" "${name}" "${seq_len}"
    fi
  done

  if [[ "${app}" == "scheduling" ]]; then
    local threshold
    for threshold in "${VAR_THRESHOLDS[@]}"; do
      make_test_dir \
        "${app}" \
        "$(variable_test_name "${app}" "${threshold}")" \
        "${VAR_MAX_SEQ_LEN}" \
        "${threshold}" \
        "${VAR_MIN_SEQ_LEN}" \
        "${VAR_LEN_SEED}" \
        "${VAR_LEN_QUANTUM}"
    done
  else
    make_test_dir \
      "${app}" \
      "$(variable_test_name "${app}")" \
      "${VAR_MAX_SEQ_LEN}" \
      "" \
      "${VAR_MIN_SEQ_LEN}" \
      "${VAR_LEN_SEED}" \
      "${VAR_LEN_QUANTUM}"
  fi
}

prepare_apps() {
  prepare_app "1d"
  prepare_app "dynamic"
  prepare_app "scheduling"
}

launch_profile_job() {
  local batch_id="$1"
  local app="$2"
  local test_name="$3"
  local test_dir="${SCRIPT_DIR}/${app}/${test_name}"
  local batch_dir="${RUN_DIR}/${batch_id}"
  local log_file="${batch_dir}/${app}.log"
  local status_file="${batch_dir}/${app}.status"

  mkdir -p "${batch_dir}"
  log "[${batch_id}] [${app}] started ${test_name}"

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
  LAUNCHED_LOG_FILE="${log_file}"
  LAUNCHED_STATUS_FILE="${status_file}"
}

wait_for_job() {
  local batch_id="$1"
  local app="$2"
  local test_name="$3"
  local pid="$4"
  local log_file="$5"
  local status_file="$6"
  local rc=0
  local status="failed"

  wait "${pid}" || rc=$?
  if [[ -f "${status_file}" ]]; then
    status="$(<"${status_file}")"
  fi

  if [[ "${rc}" -ne 0 ]]; then
    OVERALL_RC=1
  fi

  printf '%s\t%s\t%s\t%s\t%s\n' \
    "${batch_id}" \
    "${app}" \
    "${test_name}" \
    "${status}" \
    "${log_file}" \
    >> "${RUN_DIR}/summary.tsv"

  log "[${batch_id}] [${app}] finished ${test_name} with ${status}"
}

run_case_batch() {
  local batch_id="$1"
  local one_d_test="$2"
  local dynamic_test="$3"
  local scheduling_test="$4"

  launch_profile_job "${batch_id}" "1d" "${one_d_test}"
  local pid_1d="${LAUNCHED_PID}"
  local log_1d="${LAUNCHED_LOG_FILE}"
  local status_1d="${LAUNCHED_STATUS_FILE}"

  launch_profile_job "${batch_id}" "dynamic" "${dynamic_test}"
  local pid_dynamic="${LAUNCHED_PID}"
  local log_dynamic="${LAUNCHED_LOG_FILE}"
  local status_dynamic="${LAUNCHED_STATUS_FILE}"

  launch_profile_job "${batch_id}" "scheduling" "${scheduling_test}"
  local pid_sched="${LAUNCHED_PID}"
  local log_sched="${LAUNCHED_LOG_FILE}"
  local status_sched="${LAUNCHED_STATUS_FILE}"

  wait_for_job "${batch_id}" "1d" "${one_d_test}" "${pid_1d}" "${log_1d}" "${status_1d}"
  wait_for_job "${batch_id}" "dynamic" "${dynamic_test}" "${pid_dynamic}" "${log_dynamic}" "${status_dynamic}"
  wait_for_job "${batch_id}" "scheduling" "${scheduling_test}" "${pid_sched}" "${log_sched}" "${status_sched}"
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
  printf 'batch\tapp\ttest\tstatus\tlog\n' >> "${RUN_DIR}/summary.tsv"

  OVERALL_RC=0

  log "run dir: ${RUN_DIR}"
  prepare_apps

  local i
  for i in "${!FIXED_SEQ_LENS[@]}"; do
    local seq_len="${FIXED_SEQ_LENS[$i]}"
    local threshold="${FIXED_SCHED_THRESHOLDS[$i]}"
    run_case_batch \
      "fixed-seq-len-${seq_len}" \
      "$(fixed_test_name "1d" "${seq_len}" "${threshold}")" \
      "$(fixed_test_name "dynamic" "${seq_len}" "${threshold}")" \
      "$(fixed_test_name "scheduling" "${seq_len}" "${threshold}")"
  done

  local threshold
  for threshold in "${VAR_THRESHOLDS[@]}"; do
    run_case_batch \
      "variable-threshold-${threshold}" \
      "$(variable_test_name "1d")" \
      "$(variable_test_name "dynamic")" \
      "$(variable_test_name "scheduling" "${threshold}")"
  done

  log "all batches complete"
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
