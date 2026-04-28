#!/usr/bin/env bash
# Purpose:
#   Run the complete M1 sanity workflow: first verify the CFSM unit tests, then
#   run a ZenFS-backed db_bench fillrandom smoke test on the configured ZNS
#   device.
#
# Typical usage:
#   CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/M1.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Validate the in-memory fragmentation state model before touching the device.
"${SCRIPT_DIR}/run_unit_tests.sh"

# Reformat the ZenFS device and run a small RocksDB write workload.
"${SCRIPT_DIR}/run_fillrandom_sanity.sh"
