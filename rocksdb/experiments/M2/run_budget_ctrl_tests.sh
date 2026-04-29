#!/usr/bin/env bash
# Purpose:
#   Build and run FACO M2 unit tests without touching the ZNS device.
#
# Typical usage:
#   bash experiments/M2/run_budget_ctrl_tests.sh
#   SKIP_CONFIGURE=1 bash experiments/M2/run_budget_ctrl_tests.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-budget-unit-tests}"
BUILD_DEBUG_DIR="${BUILD_DEBUG_DIR:-${REPO_ROOT}/cmake-build-debug}"

mkdir -p "${RESULT_DIR}"
cat <<EOF | tee "${RESULT_DIR}/unit_test_config.txt"
M2 unit test config:
  REPO_ROOT=${REPO_ROOT}
  BUILD_DEBUG_DIR=${BUILD_DEBUG_DIR}
  SKIP_CONFIGURE=${SKIP_CONFIGURE:-0}
  RESULT_DIR=${RESULT_DIR}
EOF

if [[ "${SKIP_CONFIGURE:-0}" != "1" ]]; then
  cmake -S "${REPO_ROOT}" -B "${BUILD_DEBUG_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DROCKSDB_PLUGINS=zenfs \
    -DWITH_ALL_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 ${EXTRA_CMAKE_CXX_FLAGS:-}"
fi

cmake --build "${BUILD_DEBUG_DIR}" --target frag_state_table_test -j"$(nproc)"
cmake --build "${BUILD_DEBUG_DIR}" --target zone_budget_ctrl_test -j"$(nproc)"

for test_name in frag_state_table_test zone_budget_ctrl_test; do
  TEST_BIN="$(find "${BUILD_DEBUG_DIR}" -name "${test_name}" -type f | head -n 1)"
  if [[ -z "${TEST_BIN}" ]]; then
    echo "${test_name} was not found under ${BUILD_DEBUG_DIR}" >&2
    exit 1
  fi
  "${TEST_BIN}" 2>&1 | tee "${RESULT_DIR}/${test_name}.log"
done
