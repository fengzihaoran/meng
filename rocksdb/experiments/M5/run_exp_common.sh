#!/usr/bin/env bash
# Shared M5 benchmark helpers. This file is sourced by run_exp*.sh.
set -euo pipefail

M5_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M5_REPO_ROOT="$(cd -- "${M5_SCRIPT_DIR}/../.." && pwd)"

source "${M5_REPO_ROOT}/experiments/M1/common.sh"

M5_RESULT_ROOT="${M5_RESULT_ROOT:-${M5_SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-m5}"
M5_CONFIG="${M5_CONFIG:-${M5_SCRIPT_DIR}/faco_default.json}"
M5_PROFILE="${M5_PROFILE:-smoke}"

case "${M5_PROFILE}" in
  smoke)
    M5_RUNS="${M5_RUNS:-1}"
    NUM="${NUM:-100000}"
    VALUE_SIZE="${VALUE_SIZE:-1024}"
    M5_EXP1_BENCHMARKS_DEFAULT="${M5_EXP1_BENCHMARKS_DEFAULT:-fillseq,fillrandom,overwrite}"
    M5_EXP2_BENCHMARKS_DEFAULT="${M5_EXP2_BENCHMARKS_DEFAULT:-fillrandom,overwrite}"
    M5_EXP3_BENCHMARKS_DEFAULT="${M5_EXP3_BENCHMARKS_DEFAULT:-fillrandom,overwrite}"
    M5_EXP4_BENCHMARKS_DEFAULT="${M5_EXP4_BENCHMARKS_DEFAULT:-fillrandom,overwrite}"
    ;;
  dev)
    M5_RUNS="${M5_RUNS:-1}"
    NUM="${NUM:-1000000}"
    VALUE_SIZE="${VALUE_SIZE:-1024}"
    M5_EXP1_BENCHMARKS_DEFAULT="${M5_EXP1_BENCHMARKS_DEFAULT:-fillseq,fillrandom,overwrite}"
    M5_EXP2_BENCHMARKS_DEFAULT="${M5_EXP2_BENCHMARKS_DEFAULT:-fillrandom,overwrite,overwrite}"
    M5_EXP3_BENCHMARKS_DEFAULT="${M5_EXP3_BENCHMARKS_DEFAULT:-fillrandom,overwrite,overwrite}"
    M5_EXP4_BENCHMARKS_DEFAULT="${M5_EXP4_BENCHMARKS_DEFAULT:-fillrandom,overwrite}"
    ;;
  paper)
    M5_RUNS="${M5_RUNS:-5}"
    NUM="${NUM:-5000000}"
    VALUE_SIZE="${VALUE_SIZE:-1024}"
    M5_EXP1_BENCHMARKS_DEFAULT="${M5_EXP1_BENCHMARKS_DEFAULT:-fillseq,fillrandom,overwrite,readrandom,readwhilewriting}"
    M5_EXP2_BENCHMARKS_DEFAULT="${M5_EXP2_BENCHMARKS_DEFAULT:-fillrandom,overwrite,overwrite}"
    M5_EXP3_BENCHMARKS_DEFAULT="${M5_EXP3_BENCHMARKS_DEFAULT:-fillrandom,overwrite,overwrite}"
    M5_EXP4_BENCHMARKS_DEFAULT="${M5_EXP4_BENCHMARKS_DEFAULT:-fillrandom,overwrite,readwhilewriting}"
    ;;
  *)
    echo "Invalid M5_PROFILE=${M5_PROFILE}; expected smoke, dev, or paper." >&2
    exit 2
    ;;
esac

m5_require_device_confirmation() {
  if [[ "${CONFIRM_DEVICE_BENCH:-0}" != "1" ]]; then
    cat >&2 <<EOF
M5 device benchmark guard:
  This script may format a ZenFS target through the delegated M1/M3/M4 scripts.
  Set CONFIRM_DEVICE_BENCH=1 only after scripts and summaries are ready.
  Current request explicitly forbids running device benchmarks yet, so this is
  expected to stop here unless you are intentionally executing the protocol.
EOF
    exit 3
  fi
}

