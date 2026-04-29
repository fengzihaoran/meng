# M2 budget compare summary

- ZBD: nvme0n1
- DB_PATH: rocksdbtest/dbbench
- ZENFS_AUX_PATH: /home/femu/mnt/zenfs_aux
- NUM: 5000000
- VALUE_SIZE: 1024
- BENCHMARKS: fillrandom,overwrite,overwrite,stats
- ZENFS_ENABLE_GC: 1
- COMPARE_RUNS: 1

## Per-run results

| variant | budget | run | benchmark | micros/op | ops/sec | seconds | MB/s | log dir |
|---|---:|---:|---|---:|---:|---:|---:|---|
| budget_on | 1 | 1 | fillrandom | 7.114 | 140561 | 35.572 | 139.4 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0p5/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 10.070 | 99300 | 50.352 | 98.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0p5/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 11.076 | 90287 | 55.379 | 89.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0p5/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 7.114 | 140561 | 139.400 |
| budget_on | overwrite | 2 | 10.573 | 94794 | 94.000 |
