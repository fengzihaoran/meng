# M2 wrap-up summary

- result root: `/home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity`
- throughput rows: `12`
- run metric rows: `4`
- budget trace rows: `4`

## Throughput

| scenario | variant | benchmark | rows | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---|---|---|---|---|
| bmin_2_ptarget_0 | budget_on | fillrandom | 1 | 6.736 | 148453 | 147.200 |
| bmin_2_ptarget_0 | budget_on | overwrite | 2 | 9.354 | 107161 | 106.300 |
| bmin_2_ptarget_0p5 | budget_on | fillrandom | 1 | 6.415 | 155886 | 154.600 |
| bmin_2_ptarget_0p5 | budget_on | overwrite | 2 | 9.404 | 107883 | 107.000 |
| bmin_6_ptarget_0 | budget_on | fillrandom | 1 | 7.569 | 132123 | 131.000 |
| bmin_6_ptarget_0 | budget_on | overwrite | 2 | 9.014 | 111105 | 110.200 |
| bmin_6_ptarget_0p5 | budget_on | fillrandom | 1 | 6.244 | 160148 | 158.800 |
| bmin_6_ptarget_0p5 | budget_on | overwrite | 2 | 10.378 | 97070 | 96.250 |

## Fragmentation And Runtime Metrics

| scenario | variant | runs | avg active zones | avg empty zones | avg high-frag zones | avg resets | avg finishes | avg GC GB |
|---|---|---|---|---|---|---|---|---|
| bmin_2_ptarget_0 | budget_on | 1 | 48.00 | 152.00 | 21.00 | 278.00 | 10.00 | 0.000 |
| bmin_2_ptarget_0p5 | budget_on | 1 | 43.00 | 157.00 | 17.00 | 278.00 | 9.00 | 0.000 |
| bmin_6_ptarget_0 | budget_on | 1 | 50.00 | 150.00 | 27.00 | 272.00 | 10.00 | 0.000 |
| bmin_6_ptarget_0p5 | budget_on | 1 | 54.00 | 146.00 | 27.00 | 264.00 | 10.00 | 0.000 |

## Budget Traces

| run dir | scenario | samples | min budget | max budget | last budget | avg p_frag | max p_frag |
|---|---|---|---|---|---|---|---|
| /home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1 | bmin_2_ptarget_0 | 13 | 6 | 12 | 6 | 0.507782 | 0.550852 |
| /home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_2_ptarget_0p5/budget_on/run_1 | bmin_2_ptarget_0p5 | 13 | 5 | 12 | 5 | 0.530591 | 0.559969 |
| /home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1 | bmin_6_ptarget_0 | 13 | 6 | 12 | 6 | 0.512558 | 0.549963 |
| /home/femu/rocksdb/experiments/M2/results/20260429-052804-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1 | bmin_6_ptarget_0p5 | 14 | 5 | 12 | 5 | 0.522496 | 0.550159 |

## Notes

- `NA` means the run was produced before `faco_runtime_metrics.txt` export was available.
- M2 is expected to reduce active/high-fragment zones; throughput is a guardrail, not the only target.
