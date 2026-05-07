#!/usr/bin/env bash
# Purpose:
#   Compare M3 reorg on/off with M1 CFSM and M2 budget control kept enabled.
#   The off variant is the required fallback build: FACO_ENABLE_REORG=0.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 bash experiments/M3/run_reorg_compare.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M3_SCRIPT_DIR="${SCRIPT_DIR}"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${M3_SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-reorg-compare-v1}"
source "${REPO_ROOT}/experiments/M1/common.sh"
SCRIPT_DIR="${M3_SCRIPT_DIR}"

COMPARE_RUNS="${COMPARE_RUNS:-1}"
BUILD_ON_DIR="${BUILD_ON_DIR:-${REPO_ROOT}/cmake-build-release-reorg-on}"
BUILD_OFF_DIR="${BUILD_OFF_DIR:-${REPO_ROOT}/cmake-build-release-reorg-off}"
M3_BENCHMARKS="${M3_BENCHMARKS:-fillrandom,overwrite,overwrite,stats}"
SUMMARY_CSV="${RESULT_DIR}/summary.csv"
SUMMARY_MD="${RESULT_DIR}/summary.md"

mkdir -p "${RESULT_DIR}"

build_variant() {
  local name="$1"
  local reorg_enabled="$2"
  local build_dir="$3"

  if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    echo "SKIP_BUILD=1, reusing ${name} build at ${build_dir}."
    return
  fi

  cmake -S "${REPO_ROOT}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DROCKSDB_PLUGINS=zenfs \
    -DWITH_ALL_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=${reorg_enabled} ${EXTRA_CMAKE_CXX_FLAGS:-}"

  cmake --build "${build_dir}" --target db_bench zenfs_tool -j"$(nproc)"
}

append_metrics() {
  local variant="$1"
  local reorg_enabled="$2"
  local run_id="$3"
  local run_dir="$4"
  local log_path="${run_dir}/db_bench.log"

  if [[ ! -f "${log_path}" ]]; then
    echo "${variant},${reorg_enabled},${run_id},ERROR,ERROR,ERROR,ERROR,ERROR,${run_dir}" >> "${SUMMARY_CSV}"
    return
  fi

  awk -v variant="${variant}" \
      -v reorg_enabled="${reorg_enabled}" \
      -v run_id="${run_id}" \
      -v run_dir="${run_dir}" '
    $2 == ":" && $3 ~ /^[0-9.]+$/ && $5 ~ /^[0-9.]+$/ {
      gsub(/;/, "", $10);
      printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             variant, reorg_enabled, run_id, $1, $3, $5, $7, $11, run_dir;
      found = 1;
    }
    END {
      if (!found) {
        printf "%s,%s,%s,ERROR,ERROR,ERROR,ERROR,ERROR,%s\n",
               variant, reorg_enabled, run_id, run_dir;
      }
    }
  ' "${log_path}" >> "${SUMMARY_CSV}"
}

copy_faco_exports() {
  local run_dir="$1"
  {
    for export_file in \
      faco_budget_summary.txt \
      faco_budget_trace.csv \
      faco_reorg_summary.txt \
      faco_reorg_trace.csv \
      faco_lacr_trace.csv \
      faco_runtime_metrics.txt; do
      local src="${ZENFS_AUX_PATH}/${export_file}"
      local dst="${run_dir}/${export_file}"
      if run_sudo test -f "${src}"; then
        run_sudo cat "${src}" > "${dst}"
        echo "Copied ${src} to ${dst}"
      else
        echo "FACO export not found: ${src}"
      fi
    done
  } | tee "${run_dir}/faco_reorg_export.log"

  if [[ -f "${run_dir}/faco_reorg_trace.csv" ]]; then
    python3 "${SCRIPT_DIR}/analyze_reorg_trace.py" \
      "${run_dir}/faco_reorg_trace.csv" "${run_dir}"
  fi
}

