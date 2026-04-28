# M1 CFSM compare summary

- ZBD: nvme0n1
- DB_PATH: rocksdbtest/dbbench
- ZENFS_AUX_PATH: /home/femu/mnt/zenfs_aux
- NUM: 1000000
- VALUE_SIZE: 1024
- COMPRESSION_TYPE: none
- COMPARE_RUNS: 10

## Per-run results

| variant | CFSM | run | micros/op | ops/sec | seconds | operations | MB/s | log dir |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| cfsm_on | 1 | 1 | 8.340 | 119906 | 8.340 | 1000000 | 118.9 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_1` |
| cfsm_on | 1 | 2 | 6.505 | 153730 | 6.505 | 1000000 | 152.5 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_2` |
| cfsm_on | 1 | 3 | 6.934 | 144217 | 6.934 | 1000000 | 143.0 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_3` |
| cfsm_on | 1 | 4 | 6.926 | 144392 | 6.926 | 1000000 | 143.2 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_4` |
| cfsm_on | 1 | 5 | 7.798 | 128239 | 7.798 | 1000000 | 127.2 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_5` |
| cfsm_on | 1 | 6 | 7.153 | 139792 | 7.153 | 1000000 | 138.6 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_6` |
| cfsm_on | 1 | 7 | 7.602 | 131541 | 7.602 | 1000000 | 130.5 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_7` |
| cfsm_on | 1 | 8 | 6.856 | 145851 | 6.856 | 1000000 | 144.7 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_8` |
| cfsm_on | 1 | 9 | 6.695 | 149352 | 6.696 | 1000000 | 148.1 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_9` |
| cfsm_on | 1 | 10 | 6.771 | 147690 | 6.771 | 1000000 | 146.5 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_on/run_10` |
| cfsm_off | 0 | 1 | 6.554 | 152584 | 6.554 | 1000000 | 151.3 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_1` |
| cfsm_off | 0 | 2 | 7.661 | 130527 | 7.661 | 1000000 | 129.5 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_2` |
| cfsm_off | 0 | 3 | 7.415 | 134867 | 7.415 | 1000000 | 133.8 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_3` |
| cfsm_off | 0 | 4 | 7.276 | 137431 | 7.276 | 1000000 | 136.3 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_4` |
| cfsm_off | 0 | 5 | 7.216 | 138570 | 7.217 | 1000000 | 137.4 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_5` |
| cfsm_off | 0 | 6 | 7.118 | 140488 | 7.118 | 1000000 | 139.3 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_6` |
| cfsm_off | 0 | 7 | 7.629 | 131083 | 7.629 | 1000000 | 130.0 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_7` |
| cfsm_off | 0 | 8 | 6.515 | 153486 | 6.515 | 1000000 | 152.2 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_8` |
| cfsm_off | 0 | 9 | 7.217 | 138555 | 7.217 | 1000000 | 137.4 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_9` |
| cfsm_off | 0 | 10 | 6.159 | 162355 | 6.159 | 1000000 | 161.0 | `/home/femu/rocksdb/experiments/M1/results/20260428-123529/cfsm_off/run_10` |

## Averages

| variant | runs | avg micros/op | avg ops/sec | avg MB/s |
|---|---:|---:|---:|---:|
| cfsm_on | 10 | 7.158 | 140471 | 139.320 |
| cfsm_off | 10 | 7.076 | 141995 | 140.820 |
