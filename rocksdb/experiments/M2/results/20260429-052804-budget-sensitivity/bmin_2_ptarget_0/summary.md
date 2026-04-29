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
| budget_on | 1 | 1 | fillrandom | 6.736 | 148453 | 33.681 | 147.2 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 8.894 | 112429 | 44.472 | 111.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 9.814 | 101893 | 49.071 | 101.1 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 6.736 | 148453 | 147.200 |
| budget_on | overwrite | 2 | 9.354 | 107161 | 106.300 |
