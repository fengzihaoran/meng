#!/usr/bin/env bash
# Purpose:
#   Run the M3 workflow.  By default this only runs unit tests; device
#   workloads are opt-in because they require mkfs on a ZNS device.
#
# Typical usage:
#   bash experiments/M3/M3.sh
#   RUN_DEVICE_WORKLOAD=1 CONFIRM_MKFS=1 ZBD=nvme0n1 bash experiments/M3/M3.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

bash "${SCRIPT_DIR}/run_reorg_tests.sh"

if [[ "${RUN_DEVICE_WORKLOAD:-0}" == "1" ]]; then
  bash "${SCRIPT_DIR}/run_reorg_workload.sh"
fi

if [[ "${RUN_COMPARE:-0}" == "1" ]]; then
  bash "${SCRIPT_DIR}/run_reorg_compare.sh"
else
  echo "Skipping M3 reorg compare. Set RUN_COMPARE=1 to run it."
fi
