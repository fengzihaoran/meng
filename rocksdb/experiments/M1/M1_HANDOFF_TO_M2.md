# M1 to M2 handoff

Use this file when starting a new thread for M2. The M1 thread implemented and
validated the CFSM observation layer in ZenFS.

## Current scope

- Repository: `D:/yuej/meng/rocksdb`
- Linux path used in FEMU: `/home/femu/rocksdb`
- M1 scripts: `experiments/M1`
- Main CFSM code:
  - `plugin/zenfs/fs/frag_state_table.h`
  - `plugin/zenfs/fs/frag_state_table.cc`
  - `plugin/zenfs/fs/zbd_zenfs.cc`
  - `plugin/zenfs/fs/fs_zenfs.cc`
  - `plugin/zenfs/fs/io_zenfs.cc`
- Unit test:
  - `plugin/zenfs/tests/frag_state_table_test.cc`

## M1 model signals

- `valid_bytes`: live bytes tracked per zone from append/delete/reset hooks.
- `invalid_bytes`: `zone_capacity_bytes - valid_bytes`.
- `ZVDR`: zone validity decay rate.
  - instantaneous form: `decay_bytes / (elapsed_us * zone_capacity_bytes)`
  - smoothed form: `ema_alpha * instant + (1 - ema_alpha) * old_ema`
  - default `ema_alpha` is `0.3`
- `RBD`: reclaim benefit density.
  - raw density: `invalid_bytes / (valid_bytes + 1)`
  - normalized density: `raw / (1 + raw)`
  - score: `0.6 * normalized_density + 0.4 * ZVDR`
  - empty zones return `0` so they are not selected as GC victims.
- `FFD`: file fragmentation density.
  - for a file's zone extents: sum of `(1 - zone_valid_ratio) * extent_bytes / file_bytes`
- `fragment_class`: derived class using invalid ratio and ZVDR:
  - `COLD_LOW`
  - `COLD_HIGH`
  - `HOT_LOW`
  - `HOT_HIGH`
- `top victim zones`: zones sorted by RBD descending. This is a ranking output,
  not an independent model.

## Important experiment results

10-run on/off comparison:

- result dir: `experiments/M1/results/20260428-123529`
- `cfsm_on`: `140471 ops/sec`
- `cfsm_off`: `141995 ops/sec`
- overhead: about `1.07%`

Fragmentation workload:

- result dir: `experiments/M1/results/20260428-130502`
- workload: `fillrandom,overwrite,overwrite,compact,stats`
- active zones: `17`
- empty zones: `183`
- total valid bytes: `1,002,535,689`
- max RBD: `0.599984`
- top RBD zones identify sparse live-data zones, e.g. zone `4` has only `7317`
  valid bytes in a `256MB` zone.

## Latest M1 note

After `20260428-130502`, one final ZVDR stability patch was applied:

- ZVDR Tick now uses a minimum sampling window when callbacks happen inside the
  same microsecond.
- The hot threshold ignores denormal floating-point noise.

Recommended final M1 check:

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

## M2 starting point

M2 should use M1 signals instead of rebuilding the observation layer.

Recommended M2 direction:

1. Consume `RBD` and `top victim zones` first because they are already stable.
2. Add a policy layer that uses these signals for GC or migration decisions.
3. Treat `ZVDR` as a trend signal after the final M1 check confirms non-noise
   values under overwrite/compaction.
4. Keep M2 code separate from M1 state maintenance; M1 should remain the
   observation model.
