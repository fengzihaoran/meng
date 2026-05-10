#!/usr/bin/env bash
# Exp-1: single-tenant performance guardrail for full FACO.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M5_RESULT_ROOT="${M5_RESULT_ROOT:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-exp1}"
export M5_RESULT_ROOT
source "${SCRIPT_DIR}/run_exp_common.sh"

M5_EXP1_BENCHMARKS="${M5_EXP1_BENCHMARKS:-${M5_EXP1_BENCHMARKS_DEFAULT}}"
BUILD_FULL_DIR="${BUILD_FULL_DIR:-${M5_REPO_ROOT}/cmake-build-release-m5-full}"

m5_print_protocol_header
cat <<EOF | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
Exp-1:
  purpose=single tenant throughput, latency, and WA guardrail
  benchmarks=${M5_EXP1_BENCHMARKS}
  build=${BUILD_FULL_DIR}
EOF

m5_require_device_confirmation
m5_build_variant "full_faco" "${BUILD_FULL_DIR}" \
  "-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=1 -DFACO_ENABLE_LACR=1"

for run_id in $(seq 1 "${M5_RUNS}"); do
  run_dir="${M5_RESULT_ROOT}/exp1/full_faco/run_${run_id}"
  FACO_LACR_ENABLE=1 \
    m5_run_db_bench_workload "full_faco" "${BUILD_FULL_DIR}" "${run_dir}" \
      "${M5_EXP1_BENCHMARKS}"
done

m5_summarize_result_root "${M5_RESULT_ROOT}"
