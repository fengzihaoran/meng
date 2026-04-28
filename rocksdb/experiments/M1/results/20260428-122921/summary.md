# M1 CFSM compare summary

- ZBD: nvme0n1
- DB_PATH: rocksdbtest/dbbench
- ZENFS_AUX_PATH: /home/femu/mnt/zenfs_aux
- NUM: 1000000
- VALUE_SIZE: 1024
- COMPRESSION_TYPE: none
- COMPARE_RUNS: 3

## Per-run results

| variant | CFSM | run | micros/op | ops/sec | seconds | operations | MB/s | log dir |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| cfsm_on | 1 | 1 | 7.566 | 132164 | 7.566 | 1000000 | 131.1 | `/home/femu/rocksdb/experiments/M1/results/20260428-122921/cfsm_on/run_1` |
| cfsm_on | 1 | 2 | 7.636 | 130960 | 7.636 | 1000000 | 129.9 | `/home/femu/rocksdb/experiments/M1/results/20260428-122921/cfsm_on/run_2` |
| cfsm_on | 1 | 3 | 5.919 | 168954 | 5.919 | 1000000 | 167.6 | `/home/femu/rocksdb/experiments/M1/results/20260428-122921/cfsm_on/run_3` |
| cfsm_off | 0 | 1 | 6.142 | 162815 | 6.142 | 1000000 | 161.5 | `/home/femu/rocksdb/experiments/M1/results/20260428-122921/cfsm_off/run_1` |
| cfsm_off | 0 | 2 | 6.453 | 154975 | 6.453 | 1000000 | 153.7 | `/home/femu/rocksdb/experiments/M1/results/20260428-122921/cfsm_off/run_2` |
| cfsm_off | 0 | 3 | 6.702 | 149203 | 6.702 | 1000000 | 148.0 | `/home/femu/rocksdb/experiments/M1/results/20260428-122921/cfsm_off/run_3` |

## Averages

| variant | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---:|---:|---:|---:|
| cfsm_on | 3 | 7.040 | 144026 | 142.867 |
| cfsm_off | 3 | 6.432 | 155664 | 154.400 |
