# M1 fragmentation workload analysis

Result directory: `experiments/M1/results/20260428-130502`

## Workload

- Benchmarks: `fillrandom,overwrite,overwrite,compact,stats`
- Entries: `1,000,000`
- Value size: `1024`
- Compression: `none`
- ZenFS GC: disabled

## db_bench result

- `fillrandom`: `194754 ops/sec`, `193.2 MB/s`
- first `overwrite`: `177889 ops/sec`, `176.4 MB/s`
- second `overwrite`: `160428 ops/sec`, `159.1 MB/s`
- `compact`: `12.917 sec`

RocksDB stats:

- cumulative writes: `3000K`
- cumulative flush: `2.858 GB`
- cumulative compaction write: `8.86 GB`
- cumulative compaction read: `7.93 GB`
- write stalls: `0`

## CFSM export result

- zone capacity: `268435456` bytes
- total zones: `200`
- active zones: `17`
- empty zones: `183`
- total valid bytes: `1,002,535,689`
- active-zone average RBD: about `0.468186`
- max RBD: `0.599984`

Top victim candidates by RBD:

| rank | zone | valid bytes | valid ratio | RBD | ZVDR |
|---:|---:|---:|---:|---:|---:|
| 1 | 4 | 7317 | 0.000027 | 0.599984 | 0 |
| 2 | 3 | 20640 | 0.000077 | 0.599954 | 1.4013e-45 |
| 3 | 39 | 58444456 | 0.217723 | 0.469366 | 0 |
| 4 | 23 | 67433066 | 0.251208 | 0.449275 | 0 |
| 5 | 6 | 67433080 | 0.251208 | 0.449275 | 0 |

## Interpretation

This result is good for the RBD/top-victim part of M1:

- Empty zones are now separated correctly as `empty_zones=183`.
- Only active zones are classified as fragmented.
- `top_rbd_zones` identifies low-live-data-density zones that would be strong GC candidates.

`ZVDR` is still not meaningful in this run. The only non-zero value is
`1.4013e-45`, which is effectively floating-point noise. A follow-up code patch
after this run adds a minimum ZVDR sampling window and raises the hot threshold,
so denormal noise should no longer create a `HOT_HIGH` class.

## M1 status

M1 has enough evidence to hand off to M2:

- correctness tests pass in prior runs
- on/off overhead is around 1 percent in the 10-run comparison
- fragmentation workload exports active/empty zone counts
- RBD and top victim zones are usable

Recommended final check before archiving M1:

```bash
cd ~/rocksdb
cmake --build cmake-build-debug --target frag_state_table_test -j$(nproc)
./cmake-build-debug/frag_state_table_test

cmake --build cmake-build-release-cfsm-on --target db_bench zenfs_tool -j$(nproc)
BUILD_RELEASE_DIR=$PWD/cmake-build-release-cfsm-on \
DB_BENCH=$PWD/cmake-build-release-cfsm-on/db_bench \
ZENFS_TOOL=$PWD/cmake-build-release-cfsm-on/zenfs \
CONFIRM_MKFS=1 ZBD=nvme0n1 \
experiments/M1/run_fragmentation_workload.sh
```

If time is limited, M2 can start now using RBD/top victim zones first; ZVDR can
be treated as an additional signal once the final rerun confirms it.
