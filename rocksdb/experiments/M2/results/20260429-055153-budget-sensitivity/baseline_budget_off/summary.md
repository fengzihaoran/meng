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
| budget_off | 0 | 1 | fillrandom | 6.703 | 149183 | 33.516 | 148.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/baseline_budget_off/budget_off/run_1` |
| budget_off | 0 | 1 | overwrite | 10.787 | 92700 | 53.937 | 91.9 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/baseline_budget_off/budget_off/run_1` |
| budget_off | 0 | 1 | overwrite | 9.125 | 109591 | 45.624 | 108.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/baseline_budget_off/budget_off/run_1` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_off | fillrandom | 1 | 6.703 | 149183 | 148.000 |
| budget_off | overwrite | 2 | 9.956 | 101146 | 100.300 |
