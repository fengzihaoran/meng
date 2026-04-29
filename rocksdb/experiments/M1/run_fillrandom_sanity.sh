#!/usr/bin/env bash
# Purpose:
#   Run the M1 ZenFS db_bench sanity test. The script optionally reformats the
#   configured ZNS device with ZenFS, runs the configured db_bench benchmarks,
#   and records device/listing/CFSM logs under experiments/M1/results.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fillrandom_sanity.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Verify the benchmark and ZenFS utility before making any device changes.
require_file "${DB_BENCH}"
require_file "${ZENFS_TOOL}"

# Create a per-run result directory and save the effective configuration.
mkdir -p "${RESULT_DIR}"
print_config | tee "${RESULT_DIR}/db_bench_config.txt"

# Fresh mkfs is the default because it gives each run a clean ZenFS state. Use
# SKIP_MKFS=1 only when intentionally reusing an existing filesystem.
if [[ "${SKIP_MKFS:-0}" != "1" ]]; then
  if [[ "${CONFIRM_MKFS:-0}" != "1" ]]; then
    cat <<EOF
About to run ZenFS mkfs with --force on /dev/${ZBD}.
This destroys the existing ZenFS data on that zoned device.
Set CONFIRM_MKFS=1 to run non-interactively.
EOF
    read -r -p "Type YES to continue: " answer
    if [[ "${answer}" != "YES" ]]; then
      echo "Canceled."
      exit 1
    fi
  fi

  # mkfs requires an empty aux directory; clean it with path safety guards.
  clean_aux_path_if_needed "${ZENFS_AUX_PATH}"
  MKFS_ARGS=(mkfs --zbd="${ZBD}" --aux_path="${ZENFS_AUX_PATH}" --force)
  if [[ "${ZENFS_ENABLE_GC:-0}" == "1" ]]; then
    MKFS_ARGS+=(--enable_gc)
  fi
  run_sudo "${ZENFS_TOOL}" "${MKFS_ARGS[@]}" 2>&1 | tee "${RESULT_DIR}/mkfs.log"
else
  echo "SKIP_MKFS=1, reusing the existing ZenFS filesystem." | tee "${RESULT_DIR}/mkfs.log"
fi

# ZenFS stores lock/log/metadata files under the auxiliary path. RocksDB's
# mkdir call creates only the DB directory itself, so prepare the parent path
# that mirrors DB_PATH before opening the DB.
DB_PARENT_DIR="$(dirname -- "${DB_PATH}")"
if [[ "${DB_PARENT_DIR}" != "." ]]; then
  run_sudo mkdir -p "${ZENFS_AUX_PATH}/${DB_PARENT_DIR}"
fi

DB_BENCH_ARGS=(
  --fs_uri="zenfs://dev:${ZBD}"
  --db="${DB_PATH}"
  --benchmarks="${BENCHMARKS}"
  --num="${NUM:-1000000}"
  --value_size="${VALUE_SIZE:-1024}"
  --compression_type="${COMPRESSION_TYPE}"
  --use_direct_io_for_flush_and_compaction=true
  --use_direct_reads=true
)

if [[ -n "${EXTRA_DB_BENCH_ARGS:-}" ]]; then
  read -r -a EXTRA_ARGS <<< "${EXTRA_DB_BENCH_ARGS}"
  DB_BENCH_ARGS+=("${EXTRA_ARGS[@]}")
fi

FACO_DB_BENCH_ENV=()
for env_name in \
  FACO_BUDGET_B_MIN \
  FACO_BUDGET_B_MAX \
  FACO_BUDGET_KP \
  FACO_BUDGET_KI \
  FACO_BUDGET_P_TARGET \
  FACO_BUDGET_THETA_ZVDR \
  FACO_BUDGET_UPDATE_INTERVAL_US \
  FACO_BUDGET_TOP_K \
  FACO_BUDGET_RBD_THRESHOLD \
  FACO_BUDGET_RBD_WEIGHT \
  FACO_BUDGET_ZVDR_WEIGHT \
  FACO_REORG_W1 \
  FACO_REORG_W2 \
  FACO_REORG_W3 \
  FACO_REORG_W4 \
  FACO_REORG_WA_FACTOR \
  FACO_REORG_T_HORIZON_US \
  FACO_REORG_TAU_TRIGGER_INIT \
  FACO_REORG_TOP_K \
  FACO_REORG_TAU_MIN \
  FACO_REORG_TAU_MAX \
  FACO_REORG_TAU_EMA_ALPHA \
  FACO_REORG_FOREGROUND_P99_TARGET_US \
  FACO_REORG_TAU_PRESSURE_GAIN \
  FACO_REORG_CONTENTION_PENALTY_BYTES; do
  env_value="${!env_name-}"
  if [[ -n "${env_value}" ]]; then
    FACO_DB_BENCH_ENV+=("${env_name}=${env_value}")
  fi
done

# Use NoCompression by default so M1 measures ZenFS/CFSM behavior without being
# affected by optional Snappy linkage or CPU compression cost.
if [[ "${#FACO_DB_BENCH_ENV[@]}" -gt 0 ]]; then
  run_sudo env "${FACO_DB_BENCH_ENV[@]}" "${DB_BENCH}" "${DB_BENCH_ARGS[@]}" \
    2>&1 | tee "${RESULT_DIR}/db_bench.log"
else
  run_sudo "${DB_BENCH}" "${DB_BENCH_ARGS[@]}" \
    2>&1 | tee "${RESULT_DIR}/db_bench.log"
fi

# ZenFS writes CFSM exports to aux storage during shutdown. Copy them into the
# run directory before the next run has a chance to clean the aux path.
{
  for export_file in faco_cfsm_summary.txt faco_cfsm_zones.csv; do
    src="${ZENFS_AUX_PATH}/${export_file}"
    dst="${RESULT_DIR}/${export_file}"
    if run_sudo test -f "${src}"; then
      run_sudo cat "${src}" > "${dst}"
      echo "Copied ${src} to ${dst}"
    else
      echo "CFSM export not found: ${src}"
    fi
  done
} | tee "${RESULT_DIR}/faco_cfsm_export.log"

# Capture the resulting ZenFS directory view. SKIP_LIST=1 is available if a
# local environment has a list-only issue but the benchmark path works.
if [[ "${SKIP_LIST:-0}" != "1" ]]; then
  run_sudo "${ZENFS_TOOL}" list \
    --zbd="${ZBD}" \
    --path=rocksdbtest 2>&1 | tee "${RESULT_DIR}/zenfs_list.log"
else
  echo "SKIP_LIST=1, skipping zenfs list." | tee "${RESULT_DIR}/zenfs_list.log"
fi

echo "M1 db_bench sanity completed. Logs are in ${RESULT_DIR}"