run_variant() {
  local variant="$1"
  local reorg_enabled="$2"
  local build_dir="$3"

  require_file "${build_dir}/db_bench"
  require_file "${build_dir}/zenfs"

  for run_id in $(seq 1 "${COMPARE_RUNS}"); do
    local run_dir="${RESULT_DIR}/${variant}/run_${run_id}"
    mkdir -p "${run_dir}"
    echo "Running ${variant} run ${run_id}; logs: ${run_dir}"

    BUILD_RELEASE_DIR="${build_dir}" \
    DB_BENCH="${build_dir}/db_bench" \
    ZENFS_TOOL="${build_dir}/zenfs" \
    RESULT_DIR="${run_dir}" \
    CONFIRM_MKFS=1 \
    ZBD="${ZBD}" \
    ZENFS_AUX_PATH="${ZENFS_AUX_PATH}" \
    DB_PATH="${DB_PATH}" \
    BENCHMARKS="${M3_BENCHMARKS}" \
    NUM="${NUM:-5000000}" \
    VALUE_SIZE="${VALUE_SIZE:-1024}" \
    COMPRESSION_TYPE="${COMPRESSION_TYPE}" \
    ZENFS_ENABLE_GC=1 \
    EXTRA_DB_BENCH_ARGS="${EXTRA_DB_BENCH_ARGS:-}" \
    FACO_BUDGET_B_MIN="${FACO_BUDGET_B_MIN:-6}" \
    FACO_BUDGET_B_MAX="${FACO_BUDGET_B_MAX:-12}" \
    FACO_BUDGET_KP="${FACO_BUDGET_KP:-0.6}" \
    FACO_BUDGET_KI="${FACO_BUDGET_KI:-0.05}" \
    FACO_BUDGET_P_TARGET="${FACO_BUDGET_P_TARGET:-0}" \
    FACO_BUDGET_TOP_K="${FACO_BUDGET_TOP_K:-8}" \
    FACO_BUDGET_RBD_THRESHOLD="${FACO_BUDGET_RBD_THRESHOLD:-0.05}" \
    FACO_BUDGET_ZVDR_WEIGHT="${FACO_BUDGET_ZVDR_WEIGHT:-0}" \
    FACO_REORG_TOP_K="${FACO_REORG_TOP_K:-8}" \
    FACO_REORG_TAU_TRIGGER_INIT="${FACO_REORG_TAU_TRIGGER_INIT:-5242880}" \
    FACO_REORG_CONTENTION_PENALTY_BYTES="${FACO_REORG_CONTENTION_PENALTY_BYTES:-4194304}" \
    FACO_REORG_TAU_MODE="${FACO_REORG_TAU_MODE:-adaptive}" \
    FACO_REORG_MIN_MIGRATE_BYTES="${FACO_REORG_MIN_MIGRATE_BYTES:-}" \
    FACO_REORG_MIN_MIGRATE_RATIO="${FACO_REORG_MIN_MIGRATE_RATIO:-0.03125}" \
    FACO_REORG_VICTIM_COOLDOWN_US="${FACO_REORG_VICTIM_COOLDOWN_US:-120000000}" \
    FACO_REORG_ADAPTIVE_HISTORY_SIZE="${FACO_REORG_ADAPTIVE_HISTORY_SIZE:-32}" \
    FACO_REORG_ADAPTIVE_Q_BASE="${FACO_REORG_ADAPTIVE_Q_BASE:-0.85}" \
    FACO_REORG_ADAPTIVE_Q_MIN="${FACO_REORG_ADAPTIVE_Q_MIN:-0.70}" \
    FACO_REORG_ADAPTIVE_Q_MAX="${FACO_REORG_ADAPTIVE_Q_MAX:-0.90}" \
    FACO_REORG_ADAPTIVE_Q_BUDGET_GAIN="${FACO_REORG_ADAPTIVE_Q_BUDGET_GAIN:-0.15}" \
    FACO_REORG_ACCEPT_HYSTERESIS="${FACO_REORG_ACCEPT_HYSTERESIS:-0.01}" \
    FACO_REORG_ADAPTIVE_WARMUP_EVALS="${FACO_REORG_ADAPTIVE_WARMUP_EVALS:-5}" \
    FACO_REORG_MIN_EXEC_INTERVAL_US="${FACO_REORG_MIN_EXEC_INTERVAL_US:-60000000}" \
    FACO_REORG_FORCE_EVAL="${FACO_REORG_FORCE_EVAL:-1}" \
    FACO_REORG_FREE_SPACE_TRIGGER_PERCENT="${FACO_REORG_FREE_SPACE_TRIGGER_PERCENT:-100}" \
    FACO_LACR_ENABLE="${FACO_LACR_ENABLE:-0}" \
    FACO_LACR_W_SYNERGY="${FACO_LACR_W_SYNERGY:-0}" \
    FACO_LACR_W_WASTE="${FACO_LACR_W_WASTE:-1}" \
    FACO_LACR_W_LATENCY="${FACO_LACR_W_LATENCY:-0.25}" \
    FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES="${FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES:-8388608}" \
    FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES="${FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES:-4194304}" \
      bash "${REPO_ROOT}/experiments/M1/run_fillrandom_sanity.sh"

    copy_faco_exports "${run_dir}"
    append_metrics "${variant}" "${reorg_enabled}" "${run_id}" "${run_dir}"
  done
}

