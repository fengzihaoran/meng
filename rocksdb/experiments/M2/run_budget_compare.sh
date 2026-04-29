#!/usr/bin/env bash
# Purpose:
#   Compare FACO M2 budget control on/off with the same ZenFS workload.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=3 bash experiments/M2/run_budget_compare.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M2_SCRIPT_DIR="${SCRIPT_DIR}"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${M2_SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-budget-compare}"
source "${REPO_ROOT}/experiments/M1/common.sh"
SCRIPT_DIR="${M2_SCRIPT_DIR}"

COMPARE_RUNS="${COMPARE_RUNS:-1}"
BUILD_ON_DIR="${BUILD_ON_DIR:-${REPO_ROOT}/cmake-build-release-budget-on}"
BUILD_OFF_DIR="${BUILD_OFF_DIR:-${REPO_ROOT}/cmake-build-release-budget-off}"
M2_BENCHMARKS="${M2_BENCHMARKS:-fillrandom,overwrite,overwrite,stats}"
ZENFS_ENABLE_GC="${ZENFS_ENABLE_GC:-1}"
RUN_BUDGET_ON="${RUN_BUDGET_ON:-1}"
RUN_BUDGET_OFF="${RUN_BUDGET_OFF:-1}"
SUMMARY_CSV="${RESULT_DIR}/summary.csv"
SUMMARY_MD="${RESULT_DIR}/summary.md"

mkdir -p "${RESULT_DIR}"

if [[ "${RUN_BUDGET_ON}" != "1" && "${RUN_BUDGET_OFF}" != "1" ]]; then
  echo "At least one of RUN_BUDGET_ON or RUN_BUDGET_OFF must be 1." >&2
  exit 1
fi

build_variant() {
  local name="$1"
  local budget_enabled="$2"
  local build_dir="$3"

  if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    echo "SKIP_BUILD=1, reusing ${name} build at ${build_dir}."
    return
  fi

  echo "Configuring ${name} build: ${build_dir}"
  cmake -S "${REPO_ROOT}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DROCKSDB_PLUGINS=zenfs \
    -DWITH_ALL_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=${budget_enabled} ${EXTRA_CMAKE_CXX_FLAGS:-}"

  echo "Building ${name} db_bench and zenfs."
  cmake --build "${build_dir}" --target db_bench zenfs_tool -j"$(nproc)"
}

append_metrics() {
  local variant="$1"
  local budget_enabled="$2"
  local run_id="$3"
  local run_dir="$4"
  local log_path="${run_dir}/db_bench.log"

  if [[ ! -f "${log_path}" ]]; then
    echo "${variant},${budget_enabled},${run_id},ERROR,ERROR,ERROR,ERROR,ERROR,${run_dir}" >> "${SUMMARY_CSV}"
    return
  fi

  awk -v variant="${variant}" \
      -v budget_enabled="${budget_enabled}" \
      -v run_id="${run_id}" \
      -v run_dir="${run_dir}" '
    $2 == ":" && $3 ~ /^[0-9.]+$/ && $5 ~ /^[0-9.]+$/ {
      gsub(/;/, "", $10);
      printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             variant, budget_enabled, run_id, $1, $3, $5, $7, $11, run_dir;
      found = 1;
    }
    END {
      if (!found) {
        printf "%s,%s,%s,ERROR,ERROR,ERROR,ERROR,ERROR,%s\n",
               variant, budget_enabled, run_id, run_dir;
      }
    }
  ' "${log_path}" >> "${SUMMARY_CSV}"
}

copy_budget_exports() {
  local run_dir="$1"
  {
    for export_file in \
      faco_budget_summary.txt \
      faco_budget_trace.csv \
      faco_runtime_metrics.txt; do
      local src="${ZENFS_AUX_PATH}/${export_file}"
      local dst="${run_dir}/${export_file}"
      if run_sudo test -f "${src}"; then
        run_sudo cat "${src}" > "${dst}"
        echo "Copied ${src} to ${dst}"
      else
        echo "Budget export not found: ${src}"
      fi
    done
  } | tee "${run_dir}/faco_budget_export.log"

  if [[ -f "${run_dir}/faco_budget_trace.csv" ]]; then
    python3 "${SCRIPT_DIR}/analyze_budget_trace.py" \
      "${run_dir}/faco_budget_trace.csv" "${run_dir}"
  fi
}

