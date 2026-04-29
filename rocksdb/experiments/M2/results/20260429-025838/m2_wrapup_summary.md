# M2 wrap-up summary

- result root: `experiments\M2\results\20260429-025838`
- throughput rows: `18`
- run metric rows: `6`
- budget trace rows: `3`

## Throughput

| variant | benchmark | rows | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---|---|---|---|
| budget_off | fillrandom | 3 | 8.751 | 114434 | 113.500 |
| budget_off | overwrite | 6 | 13.952 | 73107 | 72.517 |
| budget_on | fillrandom | 3 | 8.650 | 115731 | 114.767 |
| budget_on | overwrite | 6 | 13.720 | 73124 | 72.550 |

## Fragmentation And Runtime Metrics

| variant | runs | avg active zones | avg empty zones | avg high-frag zones | avg resets | avg finishes | avg GC GB |
|---|---|---|---|---|---|---|---|
| budget_off | 3 | 95.00 | 105.00 | 83.00 | 291.67 | 1.00 | 0.000 |
| budget_on | 3 | 43.33 | 156.67 | 18.00 | 290.33 | 17.00 | 0.000 |

## Budget Traces

| run dir | samples | min budget | max budget | last budget | avg p_frag | max p_frag |
|---|---|---|---|---|---|---|
| experiments\M2\results\20260429-025838\budget_on\run_1 | 19 | 2 | 12 | 2 | 0.521622 | 0.558276 |
| experiments\M2\results\20260429-025838\budget_on\run_2 | 18 | 2 | 12 | 2 | 0.524253 | 0.566821 |
| experiments\M2\results\20260429-025838\budget_on\run_3 | 18 | 2 | 12 | 2 | 0.510277 | 0.557786 |

## Notes

- `NA` means the run was produced before `faco_runtime_metrics.txt` export was available.
- M2 is expected to reduce active/high-fragment zones; throughput is a guardrail, not the only target.
