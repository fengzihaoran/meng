#!/usr/bin/env bash
# Purpose:
#   Compare M4 LACR runtime off/on with M1 CFSM, M2 budget control, and M3
#   reorg kept enabled. This is a device benchmark wrapper; do not run it as a
#   unit-test smoke command.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 bash experiments/M4/run_lacr_compare.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-lacr-compare-v1}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/cmake-build-release-lacr}"
COMPARE_RUNS="${COMPARE_RUNS:-1}"
M3_BENCHMARKS="${M3_BENCHMARKS:-fillrandom,overwrite,overwrite,stats}"
SUMMARY_CSV="${RESULT_DIR}/summary.csv"
SUMMARY_MD="${RESULT_DIR}/summary.md"

source "${REPO_ROOT}/experiments/M1/common.sh"

mkdir -p "${RESULT_DIR}"

print_config | tee "${RESULT_DIR}/compare_config.txt"
cat <<EOF | tee -a "${RESULT_DIR}/compare_config.txt"
  COMPARE_RUNS=${COMPARE_RUNS}
  M3_BENCHMARKS=${M3_BENCHMARKS}
  BUILD_DIR=${BUILD_DIR}
  FACO_ENABLE_LACR=1
  FACO_LACR_W_SYNERGY=${FACO_LACR_W_SYNERGY:-0}
  FACO_LACR_W_WASTE=${FACO_LACR_W_WASTE:-1}
  FACO_LACR_W_LATENCY=${FACO_LACR_W_LATENCY:-0.25}
  FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES=${FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES:-8388608}
  FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES=${FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES:-4194304}
EOF

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DROCKSDB_PLUGINS=zenfs \
    -DWITH_ALL_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=1 -DFACO_ENABLE_LACR=1 ${EXTRA_CMAKE_CXX_FLAGS:-}"

  cmake --build "${BUILD_DIR}" --target db_bench zenfs_tool -j"$(nproc)"
fi

require_file "${BUILD_DIR}/db_bench"
require_file "${BUILD_DIR}/zenfs"

echo "variant,lacr_enabled,run,benchmark,micros_per_op,ops_per_sec,seconds,mb_per_sec,result_dir" > "${SUMMARY_CSV}"

append_metrics() {
  local variant="$1"
  local lacr_enabled="$2"
  local run_id="$3"
  local run_dir="$4"
  local log_path="${run_dir}/db_bench.log"

  if [[ ! -f "${log_path}" ]]; then
    echo "${variant},${lacr_enabled},${run_id},ERROR,ERROR,ERROR,ERROR,ERROR,${run_dir}" >> "${SUMMARY_CSV}"
    return
  fi

  awk -v variant="${variant}" \
      -v lacr_enabled="${lacr_enabled}" \
      -v run_id="${run_id}" \
      -v run_dir="${run_dir}" '
    $2 == ":" && $3 ~ /^[0-9.]+$/ && $5 ~ /^[0-9.]+$/ {
      gsub(/;/, "", $10);
      printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             variant, lacr_enabled, run_id, $1, $3, $5, $7, $11, run_dir;
      found = 1;
    }
    END {
      if (!found) {
        printf "%s,%s,%s,ERROR,ERROR,ERROR,ERROR,ERROR,%s\n",
               variant, lacr_enabled, run_id, run_dir;
      }
    }
  ' "${log_path}" >> "${SUMMARY_CSV}"
}

run_variant() {
  local variant="$1"
  local lacr_enabled="$2"

  for run_id in $(seq 1 "${COMPARE_RUNS}"); do
    local run_dir="${RESULT_DIR}/${variant}/run_${run_id}"
    mkdir -p "${run_dir}"
    echo "Running ${variant} run ${run_id}; logs: ${run_dir}"

    BUILD_RELEASE_DIR="${BUILD_DIR}" \
    DB_BENCH="${BUILD_DIR}/db_bench" \
    ZENFS_TOOL="${BUILD_DIR}/zenfs" \
    RESULT_DIR="${run_dir}" \
    CONFIRM_MKFS=1 \
    ZBD="${ZBD}" \
    ZENFS_AUX_PATH="${ZENFS_AUX_PATH}" \
    DB_PATH="${DB_PATH}" \
    M3_BENCHMARKS="${M3_BENCHMARKS}" \
    NUM="${NUM:-5000000}" \
    VALUE_SIZE="${VALUE_SIZE:-1024}" \
    COMPRESSION_TYPE="${COMPRESSION_TYPE}" \
    FACO_LACR_ENABLE="${lacr_enabled}" \
    FACO_LACR_W_SYNERGY="${FACO_LACR_W_SYNERGY:-0}" \
    FACO_LACR_W_WASTE="${FACO_LACR_W_WASTE:-1}" \
    FACO_LACR_W_LATENCY="${FACO_LACR_W_LATENCY:-0.25}" \
    FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES="${FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES:-8388608}" \
    FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES="${FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES:-4194304}" \
      bash "${REPO_ROOT}/experiments/M3/run_reorg_workload.sh"

    append_metrics "${variant}" "${lacr_enabled}" "${run_id}" "${run_dir}"
  done
}

run_variant "lacr_off" "0"
run_variant "lacr_on" "1"

{
  echo "# M4 LACR compare summary"
  echo
  echo "- ZBD: ${ZBD}"
  echo "- DB_PATH: ${DB_PATH}"
  echo "- NUM: ${NUM:-5000000}"
  echo "- BENCHMARKS: ${M3_BENCHMARKS}"
  echo
  echo "| variant | LACR | run | benchmark | micros/op | ops/sec | seconds | MB/s | log dir |"
  echo "|---|---:|---:|---|---:|---:|---:|---:|---|"
  awk -F, 'NR > 1 {
    printf "| %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n",
           $1, $2, $3, $4, $5, $6, $7, $8, $9;
  }' "${SUMMARY_CSV}"
} > "${SUMMARY_MD}"

python3 "${REPO_ROOT}/experiments/M3/summarize_m3_results.py" \
  "${RESULT_DIR}" "${RESULT_DIR}/m4_summary.md"

echo "M4 LACR compare completed."
echo "CSV: ${SUMMARY_CSV}"
echo "Markdown: ${SUMMARY_MD}"
