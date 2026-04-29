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
| budget_on | 1 | 1 | fillrandom | 7.569 | 132123 | 37.843 | 131.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 9.363 | 106800 | 46.816 | 105.9 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 8.665 | 115410 | 43.324 | 114.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 7.569 | 132123 | 131.000 |
| budget_on | overwrite | 2 | 9.014 | 111105 | 110.200 |
