# M1 fragmentation workload analysis

Result directory: `experiments/M1/results/20260428-125216`

## Workload

- Benchmarks: `fillrandom,overwrite,overwrite,compact,stats`
- Entries: `1,000,000`
- Value size: `1024`
- Compression: `none`
- ZenFS GC: disabled

## db_bench result

- `fillrandom`: `161040 ops/sec`, `159.7 MB/s`
- first `overwrite`: `174021 ops/sec`, `172.6 MB/s`
- second `overwrite`: `218172 ops/sec`, `216.4 MB/s`
- `compact`: `12.521 sec`

The RocksDB stats show this workload is useful for M1 fragmentation observation:

- cumulative writes: `3000K`
- cumulative flush: `2.858 GB`
- cumulative compaction write: `8.29 GB`
- cumulative compaction read: `7.35 GB`
- key drops during compaction: about `1.96M`

## CFSM export result

- total zones: `200`
- active zones: `19`
- total valid bytes: `1,002,211,200`
- max RBD: `0.599995`
- average active-zone RBD: about `0.482099`
- top RBD zones include very sparse zones such as:
  - zone `44`: `valid_bytes=2064`, `rbd=0.599995`
  - zone `4`: `valid_bytes=7317`, `rbd=0.599984`
  - zone `3`: `valid_bytes=18611`, `rbd=0.599958`

This confirms that CFSM is now exporting zone-level fragmentation state, not only throughput data.

## Issues Found

This run also exposed two M1 observation issues:

1. Empty zones were counted as `COLD_HIGH`.
   - The old summary showed `class_cold_high=200`.
   - Empty zones should not be treated as fragmented victim candidates.
   - Fixed after this run: empty zones now stay `COLD_LOW`, and summary reports `empty_zones` separately.

2. `zvdr_ema` stayed `0`.
   - The workload did create invalidation through overwrite and compaction.
   - But CFSM had no initial `Tick()` before writes, so the final shutdown `Tick()` only initialized history.
   - Fixed after this run: CFSM now establishes an initial time reference when the zoned block device opens.

## Next Step

Rebuild and rerun the fragmentation workload after the fixes:

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

Expected improvement in the next result:

- `empty_zones` should be reported separately.
- `class_cold_high` should describe only active fragmented zones.
- some zones should have non-zero `zvdr_ema` after overwrite/compaction.
