# M1 fragmentation workload analysis

Result directory: `experiments/M1/results/20260428-131217`

## Workload

- Benchmarks: `fillrandom,overwrite,overwrite,compact,stats`
- Entries: `1,000,000`
- Value size: `1024`
- Compression: `none`
- ZenFS GC: disabled

## Main files to inspect

- `db_bench_config.txt`: effective script parameters.
- `fragmentation_workload_config.txt`: fragmentation workload name and scale.
- `db_bench.log`: RocksDB throughput, compaction work, key drops, and stalls.
- `faco_cfsm_summary.txt`: compact CFSM state summary.
- `faco_cfsm_zones.csv`: per-zone CFSM state table.
- `faco_cfsm_export.log`: confirms CFSM export files were copied from aux path.
- `zenfs_list.log`: confirms the ZenFS DB directory exists after the run.

## db_bench result

- `fillrandom`: `149791 ops/sec`, `148.6 MB/s`
- first `overwrite`: `164050 ops/sec`, `162.7 MB/s`
- second `overwrite`: `133433 ops/sec`, `132.3 MB/s`
- `compact`: `8.563 sec`

RocksDB stats:

- cumulative writes: `3000K`
- cumulative flush: `2.858 GB`
- cumulative compaction write: `9.55 GB`
- cumulative compaction read: `8.62 GB`
- write stalls: `0`
- compaction key drops: about `1.958M`

This workload is sufficient to create LSM churn and stale data for CFSM state
observation.

## CFSM summary

- zone capacity: `268435456` bytes
- total zones: `200`
- active zones: `18`
- empty zones: `182`
- total valid bytes: `1,002,509,764`
- average active-zone valid bytes: about `55.69 MB`
- average active-zone RBD: about `0.475512`
- max RBD: `0.599984`
- cold high zones: `18`
- hot high zones: `0`

The active/empty split is now correct. Empty zones are no longer counted as high
fragmentation victim candidates.

## Top RBD victim candidates

| rank | zone | valid bytes | valid ratio | RBD | ZVDR |
|---:|---:|---:|---:|---:|---:|
| 1 | 4 | 7317 | 0.000027 | 0.599984 | 0 |
| 2 | 3 | 22670 | 0.000084 | 0.599949 | 1.4013e-45 |
| 3 | 41 | 25114835 | 0.093560 | 0.543864 | 0 |
| 4 | 15 | 33301666 | 0.124058 | 0.525565 | 0 |
| 5 | 37 | 67433117 | 0.251208 | 0.449275 | 0 |

Zones `4` and `3` are the strongest victim candidates because they retain only
a tiny amount of live data inside a 256 MB zone.

## Interpretation

RBD and top victim zones are usable for M2. They identify sparse live-data zones
that are strong candidates for migration or garbage collection.

ZVDR is still not practically useful in this result. The only non-zero value is
`1.4013e-45`, which is denormal float noise and should be treated as zero. A
follow-up M1 patch has raised the hot threshold and added a minimum sampling
window, so future runs should avoid classifying noise as hot.

## M1 status

M1 is sufficient to hand off to M2 using RBD and top victim zones as stable
signals. ZVDR should be treated as an optional trend signal and rechecked after
the latest patch.