write_markdown_summary() {
  {
    echo "# M3 reorg compare summary"
    echo
    echo "- ZBD: ${ZBD}"
    echo "- DB_PATH: ${DB_PATH}"
    echo "- NUM: ${NUM:-5000000}"
    echo "- BENCHMARKS: ${M3_BENCHMARKS}"
    echo "- M2 defaults: B_min=${FACO_BUDGET_B_MIN:-6}, P_target=${FACO_BUDGET_P_TARGET:-0}"
    echo
    echo "| variant | reorg | run | benchmark | micros/op | ops/sec | seconds | MB/s | log dir |"
    echo "|---|---:|---:|---|---:|---:|---:|---:|---|"
    awk -F, 'NR > 1 {
      printf "| %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n",
             $1, $2, $3, $4, $5, $6, $7, $8, $9;
    }' "${SUMMARY_CSV}"
  } > "${SUMMARY_MD}"
}

print_config | tee "${RESULT_DIR}/compare_config.txt"
cat <<EOF | tee -a "${RESULT_DIR}/compare_config.txt"
  COMPARE_RUNS=${COMPARE_RUNS}
  M3_BENCHMARKS=${M3_BENCHMARKS}
  BUILD_ON_DIR=${BUILD_ON_DIR}
  BUILD_OFF_DIR=${BUILD_OFF_DIR}
  FACO_REORG_TAU_MODE=${FACO_REORG_TAU_MODE:-adaptive}
  FACO_REORG_FORCE_EVAL=${FACO_REORG_FORCE_EVAL:-1}
  FACO_REORG_FREE_SPACE_TRIGGER_PERCENT=${FACO_REORG_FREE_SPACE_TRIGGER_PERCENT:-100}
  FACO_REORG_MIN_MIGRATE_BYTES=${FACO_REORG_MIN_MIGRATE_BYTES:-}
  FACO_REORG_MIN_MIGRATE_RATIO=${FACO_REORG_MIN_MIGRATE_RATIO:-0.03125}
  FACO_REORG_VICTIM_COOLDOWN_US=${FACO_REORG_VICTIM_COOLDOWN_US:-120000000}
  FACO_REORG_ADAPTIVE_HISTORY_SIZE=${FACO_REORG_ADAPTIVE_HISTORY_SIZE:-32}
  FACO_REORG_ADAPTIVE_Q_BASE=${FACO_REORG_ADAPTIVE_Q_BASE:-0.85}
  FACO_REORG_ADAPTIVE_Q_MIN=${FACO_REORG_ADAPTIVE_Q_MIN:-0.70}
  FACO_REORG_ADAPTIVE_Q_MAX=${FACO_REORG_ADAPTIVE_Q_MAX:-0.90}
  FACO_REORG_ADAPTIVE_Q_BUDGET_GAIN=${FACO_REORG_ADAPTIVE_Q_BUDGET_GAIN:-0.15}
  FACO_REORG_ACCEPT_HYSTERESIS=${FACO_REORG_ACCEPT_HYSTERESIS:-0.01}
  FACO_REORG_ADAPTIVE_WARMUP_EVALS=${FACO_REORG_ADAPTIVE_WARMUP_EVALS:-5}
  FACO_REORG_MIN_EXEC_INTERVAL_US=${FACO_REORG_MIN_EXEC_INTERVAL_US:-60000000}
  FACO_LACR_ENABLE=${FACO_LACR_ENABLE:-0}
EOF

echo "variant,reorg_enabled,run,benchmark,micros_per_op,ops_per_sec,seconds,mb_per_sec,result_dir" > "${SUMMARY_CSV}"

build_variant "reorg_on" "1" "${BUILD_ON_DIR}"
build_variant "reorg_off" "0" "${BUILD_OFF_DIR}"
run_variant "reorg_on" "1" "${BUILD_ON_DIR}"
run_variant "reorg_off" "0" "${BUILD_OFF_DIR}"
write_markdown_summary

python3 "${SCRIPT_DIR}/summarize_m3_results.py" \
  "${RESULT_DIR}" "${RESULT_DIR}/m3_summary.md"

echo "M3 reorg compare completed."
echo "CSV: ${SUMMARY_CSV}"
echo "Markdown: ${SUMMARY_MD}"
