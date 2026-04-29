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
| budget_on | 1 | 1 | fillrandom | 8.551 | 116942 | 42.756 | 116.0 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 13.179 | 75879 | 65.894 | 75.3 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_1` |
| budget_on | 1 | 1 | overwrite | 15.170 | 65918 | 75.852 | 65.4 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_1` |
| budget_on | 1 | 2 | fillrandom | 9.046 | 110541 | 45.232 | 109.6 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_2` |
| budget_on | 1 | 2 | overwrite | 12.918 | 77414 | 64.588 | 76.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_2` |
| budget_on | 1 | 2 | overwrite | 13.934 | 71764 | 69.672 | 71.2 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_2` |
| budget_on | 1 | 3 | fillrandom | 8.353 | 119711 | 41.767 | 118.7 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_3` |
| budget_on | 1 | 3 | overwrite | 12.973 | 77083 | 64.865 | 76.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_3` |
| budget_on | 1 | 3 | overwrite | 14.146 | 70689 | 70.732 | 70.1 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_on/run_3` |
| budget_off | 0 | 1 | fillrandom | 9.224 | 108414 | 46.119 | 107.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_1` |
| budget_off | 0 | 1 | overwrite | 10.867 | 92020 | 54.336 | 91.3 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_1` |
| budget_off | 0 | 1 | overwrite | 12.795 | 78155 | 63.975 | 77.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_1` |
| budget_off | 0 | 2 | fillrandom | 8.494 | 117727 | 42.471 | 116.8 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_2` |
| budget_off | 0 | 2 | overwrite | 15.371 | 65056 | 76.856 | 64.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_2` |
| budget_off | 0 | 2 | overwrite | 12.902 | 77504 | 64.512 | 76.9 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_2` |
| budget_off | 0 | 3 | fillrandom | 8.535 | 117160 | 42.676 | 116.2 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_3` |
| budget_off | 0 | 3 | overwrite | 15.643 | 63925 | 78.216 | 63.4 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_3` |
| budget_off | 0 | 3 | overwrite | 16.133 | 61983 | 80.667 | 61.5 | `/home/femu/rocksdb/experiments/M2/results/20260429-025838/budget_off/run_3` |

## Averages

| variant | benchmark | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---:|---:|---:|---:|
| budget_on | fillrandom | 3 | 8.650 | 115731 | 114.767 |
| budget_off | fillrandom | 3 | 8.751 | 114434 | 113.500 |
| budget_on | overwrite | 6 | 13.720 | 73124 | 72.550 |
| budget_off | overwrite | 6 | 13.952 | 73107 | 72.517 |
