#!/usr/bin/env bash
# Purpose:
#   Shared configuration and helper functions for all M1 experiment scripts.
#   This file is sourced by other scripts and should not be executed directly.
set -euo pipefail

# Resolve paths relative to this script so commands work from any cwd.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

# Build output locations. Override these from the environment when comparing
# multiple build variants.
BUILD_DEBUG_DIR="${BUILD_DEBUG_DIR:-${REPO_ROOT}/cmake-build-debug}"
BUILD_RELEASE_DIR="${BUILD_RELEASE_DIR:-${REPO_ROOT}/cmake-build-release}"
DB_BENCH="${DB_BENCH:-${BUILD_RELEASE_DIR}/db_bench}"

# Prefer the zenfs binary from the active release build. The copied utility is
# only a fallback because it can become stale after rebuilding.
if [[ -z "${ZENFS_TOOL:-}" ]]; then
  if [[ -x "${BUILD_RELEASE_DIR}/zenfs" ]]; then
    ZENFS_TOOL="${BUILD_RELEASE_DIR}/zenfs"
  else
    ZENFS_TOOL="${REPO_ROOT}/plugin/zenfs/util/zenfs"
  fi
fi

# ZenFS' zbdlib backend prepends "/dev/" internally. Keep this as "nvme0n1",
# not "/dev/nvme0n1".
ZBD="${ZBD:-nvme0n1}"
ZENFS_AUX_PATH="${ZENFS_AUX_PATH:-/home/femu/mnt/zenfs_aux}"
DB_PATH="${DB_PATH:-rocksdbtest/dbbench}"
BENCHMARKS="${BENCHMARKS:-fillrandom}"
COMPRESSION_TYPE="${COMPRESSION_TYPE:-none}"
RESULT_PURPOSE="${RESULT_PURPOSE:-m1-run-v1}"
RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-${RESULT_PURPOSE}}"
RESULT_OWNER="${RESULT_OWNER:-${SUDO_USER:-}}"

# If the user accidentally runs the whole script through sudo, restore result
# log ownership on exit so later non-root inspection/editing still works.
fix_result_owner() {
  if [[ "${EUID}" -eq 0 && -n "${RESULT_OWNER}" && -d "${RESULT_DIR}" ]]; then
    local owner_group
    owner_group="$(id -gn "${RESULT_OWNER}" 2>/dev/null || echo "${RESULT_OWNER}")"
    chown -R "${RESULT_OWNER}:${owner_group}" "${RESULT_DIR}" 2>/dev/null || true
  fi
}

trap fix_result_owner EXIT

# Fail early with a clear message when a required binary has not been built.
require_file() {
  local path="$1"
  if [[ ! -e "${path}" ]]; then
    echo "Missing required file: ${path}" >&2
    exit 1
  fi
}

# Run a command with sudo only when the current process is not already root.
run_sudo() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

# Guard against catastrophic cleanup if ZENFS_AUX_PATH is unset or too broad.
ensure_safe_aux_path() {
  local aux_path="$1"
  case "${aux_path}" in
    ""|"/"|"/home"|"/home/"|"/home/femu"|"/home/femu/"|"/home/femu/mnt"|"/home/femu/mnt/"|"/tmp"|"/tmp/"|"/mnt"|"/mnt/")
      echo "Refusing to clean unsafe ZENFS_AUX_PATH: ${aux_path}" >&2
      exit 1
      ;;
  esac
}

# Prepare an empty ZenFS auxiliary directory before mkfs. ZenFS refuses to
# format when stale files from a previous run are still present.
clean_aux_path_if_needed() {
  local aux_path="$1"
  ensure_safe_aux_path "${aux_path}"
  run_sudo mkdir -p "${aux_path}"

  # ZenFS mkfs requires an empty auxiliary directory. Remove stale lock/log
  # files from previous runs, but only after the path has passed the guard
  # above so a bad variable cannot wipe a broad system directory.
  if [[ -n "$(run_sudo find "${aux_path}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    echo "Aux directory ${aux_path} is not empty; deleting its contents before mkfs."
    run_sudo find "${aux_path}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
  fi
}

# Print the effective runtime configuration into both the terminal and logs.
print_config() {
  cat <<EOF
M1 experiment config:
  REPO_ROOT=${REPO_ROOT}
  ZBD=${ZBD}
  ZENFS_AUX_PATH=${ZENFS_AUX_PATH}
  DB_BENCH=${DB_BENCH}
  ZENFS_TOOL=${ZENFS_TOOL}
  BENCHMARKS=${BENCHMARKS}
  NUM=${NUM:-1000000}
  VALUE_SIZE=${VALUE_SIZE:-1024}
  COMPRESSION_TYPE=${COMPRESSION_TYPE}
  ZENFS_ENABLE_GC=${ZENFS_ENABLE_GC:-0}
  FACO_CONFIG_PATH=${FACO_CONFIG_PATH:-}
  FACO_LACR_ENABLE=${FACO_LACR_ENABLE:-}
  EXTRA_DB_BENCH_ARGS=${EXTRA_DB_BENCH_ARGS:-}
  RESULT_DIR=${RESULT_DIR}
EOF
  if [[ "${EUID}" -eq 0 && -n "${SUDO_USER:-}" ]]; then
    echo "  NOTE=running as root via sudo; prefer running this script as ${SUDO_USER} and let it sudo only device operations"
  fi
}
