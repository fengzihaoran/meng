# M1 fragmentation workload analysis

Result directory: `experiments/M1/results/20260428-125951`

## Workload

- Benchmarks: `fillrandom,overwrite,overwrite,compact,stats`
- Entries: `1,000,000`
- Value size: `1024`
- Compression: `none`
- ZenFS GC: disabled

## db_bench result

- `fillrandom`: `156834 ops/sec`, `155.6 MB/s`
- first `overwrite`: `204354 ops/sec`, `202.7 MB/s`
- second `overwrite`: `172651 ops/sec`, `171.2 MB/s`
- `compact`: `8.132 sec`

RocksDB stats confirm that the workload created enough LSM churn for M1:

- cumulative writes: `3000K`
- cumulative flush: `2.858 GB`
- cumulative compaction write: `8.71 GB`
- cumulative compaction read: `7.78 GB`
- write stalls: `0`

## CFSM result

- total zones: `200`
- active zones: `18`
- empty zones: `182`
- total valid bytes: `1,002,802,800`
- active zones are correctly separated from empty zones
- all active zones are currently classified as `COLD_HIGH`

Top RBD victim candidates:

| rank | zone | valid bytes | RBD | meaning |
|---:|---:|---:|---:|---|
| 1 | 4 | 7317 | 0.599984 | almost empty but still has tiny live data |
| 2 | 3 | 19289 | 0.599957 | almost empty but still has tiny live data |
| 3 | 37 | 24524030 | 0.545185 | low live-data density |
| 4 | 11 | 34188887 | 0.523582 | low live-data density |
| 5 | 28 | 67433075 | 0.449275 | partially occupied |

## Interpretation

This run proves that M1 can now export useful RBD and top victim-zone signals.
The earlier bug where empty zones were counted as high fragmentation is fixed:
empty zones are now reported separately as `empty_zones=182`.

`zvdr_ema` is still `0` in this run. That means the final shutdown Tick alone did
not observe validity decay before reset/cleanup erased the short-lived decay
signal. A follow-up patch now samples ZVDR immediately after `OnDelete`, so the
next run should show non-zero ZVDR for zones affected by overwrite/compaction.

## Next Step

Rebuild and rerun this workload once more:

```bash
cd ~/rocksdb
cmake --build cmake-build-release-cfsm-on --target db_bench zenfs_tool -j$(nproc)

BUILD_RELEASE_DIR=$PWD/cmake-build-release-cfsm-on \
DB_BENCH=$PWD/cmake-build-release-cfsm-on/db_bench \
ZENFS_TOOL=$PWD/cmake-build-release-cfsm-on/zenfs \
CONFIRM_MKFS=1 ZBD=nvme0n1 \
experiments/M1/run_fragmentation_workload.sh
```

M1 can be considered complete when:

- `active_zones` and `empty_zones` are reported correctly.
- `top_rbd_zones` is populated.
- at least some zones show non-zero `zvdr_ema` under overwrite/compaction.
