#!/usr/bin/env bash
# Purpose:
#   Run a M1 workload that creates more invalidated SST data than plain
#   fillrandom. This is used after the basic CFSM on/off throughput comparison
#   to verify that CFSM exports meaningful zone fragmentation state.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fragmentation_workload.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# overwrite and compaction force RocksDB to create newer SSTs and obsolete older
# files, which gives CFSM delete/reset hooks real fragmentation signals.
FRAG_BENCHMARKS="${FRAG_BENCHMARKS:-fillrandom,overwrite,overwrite,compact,stats}"

mkdir -p "${RESULT_DIR}"
cat <<EOF | tee "${RESULT_DIR}/fragmentation_workload_config.txt"
M1 fragmentation workload:
  FRAG_BENCHMARKS=${FRAG_BENCHMARKS}
  NUM=${NUM:-1000000}
  VALUE_SIZE=${VALUE_SIZE:-1024}
  RESULT_DIR=${RESULT_DIR}
EOF

BENCHMARKS="${FRAG_BENCHMARKS}" \
RESULT_DIR="${RESULT_DIR}" \
CONFIRM_MKFS="${CONFIRM_MKFS:-0}" \
ZBD="${ZBD}" \
ZENFS_AUX_PATH="${ZENFS_AUX_PATH}" \
DB_PATH="${DB_PATH}" \
NUM="${NUM:-1000000}" \
VALUE_SIZE="${VALUE_SIZE:-1024}" \
COMPRESSION_TYPE="${COMPRESSION_TYPE}" \
ZENFS_ENABLE_GC="${ZENFS_ENABLE_GC:-0}" \
EXTRA_DB_BENCH_ARGS="${EXTRA_DB_BENCH_ARGS:-}" \
  "${SCRIPT_DIR}/run_fillrandom_sanity.sh"

echo "M1 fragmentation workload completed. Inspect:"
echo "  ${RESULT_DIR}/faco_cfsm_summary.txt"
echo "  ${RESULT_DIR}/faco_cfsm_zones.csv"
