#!/usr/bin/env bash
# Exp-4: FACO ablation matrix and sensitivity scaffold.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M5_RESULT_ROOT="${M5_RESULT_ROOT:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-exp4}"
export M5_RESULT_ROOT
source "${SCRIPT_DIR}/run_exp_common.sh"
M5_EXP4_BENCHMARKS="${M5_EXP4_BENCHMARKS:-${M5_EXP4_BENCHMARKS_DEFAULT}}"

declare -a VARIANTS=(
  "native|-DFACO_ENABLE_CFSM=0 -DFACO_ENABLE_BUDGET=0 -DFACO_ENABLE_REORG=0 -DFACO_ENABLE_LACR=0|0"
  "cfsm_only|-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=0 -DFACO_ENABLE_REORG=0 -DFACO_ENABLE_LACR=0|0"
  "without_ebcr|-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=0 -DFACO_ENABLE_REORG=0 -DFACO_ENABLE_LACR=1|1"
  "without_lacr|-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=1 -DFACO_ENABLE_LACR=1|0"
  "full_faco|-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=1 -DFACO_ENABLE_LACR=1|1"
)

m5_print_protocol_header
cat <<EOF | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
Exp-4:
  purpose=ablation matrix
  benchmarks=${M5_EXP4_BENCHMARKS}
  unsupported=B4 CAZA and B5 static lifetime-hint require separate implementation; this script records FACO-internal ablations only.
EOF

m5_require_device_confirmation

for spec in "${VARIANTS[@]}"; do
  IFS='|' read -r variant cflags lacr_enable <<< "${spec}"
  build_dir="${M5_REPO_ROOT}/cmake-build-release-m5-${variant}"
  m5_build_variant "${variant}" "${build_dir}" "${cflags}"

  for run_id in $(seq 1 "${M5_RUNS}"); do
    run_dir="${M5_RESULT_ROOT}/exp4/${variant}/run_${run_id}"
    FACO_LACR_ENABLE="${lacr_enable}" \
      m5_run_db_bench_workload "${variant}" "${build_dir}" "${run_dir}" \
        "${M5_EXP4_BENCHMARKS}"
  done
done

m5_summarize_result_root "${M5_RESULT_ROOT}"
