#!/usr/bin/env bash
# Run the complete M5 benchmark protocol. Guarded by CONFIRM_DEVICE_BENCH.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
M5_RESULT_ROOT="${M5_RESULT_ROOT:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-run-all}"
export M5_RESULT_ROOT

if [[ "${CONFIRM_DEVICE_BENCH:-0}" != "1" ]]; then
  mkdir -p "${M5_RESULT_ROOT}"
  cat > "${M5_RESULT_ROOT}/DRY_RUN_PROTOCOL.txt" <<EOF
M5 run_all dry run

Device benchmarks were not started because CONFIRM_DEVICE_BENCH is not 1.
This is the expected mode while M5 scripts and summaries are being completed.

To execute later:
  CONFIRM_DEVICE_BENCH=1 ZBD=<device-name> M5_PROFILE=smoke bash experiments/M5/run_all.sh

Profiles:
  smoke: 1 run, NUM=100000, no readwhilewriting by default
  dev:   1 run, NUM=1000000, moderate protocol
  paper: 5 runs, NUM=5000000, full long protocol

Planned steps:
  1. run_exp1.sh: single-tenant performance guardrail
  2. run_exp2.sh: fragmentation evolution traces
  3. run_exp3.sh: LACR movement breakdown
  4. run_exp4.sh: FACO-internal ablation matrix
  5. summarize_traces.py over the full result root
EOF
  cat "${M5_RESULT_ROOT}/DRY_RUN_PROTOCOL.txt"
  exit 0
fi

bash "${SCRIPT_DIR}/run_exp1.sh"
bash "${SCRIPT_DIR}/run_exp2.sh"
bash "${SCRIPT_DIR}/run_exp3.sh"
bash "${SCRIPT_DIR}/run_exp4.sh"
python3 "${SCRIPT_DIR}/summarize_traces.py" "${M5_RESULT_ROOT}" \
  "${M5_RESULT_ROOT}/m5_summary.md"
