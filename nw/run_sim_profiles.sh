#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename -- "${BASH_SOURCE[0]}")"
RUN_ROOT="${SCRIPT_DIR}/sim-profile-runs"
APPS=(naive baseline efficient)
SEQ_LENS=(16 32 64 128 256)
NUM_SEQ=64
TIMEOUT_LIMIT=90m
MODULE_LOAD_CMD="${MODULE_LOAD_CMD:-module load hammerblade}"

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

test_name() {
  local seq_len="$1"
  printf 'seq-len_%s__num-seq_%s' "${seq_len}" "${NUM_SEQ}"
}

make_test_dir() {
  local app_dir="$1"
  local seq_len="$2"
  local name
  name="$(test_name "${seq_len}")"
  local test_dir="${app_dir}/${name}"

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
}

run_with_module() {
  local command="$1"
  bash -lc "set -euo pipefail; ${MODULE_LOAD_CMD}; ${command}"
}

prepare_apps() {
  for app in "${APPS[@]}"; do
    log "[${app}] clean"
    run_with_module "make -C '${SCRIPT_DIR}/${app}' clean"
  done

  for app in "${APPS[@]}"; do
    log "[${app}] generate"
    run_with_module "make -C '${SCRIPT_DIR}/${app}' generate"
    for seq_len in "${SEQ_LENS[@]}"; do
      make_test_dir "${SCRIPT_DIR}/${app}" "${seq_len}"
    done
  done
}

launch_profile_job() {
  local app="$1"
  local seq_len="$2"
  local name
  name="$(test_name "${seq_len}")"
  local test_dir="${SCRIPT_DIR}/${app}/${name}"
  local app_run_dir="${RUN_DIR}/${app}"
  local log_file="${app_run_dir}/${name}.log"
  local status_file="${app_run_dir}/${name}.status"

  mkdir -p "${app_run_dir}"

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

  echo $!
}

run_app_batch() {
  local app="$1"
  local pids=()
  local names=()

  log "[${app}] launching ${#SEQ_LENS[@]} profile runs"
  for seq_len in "${SEQ_LENS[@]}"; do
    local name
    name="$(test_name "${seq_len}")"
    local pid
    pid="$(launch_profile_job "${app}" "${seq_len}")"
    pids+=("${pid}")
    names+=("${name}")
    log "[${app}] started ${name} (pid=${pid})"
  done

  for i in "${!pids[@]}"; do
    local pid="${pids[$i]}"
    local name="${names[$i]}"
    local rc=0
    wait "${pid}" || rc=$?
    if [[ "${rc}" -ne 0 ]]; then
      OVERALL_RC=1
    fi

    local status_file="${RUN_DIR}/${app}/${name}.status"
    local status="failed(${rc})"
    if [[ -f "${status_file}" ]]; then
      status="$(<"${status_file}")"
    fi

    printf '%s\t%s\t%s\t%s\n' \
      "${app}" \
      "${name}" \
      "${status}" \
      "${RUN_DIR}/${app}/${name}.log" \
      >> "${RUN_DIR}/summary.tsv"

    log "[${app}] finished ${name} with ${status}"
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
  printf 'app\ttest\tstatus\tlog\n' >> "${RUN_DIR}/summary.tsv"

  OVERALL_RC=0

  log "run dir: ${RUN_DIR}"
  prepare_apps

  for app in "${APPS[@]}"; do
    run_app_batch "${app}"
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
