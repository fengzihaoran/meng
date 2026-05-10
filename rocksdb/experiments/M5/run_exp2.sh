#!/usr/bin/env bash
# Exp-2: long fragmentation evolution trace.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M5_RESULT_ROOT="${M5_RESULT_ROOT:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-exp2}"
export M5_RESULT_ROOT
source "${SCRIPT_DIR}/run_exp_common.sh"

M5_EXP2_BENCHMARKS="${M5_EXP2_BENCHMARKS:-${M5_EXP2_BENCHMARKS_DEFAULT}}"
BUILD_FULL_DIR="${BUILD_FULL_DIR:-${M5_REPO_ROOT}/cmake-build-release-m5-full}"

m5_print_protocol_header
cat <<EOF | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
Exp-2:
  purpose=fragmentation evolution and zone state time-series
  benchmarks=${M5_EXP2_BENCHMARKS}
  note=use NUM/EXTRA_DB_BENCH_ARGS to choose 1h development or 12h paper runs
EOF

m5_require_device_confirmation
m5_build_variant "full_faco" "${BUILD_FULL_DIR}" \
  "-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=1 -DFACO_ENABLE_LACR=1"

for run_id in $(seq 1 "${M5_RUNS}"); do
  run_dir="${M5_RESULT_ROOT}/exp2/full_faco/run_${run_id}"
  FACO_LACR_ENABLE=1 \
    m5_run_db_bench_workload "full_faco" "${BUILD_FULL_DIR}" "${run_dir}" \
      "${M5_EXP2_BENCHMARKS}"
done

m5_summarize_result_root "${M5_RESULT_ROOT}"
