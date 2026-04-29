#!/usr/bin/env bash
# Purpose:
#   Run the M2 correctness workflow. Device workload is opt-in because it
#   reformats the configured ZenFS ZNS device unless SKIP_MKFS=1 is set.
#
# Typical usage:
#   bash experiments/M2/M2.sh
#   RUN_DEVICE_WORKLOAD=1 CONFIRM_MKFS=1 ZBD=nvme0n1 bash experiments/M2/M2.sh
#   RUN_COMPARE=1 CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=3 bash experiments/M2/M2.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

bash "${SCRIPT_DIR}/run_budget_ctrl_tests.sh"

if [[ "${RUN_DEVICE_WORKLOAD:-0}" == "1" ]]; then
  bash "${SCRIPT_DIR}/run_budget_workload.sh"
else
  echo "Skipping M2 device workload. Set RUN_DEVICE_WORKLOAD=1 to run it."
fi

if [[ "${RUN_COMPARE:-0}" == "1" ]]; then
  bash "${SCRIPT_DIR}/run_budget_compare.sh"
else
  echo "Skipping M2 budget compare. Set RUN_COMPARE=1 to run it."
fi
