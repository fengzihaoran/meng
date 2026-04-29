# M2 wrap-up summary

- result root: `experiments\M2\results\20260429-020219`
- throughput rows: `18`
- run metric rows: `6`
- budget trace rows: `3`

## Throughput

| variant | benchmark | rows | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---|---|---|---|
| budget_off | fillrandom | 3 | 7.670 | 131870 | 130.800 |
| budget_off | overwrite | 6 | 12.469 | 80784 | 80.133 |
| budget_on | fillrandom | 3 | 8.230 | 122539 | 121.567 |
| budget_on | overwrite | 6 | 13.048 | 78675 | 78.017 |

## Fragmentation And Runtime Metrics

| variant | runs | avg active zones | avg empty zones | avg high-frag zones | avg resets | avg finishes | avg GC GB |
|---|---|---|---|---|---|---|---|
| budget_off | 3 | 96.00 | 104.00 | 84.00 | NA | NA | NA |
| budget_on | 3 | 42.33 | 157.67 | 16.67 | NA | NA | NA |

## Budget Traces

| run dir | samples | min budget | max budget | last budget | avg p_frag | max p_frag |
|---|---|---|---|---|---|---|
| experiments\M2\results\20260429-020219\budget_on\run_1 | 18 | 2 | 12 | 2 | 0.526793 | 0.555134 |
| experiments\M2\results\20260429-020219\budget_on\run_2 | 17 | 3 | 12 | 3 | 0.515638 | 0.551590 |
| experiments\M2\results\20260429-020219\budget_on\run_3 | 18 | 2 | 12 | 2 | 0.527027 | 0.562305 |

## Notes

- `NA` means the run was produced before `faco_runtime_metrics.txt` export was available.
- M2 is expected to reduce active/high-fragment zones; throughput is a guardrail, not the only target.
