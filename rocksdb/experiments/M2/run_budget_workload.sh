#!/usr/bin/env bash
# Purpose:
#   Run a ZenFS db_bench workload with FACO M2 budget control enabled and
#   collect the exported budget curve.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 bash experiments/M2/run_budget_workload.sh
set -euo pipefail

M2_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${M2_SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${M2_SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-budget-workload}"

mkdir -p "${RESULT_DIR}"
cat <<EOF | tee "${RESULT_DIR}/budget_workload_config.txt"
M2 budget workload:
  REPO_ROOT=${REPO_ROOT}
  RESULT_DIR=${RESULT_DIR}
  M2_BENCHMARKS=${M2_BENCHMARKS:-fillrandom,overwrite,overwrite,stats}
  NUM=${NUM:-5000000}
  VALUE_SIZE=${VALUE_SIZE:-1024}
  DURATION=${DURATION:-600}
  ZENFS_ENABLE_GC=1
  FACO_BUDGET_B_MIN=${FACO_BUDGET_B_MIN:-2}
  FACO_BUDGET_B_MAX=${FACO_BUDGET_B_MAX:-12}
  FACO_BUDGET_KP=${FACO_BUDGET_KP:-0.6}
  FACO_BUDGET_KI=${FACO_BUDGET_KI:-0.05}
  FACO_BUDGET_P_TARGET=${FACO_BUDGET_P_TARGET:-0}
  FACO_BUDGET_TOP_K=${FACO_BUDGET_TOP_K:-8}
  FACO_BUDGET_RBD_THRESHOLD=${FACO_BUDGET_RBD_THRESHOLD:-0.05}
  FACO_BUDGET_ZVDR_WEIGHT=${FACO_BUDGET_ZVDR_WEIGHT:-0}
EOF

EXTRA_ARGS="${EXTRA_DB_BENCH_ARGS:-}"
if [[ -z "${EXTRA_ARGS}" ]]; then
  EXTRA_ARGS="--duration=${DURATION:-600}"
fi

BENCHMARKS="${M2_BENCHMARKS:-fillrandom,overwrite,overwrite,stats}" \
RESULT_DIR="${RESULT_DIR}" \
CONFIRM_MKFS="${CONFIRM_MKFS:-0}" \
ZBD="${ZBD:-nvme0n1}" \
ZENFS_AUX_PATH="${ZENFS_AUX_PATH:-/home/femu/mnt/zenfs_aux}" \
DB_PATH="${DB_PATH:-rocksdbtest/dbbench}" \
NUM="${NUM:-5000000}" \
VALUE_SIZE="${VALUE_SIZE:-1024}" \
COMPRESSION_TYPE="${COMPRESSION_TYPE:-none}" \
ZENFS_ENABLE_GC=1 \
EXTRA_DB_BENCH_ARGS="${EXTRA_ARGS}" \
FACO_BUDGET_B_MIN="${FACO_BUDGET_B_MIN:-2}" \
FACO_BUDGET_B_MAX="${FACO_BUDGET_B_MAX:-12}" \
FACO_BUDGET_KP="${FACO_BUDGET_KP:-0.6}" \
FACO_BUDGET_KI="${FACO_BUDGET_KI:-0.05}" \
FACO_BUDGET_P_TARGET="${FACO_BUDGET_P_TARGET:-0}" \
FACO_BUDGET_TOP_K="${FACO_BUDGET_TOP_K:-8}" \
FACO_BUDGET_RBD_THRESHOLD="${FACO_BUDGET_RBD_THRESHOLD:-0.05}" \
FACO_BUDGET_ZVDR_WEIGHT="${FACO_BUDGET_ZVDR_WEIGHT:-0}" \
  bash "${REPO_ROOT}/experiments/M1/run_fillrandom_sanity.sh"

{
  for export_file in \
    faco_budget_summary.txt \
    faco_budget_trace.csv \
    faco_runtime_metrics.txt; do
    src="${ZENFS_AUX_PATH:-/home/femu/mnt/zenfs_aux}/${export_file}"
    dst="${RESULT_DIR}/${export_file}"
    if [[ "${EUID}" -eq 0 ]]; then
      test -f "${src}" && cat "${src}" > "${dst}" && echo "Copied ${src} to ${dst}" || echo "Budget export not found: ${src}"
    else
      sudo test -f "${src}" && sudo cat "${src}" > "${dst}" && echo "Copied ${src} to ${dst}" || echo "Budget export not found: ${src}"
    fi
  done
} | tee "${RESULT_DIR}/faco_budget_export.log"

if [[ -f "${RESULT_DIR}/faco_budget_trace.csv" ]]; then
  python3 "${M2_SCRIPT_DIR}/analyze_budget_trace.py" \
    "${RESULT_DIR}/faco_budget_trace.csv" "${RESULT_DIR}"
fi

echo "M2 budget workload completed. Inspect:"
echo "  ${RESULT_DIR}/faco_budget_summary.txt"
echo "  ${RESULT_DIR}/faco_budget_trace.csv"
echo "  ${RESULT_DIR}/faco_runtime_metrics.txt"
echo "  ${RESULT_DIR}/budget_trace_analysis.md"
