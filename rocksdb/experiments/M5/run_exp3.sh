#!/usr/bin/env bash
# Exp-3: LACR coordination effect. Delegates workload execution to M4.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M5_RESULT_ROOT="${M5_RESULT_ROOT:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-exp3}"
export M5_RESULT_ROOT
source "${SCRIPT_DIR}/run_exp_common.sh"
M5_EXP3_BENCHMARKS="${M5_EXP3_BENCHMARKS:-${M5_EXP3_BENCHMARKS_DEFAULT}}"

m5_print_protocol_header
cat <<EOF | tee -a "${M5_RESULT_ROOT}/m5_protocol.log"
Exp-3:
  purpose=LACR movement breakdown and compaction coordination
  delegate=experiments/M4/run_lacr_compare.sh
  benchmarks=${M5_EXP3_BENCHMARKS}
EOF

m5_require_device_confirmation
RESULT_DIR="${M5_RESULT_ROOT}/exp3/lacr_compare" \
FACO_CONFIG_PATH="${M5_CONFIG}" \
COMPARE_RUNS="${M5_RUNS}" \
M3_BENCHMARKS="${M5_EXP3_BENCHMARKS}" \
NUM="${NUM}" \
VALUE_SIZE="${VALUE_SIZE}" \
CONFIRM_MKFS=1 \
CONFIRM_DEVICE_BENCH=1 \
  bash "${M5_REPO_ROOT}/experiments/M4/run_lacr_compare.sh"

m5_summarize_result_root "${M5_RESULT_ROOT}"
