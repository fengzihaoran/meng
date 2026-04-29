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
| budget_on | 1 | 1 | fillrandom | 8.103 | 123408 | 40.516 | 122.4 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 9.721 | 102869 | 48.605 | 102.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 11.138 | 89785 | 55.688 | 89.1 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 8.103 | 123408 | 122.400 |
| budget_on | overwrite | 2 | 10.430 | 96327 | 95.550 |