run_variant() {
  local variant="$1"
  local budget_enabled="$2"
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
    BENCHMARKS="${M2_BENCHMARKS}" \
    NUM="${NUM:-5000000}" \
    VALUE_SIZE="${VALUE_SIZE:-1024}" \
    COMPRESSION_TYPE="${COMPRESSION_TYPE}" \
    ZENFS_ENABLE_GC="${ZENFS_ENABLE_GC}" \
    EXTRA_DB_BENCH_ARGS="${EXTRA_DB_BENCH_ARGS:-}" \
    FACO_BUDGET_B_MIN="${FACO_BUDGET_B_MIN:-2}" \
    FACO_BUDGET_B_MAX="${FACO_BUDGET_B_MAX:-12}" \
    FACO_BUDGET_KP="${FACO_BUDGET_KP:-0.6}" \
    FACO_BUDGET_KI="${FACO_BUDGET_KI:-0.05}" \
    FACO_BUDGET_P_TARGET="${FACO_BUDGET_P_TARGET:-0}" \
    FACO_BUDGET_TOP_K="${FACO_BUDGET_TOP_K:-8}" \
    FACO_BUDGET_RBD_THRESHOLD="${FACO_BUDGET_RBD_THRESHOLD:-0.05}" \
    FACO_BUDGET_ZVDR_WEIGHT="${FACO_BUDGET_ZVDR_WEIGHT:-0}" \
      bash "${REPO_ROOT}/experiments/M1/run_fillrandom_sanity.sh"

    copy_budget_exports "${run_dir}"
    append_metrics "${variant}" "${budget_enabled}" "${run_id}" "${run_dir}"
  done
}

write_markdown_summary() {
  {
    echo "# M2 budget compare summary"
    echo
    echo "- ZBD: ${ZBD}"
    echo "- DB_PATH: ${DB_PATH}"
    echo "- ZENFS_AUX_PATH: ${ZENFS_AUX_PATH}"
    echo "- NUM: ${NUM:-5000000}"
    echo "- VALUE_SIZE: ${VALUE_SIZE:-1024}"
    echo "- BENCHMARKS: ${M2_BENCHMARKS}"
    echo "- ZENFS_ENABLE_GC: ${ZENFS_ENABLE_GC}"
    echo "- COMPARE_RUNS: ${COMPARE_RUNS}"
    echo
    echo "## Per-run results"
    echo
    echo "| variant | budget | run | benchmark | micros/op | ops/sec | seconds | MB/s | log dir |"
    echo "|---|---:|---:|---|---:|---:|---:|---:|---|"
    awk -F, 'NR > 1 {
      printf "| %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n",
             $1, $2, $3, $4, $5, $6, $7, $8, $9;
    }' "${SUMMARY_CSV}"
    echo
    echo "## Averages"
    echo
    echo "| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |"
    echo "|---|---|---:|---:|---:|---:|"
    awk -F, 'NR > 1 && $4 != "ERROR" {
      key = $1 "," $4;
      count[key] += 1;
      micros[key] += $5;
      ops[key] += $6;
      mb[key] += $8;
    }
    END {
      for (key in count) {
        split(key, parts, ",");
        printf "| %s | %s | %d | %.3f | %.0f | %.3f |\n",
               parts[1], parts[2], count[key],
               micros[key] / count[key],
               ops[key] / count[key],
               mb[key] / count[key];
      }
    }' "${SUMMARY_CSV}"
  } > "${SUMMARY_MD}"
}

print_config | tee "${RESULT_DIR}/compare_config.txt"
cat <<EOF | tee -a "${RESULT_DIR}/compare_config.txt"
  COMPARE_RUNS=${COMPARE_RUNS}
  M2_BENCHMARKS=${M2_BENCHMARKS}
  BUILD_ON_DIR=${BUILD_ON_DIR}
  BUILD_OFF_DIR=${BUILD_OFF_DIR}
  RUN_BUDGET_ON=${RUN_BUDGET_ON}
  RUN_BUDGET_OFF=${RUN_BUDGET_OFF}
EOF

echo "variant,budget_enabled,run,benchmark,micros_per_op,ops_per_sec,seconds,mb_per_sec,result_dir" > "${SUMMARY_CSV}"

if [[ "${RUN_BUDGET_ON}" == "1" ]]; then
  build_variant "budget_on" "1" "${BUILD_ON_DIR}"
fi
if [[ "${RUN_BUDGET_OFF}" == "1" ]]; then
  build_variant "budget_off" "0" "${BUILD_OFF_DIR}"
fi

if [[ "${RUN_BUDGET_ON}" == "1" ]]; then
  run_variant "budget_on" "1" "${BUILD_ON_DIR}"
fi
if [[ "${RUN_BUDGET_OFF}" == "1" ]]; then
  run_variant "budget_off" "0" "${BUILD_OFF_DIR}"
fi

write_markdown_summary

echo "M2 budget compare completed."
echo "CSV: ${SUMMARY_CSV}"
echo "Markdown: ${SUMMARY_MD}"
