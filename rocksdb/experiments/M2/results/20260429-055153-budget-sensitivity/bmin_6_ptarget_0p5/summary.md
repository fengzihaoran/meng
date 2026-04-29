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
| budget_on | 1 | 1 | fillrandom | 8.496 | 117700 | 42.481 | 116.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 9.058 | 110401 | 45.289 | 109.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 10.036 | 99645 | 50.178 | 98.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 1 | 8.496 | 117700 | 116.700 |
| budget_on | overwrite | 2 | 9.547 | 105023 | 104.150 |
