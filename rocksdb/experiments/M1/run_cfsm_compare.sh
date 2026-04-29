#!/usr/bin/env bash
# Purpose:
#   Run the M1 CFSM on/off comparison. The script builds two release variants
#   with FACO_ENABLE_CFSM=1 and FACO_ENABLE_CFSM=0, executes the same
#   fillrandom workload for each variant, and writes summary.csv/summary.md.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=3 experiments/M1/run_cfsm_compare.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RESULT_PURPOSE="${RESULT_PURPOSE:-cfsm-compare-v1}"
source "${SCRIPT_DIR}/common.sh"

# Number of repeated runs for each variant. Increase this when collecting
# report-quality data to reduce noise.
COMPARE_RUNS="${COMPARE_RUNS:-1}"

# Keep the on/off builds separate so switching FACO_ENABLE_CFSM does not reuse
# stale object files from the other variant.
BUILD_ON_DIR="${BUILD_ON_DIR:-${REPO_ROOT}/cmake-build-release-cfsm-on}"
BUILD_OFF_DIR="${BUILD_OFF_DIR:-${REPO_ROOT}/cmake-build-release-cfsm-off}"
SUMMARY_CSV="${RESULT_DIR}/summary.csv"
SUMMARY_MD="${RESULT_DIR}/summary.md"

mkdir -p "${RESULT_DIR}"

# Configure and build one RocksDB/ZenFS release variant. SKIP_BUILD=1 lets a
# later rerun reuse already-built binaries.
build_variant() {
  local name="$1"
  local cfsm_enabled="$2"
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
    -DCMAKE_CXX_FLAGS="-DFACO_ENABLE_CFSM=${cfsm_enabled} ${EXTRA_CMAKE_CXX_FLAGS:-}"

  echo "Building ${name} db_bench and zenfs."
  cmake --build "${build_dir}" --target db_bench zenfs_tool -j"$(nproc)"
}

# Parse the db_bench fillrandom line into a machine-readable CSV row. ERROR rows
# make failed runs visible in the final summary instead of silently vanishing.
append_metrics() {
  local variant="$1"
  local cfsm_enabled="$2"
  local run_id="$3"
  local run_dir="$4"
  local log_path="${run_dir}/db_bench.log"
  if [[ ! -f "${log_path}" && -f "${run_dir}/db_bench_fillrandom.log" ]]; then
    log_path="${run_dir}/db_bench_fillrandom.log"
  fi

  if [[ ! -f "${log_path}" ]]; then
    echo "${variant},${cfsm_enabled},${run_id},ERROR,ERROR,ERROR,ERROR,ERROR,${run_dir}" >> "${SUMMARY_CSV}"
    return
  fi

  awk -v variant="${variant}" \
      -v cfsm_enabled="${cfsm_enabled}" \
      -v run_id="${run_id}" \
      -v run_dir="${run_dir}" '
    $1 == "fillrandom" && $2 == ":" {
      gsub(/;/, "", $10);
      printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             variant, cfsm_enabled, run_id, $3, $5, $7, $9, $11, run_dir;
      found = 1;
    }
    END {
      if (!found) {
        printf "%s,%s,%s,ERROR,ERROR,ERROR,ERROR,ERROR,%s\n",
               variant, cfsm_enabled, run_id, run_dir;
      }
    }
  ' "${log_path}" >> "${SUMMARY_CSV}"
}

# Render the CSV into a compact Markdown table for quick inspection in the
# terminal or a report.
write_markdown_summary() {
  {
    echo "# M1 CFSM compare summary"
    echo
    echo "- ZBD: ${ZBD}"
    echo "- DB_PATH: ${DB_PATH}"
    echo "- ZENFS_AUX_PATH: ${ZENFS_AUX_PATH}"
    echo "- NUM: ${NUM:-1000000}"
    echo "- VALUE_SIZE: ${VALUE_SIZE:-1024}"
    echo "- BENCHMARKS: ${BENCHMARKS}"
    echo "- COMPRESSION_TYPE: ${COMPRESSION_TYPE}"
    echo "- ZENFS_ENABLE_GC: ${ZENFS_ENABLE_GC:-0}"
    echo "- COMPARE_RUNS: ${COMPARE_RUNS}"
    echo
    echo "## Per-run results"
    echo
    echo "| variant | CFSM | run | micros/op | ops/sec | seconds | operations | MB/s | log dir |"
    echo "|---|---:|---:|---:|---:|---:|---:|---:|---|"
    awk -F, 'NR > 1 {
      printf "| %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n",
             $1, $2, $3, $4, $5, $6, $7, $8, $9;
    }' "${SUMMARY_CSV}"
    echo
    echo "## Averages"
    echo
    echo "| variant | runs | avg micros/op | avg ops/sec | avg MB/s |"
    echo "|---|---:|---:|---:|---:|"
    awk -F, 'NR > 1 && $4 != "ERROR" {
      count[$1] += 1;
      micros[$1] += $4;
      ops[$1] += $5;
      mb[$1] += $8;
    }
    END {
      for (variant in count) {
        printf "| %s | %d | %.3f | %.0f | %.3f |\n",
               variant, count[variant],
               micros[variant] / count[variant],
               ops[variant] / count[variant],
               mb[variant] / count[variant];
      }
    }' "${SUMMARY_CSV}"
  } > "${SUMMARY_MD}"
}

# Execute one variant for COMPARE_RUNS repetitions. Each repetition calls the
# sanity script, which performs a fresh mkfs unless SKIP_MKFS is explicitly set.
run_variant() {
  local variant="$1"
  local cfsm_enabled="$2"
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
    BENCHMARKS="${BENCHMARKS}" \
    NUM="${NUM:-1000000}" \
    VALUE_SIZE="${VALUE_SIZE:-1024}" \
    COMPRESSION_TYPE="${COMPRESSION_TYPE}" \
    ZENFS_ENABLE_GC="${ZENFS_ENABLE_GC:-0}" \
    EXTRA_DB_BENCH_ARGS="${EXTRA_DB_BENCH_ARGS:-}" \
      "${SCRIPT_DIR}/run_fillrandom_sanity.sh"

    # Append metrics only after the underlying sanity run has produced logs.
    append_metrics "${variant}" "${cfsm_enabled}" "${run_id}" "${run_dir}"
  done
}

# Record comparison-level configuration before building/running variants.
print_config | tee "${RESULT_DIR}/compare_config.txt"
cat <<EOF | tee -a "${RESULT_DIR}/compare_config.txt"
  COMPARE_RUNS=${COMPARE_RUNS}
  BUILD_ON_DIR=${BUILD_ON_DIR}
  BUILD_OFF_DIR=${BUILD_OFF_DIR}
EOF

echo "variant,cfsm_enabled,run,micros_per_op,ops_per_sec,seconds,operations,mb_per_sec,result_dir" > "${SUMMARY_CSV}"

# Build both binaries first so device runs are not interleaved with compilation.
build_variant "cfsm_on" "1" "${BUILD_ON_DIR}"
build_variant "cfsm_off" "0" "${BUILD_OFF_DIR}"

# Run the same workload against both binaries and summarize the results.
run_variant "cfsm_on" "1" "${BUILD_ON_DIR}"
run_variant "cfsm_off" "0" "${BUILD_OFF_DIR}"

write_markdown_summary

echo "M1 CFSM compare completed."
echo "CSV: ${SUMMARY_CSV}"
echo "Markdown: ${SUMMARY_MD}"
