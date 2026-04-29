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
| budget_on | 1 | 1 | fillrandom | 6.244 | 160148 | 31.221 | 158.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 9.488 | 105398 | 47.439 | 104.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 11.269 | 88741 | 56.343 | 88.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 6.244 | 160148 | 158.800 |
| budget_on | overwrite | 2 | 10.378 | 97070 | 96.250 |
