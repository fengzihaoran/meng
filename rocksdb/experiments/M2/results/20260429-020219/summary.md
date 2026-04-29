# M2 budget compare summary

- ZBD: nvme0n1
- DB_PATH: rocksdbtest/dbbench
- ZENFS_AUX_PATH: /home/femu/mnt/zenfs_aux
- NUM: 5000000
- VALUE_SIZE: 1024
- BENCHMARKS: fillrandom,overwrite,overwrite,stats
- ZENFS_ENABLE_GC: 1
- COMPARE_RUNS: 3

## Per-run results

| variant | budget | run | benchmark | micros/op | ops/sec | seconds | MB/s | log dir |
|---|---:|---:|---|---:|---:|---:|---:|---|
| budget_on | 1 | 1 | fillrandom | 8.401 | 119035 | 42.004 | 118.1 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 12.565 | 79587 | 62.824 | 78.9 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 14.020 | 71328 | 70.098 | 70.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_1` |
| budget_on | 1 | 2 | fillrandom | 9.042 | 110590 | 45.212 | 109.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_2` |
| budget_on | 1 | 2 | overwrite | 14.063 | 71110 | 70.313 | 70.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_2` |
| budget_on | 1 | 2 | overwrite | 9.575 | 104432 | 47.878 | 103.6 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_2` |
| budget_on | 1 | 3 | fillrandom | 7.247 | 137991 | 36.234 | 136.9 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_3` |
| budget_on | 1 | 3 | overwrite | 11.994 | 83377 | 59.968 | 82.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_3` |
| budget_on | 1 | 3 | overwrite | 16.073 | 62216 | 80.365 | 61.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_on/run_3` |
| budget_off | 0 | 1 | fillrandom | 6.575 | 152088 | 32.876 | 150.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_1` |
| budget_off | 0 | 1 | overwrite | 11.991 | 83394 | 59.956 | 82.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_1` |
| budget_off | 0 | 1 | overwrite | 14.991 | 66705 | 74.956 | 66.2 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_1` |
| budget_off | 0 | 2 | fillrandom | 8.033 | 124488 | 40.164 | 123.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_2` |
| budget_off | 0 | 2 | overwrite | 12.205 | 81933 | 61.025 | 81.3 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_2` |
| budget_off | 0 | 2 | overwrite | 12.096 | 82670 | 60.481 | 82.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_2` |
| budget_off | 0 | 3 | fillrandom | 8.401 | 119034 | 42.005 | 118.1 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_3` |
| budget_off | 0 | 3 | overwrite | 11.839 | 84463 | 59.197 | 83.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_3` |
| budget_off | 0 | 3 | overwrite | 11.691 | 85539 | 58.453 | 84.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-020219/budget_off/run_3` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 3 | 8.230 | 122539 | 121.567 |
| budget_off | fillrandom | 3 | 7.670 | 131870 | 130.800 |
| budget_on | overwrite | 6 | 13.048 | 78675 | 78.017 |
| budget_off | overwrite | 6 | 12.469 | 80784 | 80.133 |
