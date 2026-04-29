#!/usr/bin/env bash
# Purpose:
#   Build and run the M3 ReorgPlanner unit tests.  This validates the
#   benefit-cost planner without touching a ZNS device.
#
# Typical usage:
#   bash experiments/M3/run_reorg_tests.sh
#   SKIP_CONFIGURE=1 bash experiments/M3/run_reorg_tests.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/results/$(date +%Y%m%d-%H%M%S)-reorg-unit-tests}"
BUILD_DEBUG_DIR="${BUILD_DEBUG_DIR:-${REPO_ROOT}/cmake-build-debug}"

mkdir -p "${RESULT_DIR}"

if [[ "${SKIP_CONFIGURE:-0}" != "1" ]]; then
  cmake -S "${REPO_ROOT}" -B "${BUILD_DEBUG_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DROCKSDB_PLUGINS=zenfs \
    -DWITH_ALL_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-DFACO_ENABLE_CFSM=1 -DFACO_ENABLE_BUDGET=1 -DFACO_ENABLE_REORG=1 ${EXTRA_CMAKE_CXX_FLAGS:-}"
fi

cmake --build "${BUILD_DEBUG_DIR}" --target reorg_planner_test -j"$(nproc)"

for test_name in reorg_planner_test; do
  echo "Running ${test_name}"
  "${BUILD_DEBUG_DIR}/${test_name}" 2>&1 | tee "${RESULT_DIR}/${test_name}.log"
done

echo "M3 ReorgPlanner tests completed. Logs are in ${RESULT_DIR}"
