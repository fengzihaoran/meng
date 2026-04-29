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
| budget_on | 1 | 1 | fillrandom | 8.693 | 115038 | 43.464 | 114.1 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 13.585 | 73612 | 67.924 | 73.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 10.911 | 91654 | 54.553 | 90.9 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 8.693 | 115038 | 114.100 |
| budget_on | overwrite | 2 | 12.248 | 82633 | 81.950 |
