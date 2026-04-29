#!/usr/bin/env bash
# Purpose:
#   Run a small M2 sensitivity matrix over B_min and P_target, with a
#   budget-off baseline by default.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 NUM=1000000 \
#     bash experiments/M2/run_budget_sensitivity.sh
#
#   CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 NUM=5000000 \
#     B_MIN_LIST="2 6" P_TARGET_LIST="0 0.5" \
#     bash experiments/M2/run_budget_sensitivity.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-budget-sensitivity}"
B_MIN_LIST="${B_MIN_LIST:-2 4 6}"
P_TARGET_LIST="${P_TARGET_LIST:-0 0.3 0.5}"
COMPARE_RUNS="${COMPARE_RUNS:-1}"
NUM="${NUM:-1000000}"
VALUE_SIZE="${VALUE_SIZE:-1024}"
RUN_BASELINE="${RUN_BASELINE:-1}"

mkdir -p "${RESULT_DIR}"

cat <<EOF | tee "${RESULT_DIR}/sensitivity_config.txt"
M2 sensitivity config:
  REPO_ROOT=${REPO_ROOT}
  RESULT_DIR=${RESULT_DIR}
  B_MIN_LIST=${B_MIN_LIST}
  P_TARGET_LIST=${P_TARGET_LIST}
  COMPARE_RUNS=${COMPARE_RUNS}
  NUM=${NUM}
  VALUE_SIZE=${VALUE_SIZE}
  RUN_BASELINE=${RUN_BASELINE}
  ZBD=${ZBD:-nvme0n1}
EOF

if [[ "${RUN_BASELINE}" == "1" ]]; then
  baseline_dir="${RESULT_DIR}/baseline_budget_off"
  echo "Running M2 sensitivity baseline budget_off; logs: ${baseline_dir}"

  RESULT_DIR="${baseline_dir}" \
  RUN_BUDGET_ON=0 \
  RUN_BUDGET_OFF=1 \
  SKIP_BUILD="${SKIP_BUILD:-0}" \
  COMPARE_RUNS="${COMPARE_RUNS}" \
  NUM="${NUM}" \
  VALUE_SIZE="${VALUE_SIZE}" \
  CONFIRM_MKFS="${CONFIRM_MKFS:-0}" \
  ZBD="${ZBD:-nvme0n1}" \
    bash "${SCRIPT_DIR}/run_budget_compare.sh"
fi

skip_build="${SKIP_BUILD:-0}"
for b_min in ${B_MIN_LIST}; do
  for p_target in ${P_TARGET_LIST}; do
    label="bmin_${b_min}_ptarget_${p_target//./p}"
    run_dir="${RESULT_DIR}/${label}"
    echo "Running M2 sensitivity ${label}; logs: ${run_dir}"

    RESULT_DIR="${run_dir}" \
    RUN_BUDGET_ON=1 \
    RUN_BUDGET_OFF=0 \
    SKIP_BUILD="${skip_build}" \
    COMPARE_RUNS="${COMPARE_RUNS}" \
    NUM="${NUM}" \
    VALUE_SIZE="${VALUE_SIZE}" \
    FACO_BUDGET_B_MIN="${b_min}" \
    FACO_BUDGET_P_TARGET="${p_target}" \
    CONFIRM_MKFS="${CONFIRM_MKFS:-0}" \
    ZBD="${ZBD:-nvme0n1}" \
      bash "${SCRIPT_DIR}/run_budget_compare.sh"

    skip_build=1
  done
done

python3 "${SCRIPT_DIR}/summarize_m2_results.py" \
  "${RESULT_DIR}" "${RESULT_DIR}/m2_sensitivity_summary.md"

echo "M2 sensitivity completed."
echo "Summary: ${RESULT_DIR}/m2_sensitivity_summary.md"