m5_print_protocol_header() {
  mkdir -p "${M5_RESULT_ROOT}"
  cat <<EOF | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
M5 protocol:
  REPO_ROOT=${M5_REPO_ROOT}
  RESULT_ROOT=${M5_RESULT_ROOT}
  FACO_CONFIG_PATH=${M5_CONFIG}
  M5_PROFILE=${M5_PROFILE}
  M5_RUNS=${M5_RUNS}
  NUM=${NUM}
  VALUE_SIZE=${VALUE_SIZE}
  ZBD=${ZBD}
  ZENFS_AUX_PATH=${ZENFS_AUX_PATH}
  DB_PATH=${DB_PATH}
EOF
}

m5_build_variant() {
  local variant="$1"
  local build_dir="$2"
  local cflags="$3"

  if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    echo "SKIP_BUILD=1: reusing ${build_dir}" | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
    return
  fi

  cmake -S "${M5_REPO_ROOT}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DROCKSDB_PLUGINS=zenfs \
    -DWITH_ALL_TESTS=ON \
    -DCMAKE_CXX_FLAGS="${cflags} ${EXTRA_CMAKE_CXX_FLAGS:-}"

  cmake --build "${build_dir}" --target db_bench zenfs_tool -j"$(nproc)"
  echo "Built ${variant}: ${build_dir}" | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
}

m5_copy_exports() {
  local run_dir="$1"
  mkdir -p "${run_dir}"
  {
    for export_file in \
      faco_cfsm_summary.txt \
      faco_cfsm_zones.csv \
      faco_budget_summary.txt \
      faco_budget_trace.csv \
      faco_reorg_summary.txt \
      faco_reorg_trace.csv \
      faco_lacr_trace.csv \
      faco_runtime_metrics.txt \
      faco_metrics.txt \
      faco_metrics.json \
      faco_metrics.prom \
      faco_config_effective.txt \
      faco_shutdown_stage.txt; do
      local src="${ZENFS_AUX_PATH}/${export_file}"
      local dst="${run_dir}/${export_file}"
      if run_sudo test -f "${src}"; then
        run_sudo cat "${src}" > "${dst}"
        echo "Copied ${src} to ${dst}"
      else
        echo "FACO export not found: ${src}"
      fi
    done
  } | tee "${run_dir}/faco_export.log"
}

m5_run_db_bench_workload() {
  local variant="$1"
  local build_dir="$2"
  local run_dir="$3"
  local benchmarks_csv="$4"

  require_file "${build_dir}/db_bench"
  require_file "${build_dir}/zenfs"
  mkdir -p "${run_dir}"

  set +e
  BUILD_RELEASE_DIR="${build_dir}" \
  DB_BENCH="${build_dir}/db_bench" \
  ZENFS_TOOL="${build_dir}/zenfs" \
  RESULT_DIR="${run_dir}" \
  RESULT_PURPOSE="m5-${variant}" \
  CONFIRM_MKFS=1 \
  ZBD="${ZBD}" \
  ZENFS_AUX_PATH="${ZENFS_AUX_PATH}" \
  DB_PATH="${DB_PATH}" \
  BENCHMARKS="${benchmarks_csv}" \
  NUM="${NUM}" \
  VALUE_SIZE="${VALUE_SIZE}" \
  COMPRESSION_TYPE="${COMPRESSION_TYPE}" \
  ZENFS_ENABLE_GC="${ZENFS_ENABLE_GC:-1}" \
  EXTRA_DB_BENCH_ARGS="${EXTRA_DB_BENCH_ARGS:-}" \
  FACO_CONFIG_PATH="${M5_CONFIG}" \
  FACO_LACR_ENABLE="${FACO_LACR_ENABLE:-0}" \
  FACO_FINAL_DUMP_ENABLE="${FACO_FINAL_DUMP_ENABLE:-1}" \
    bash "${M5_REPO_ROOT}/experiments/M1/run_fillrandom_sanity.sh"
  local db_bench_status=$?
  set -e

  m5_copy_exports "${run_dir}"
  if [[ "${db_bench_status}" -ne 0 ]]; then
    echo "M5 workload ${variant} failed with status ${db_bench_status}; copied available exports to ${run_dir}." >&2
    return "${db_bench_status}"
  fi
}

m5_summarize_result_root() {
  local root="$1"
  python3 "${M5_SCRIPT_DIR}/summarize_traces.py" \
    "${root}" "${root}/m5_summary.md"
}
