#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/fragsense/build_linux.sh [options]

Create a non-destructive clean Linux build of RocksDB with ZenFS.

Options:
  --source-dir PATH  RocksDB source directory (default: repository root)
  --build-dir PATH   Build directory (default: SOURCE/cmake-build-fragsense-m05)
  --jobs N           Parallel build jobs (default: nproc)
  --no-clean         Reuse the build directory instead of removing it first
  --skip-tests       Build targets but do not run ctest
  -h, --help         Show this help

The script never runs zenfs mkfs, resets a Zone, or writes to a block device.
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DEFAULT_SOURCE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR=$DEFAULT_SOURCE_DIR
BUILD_DIR=
JOBS=
CLEAN=1
RUN_TESTS=1

while (($# > 0)); do
  case "$1" in
    --source-dir)
      (($# >= 2)) || die "--source-dir requires a value"
      SOURCE_DIR=$2
      shift 2
      ;;
    --build-dir)
      (($# >= 2)) || die "--build-dir requires a value"
      BUILD_DIR=$2
      shift 2
      ;;
    --jobs)
      (($# >= 2)) || die "--jobs requires a value"
      JOBS=$2
      shift 2
      ;;
    --no-clean)
      CLEAN=0
      shift
      ;;
    --skip-tests)
      RUN_TESTS=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ $(uname -s) == Linux ]] || die "this gate must run on Linux"

require_command cmake
require_command c++
require_command ctest
require_command git
require_command nproc
require_command pkg-config
require_command realpath
require_command tee

SOURCE_DIR=$(realpath -e -- "$SOURCE_DIR")
[[ -f "$SOURCE_DIR/CMakeLists.txt" ]] || die "not a RocksDB source tree: $SOURCE_DIR"
[[ -f "$SOURCE_DIR/plugin/zenfs/CMakeLists.txt" ]] || die "ZenFS plugin missing under: $SOURCE_DIR"

GIT_PREFIX=$(git -C "$SOURCE_DIR" rev-parse --show-prefix)
ZENFS_GIT_PATH="${GIT_PREFIX}plugin/zenfs"
git -C "$SOURCE_DIR" cat-file -e "HEAD:$ZENFS_GIT_PATH" ||
  die "ZenFS subtree is not tracked at HEAD:$ZENFS_GIT_PATH"

if [[ -z $BUILD_DIR ]]; then
  BUILD_DIR="$SOURCE_DIR/cmake-build-fragsense-m05"
fi
BUILD_DIR=$(realpath -m -- "$BUILD_DIR")

if [[ -z $JOBS ]]; then
  JOBS=$(nproc)
fi
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || die "--jobs must be a positive integer"

pkg-config --atleast-version=1.5.0 libzbd ||
  die "libzbd >= 1.5.0 is required"

if ((CLEAN)); then
  case "$BUILD_DIR" in
    "$SOURCE_DIR"/cmake-build-fragsense-*) ;;
    *)
      die "refusing to clean build directory outside SOURCE/cmake-build-fragsense-*"
      ;;
  esac
  rm -rf -- "$BUILD_DIR"
fi

mkdir -p -- "$BUILD_DIR"
EVIDENCE_DIR="$BUILD_DIR/fragsense-m05-evidence"
mkdir -p -- "$EVIDENCE_DIR"

STATUS_FILE="$EVIDENCE_DIR/status.txt"
LOG_FILE="$EVIDENCE_DIR/build-and-test.log"
on_exit() {
  rc=$?
  trap - EXIT
  if ((rc == 0)); then
    printf 'PASSED\n' >"$STATUS_FILE"
  else
    printf 'FAILED exit_code=%d\n' "$rc" >"$STATUS_FILE"
  fi
  exit "$rc"
}
trap on_exit EXIT

exec > >(tee "$LOG_FILE") 2>&1

{
  printf 'timestamp_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'source_dir=%s\n' "$SOURCE_DIR"
  printf 'build_dir=%s\n' "$BUILD_DIR"
  printf 'git_commit=%s\n' "$(git -C "$SOURCE_DIR" rev-parse HEAD)"
  printf 'zenfs_upstream_candidate=%s\n' \
    '919c2ebbcdc170525a9abffb8b61a3795b1e6ae5'
  printf 'zenfs_git_path=%s\n' "$ZENFS_GIT_PATH"
  printf 'zenfs_tree=%s\n' "$(git -C "$SOURCE_DIR" rev-parse "HEAD:$ZENFS_GIT_PATH")"
  printf 'kernel=%s\n' "$(uname -a)"
  printf 'compiler=%s\n' "$(c++ --version | head -n 1)"
  printf 'cmake=%s\n' "$(cmake --version | head -n 1)"
  printf 'libzbd=%s\n' "$(pkg-config --modversion libzbd)"
  printf 'jobs=%s\n' "$JOBS"
  printf 'clean=%s\n' "$CLEAN"
  printf 'run_tests=%s\n' "$RUN_TESTS"
  printf '%s\n' 'git_status_begin'
  git -C "$SOURCE_DIR" status --porcelain
  printf '%s\n' 'git_status_end'
} >"$EVIDENCE_DIR/environment.txt"

CONFIGURE_CMD=(
  cmake -S "$SOURCE_DIR" -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE=Release
  -DROCKSDB_PLUGINS=zenfs
  -DWITH_GFLAGS=ON
  -DWITH_SNAPPY=ON
  -DWITH_TESTS=ON
  -DWITH_ALL_TESTS=OFF
  -DWITH_BENCHMARK_TOOLS=ON
  -DWITH_ZENFS_TOOL=ON
)
BUILD_CMD=(
  cmake --build "$BUILD_DIR"
  --target rocksdb-shared zenfs_tool db_bench
  --parallel "$JOBS"
)
TEST_CMD=(
  ctest --test-dir "$BUILD_DIR" --output-on-failure
)

printf '%q ' "${CONFIGURE_CMD[@]}" >"$EVIDENCE_DIR/configure.command"
printf '\n' >>"$EVIDENCE_DIR/configure.command"
printf '%q ' "${BUILD_CMD[@]}" >"$EVIDENCE_DIR/build.command"
printf '\n' >>"$EVIDENCE_DIR/build.command"
printf '%q ' "${TEST_CMD[@]}" >"$EVIDENCE_DIR/test.command"
printf '\n' >>"$EVIDENCE_DIR/test.command"

printf 'fragsense: configuring clean Linux ZenFS build\n'
"${CONFIGURE_CMD[@]}"

printf 'fragsense: building rocksdb-shared, zenfs_tool, and db_bench\n'
"${BUILD_CMD[@]}"

if ((RUN_TESTS)); then
  printf 'fragsense: running non-device CTest suite\n'
  "${TEST_CMD[@]}"
else
  printf 'fragsense: tests skipped by --skip-tests\n'
fi

printf 'fragsense: evidence written to %s\n' "$EVIDENCE_DIR"
