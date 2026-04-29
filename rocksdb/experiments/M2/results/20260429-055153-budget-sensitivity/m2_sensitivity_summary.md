# M2 wrap-up summary

- result root: `/home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity`
- throughput rows: `15`
- run metric rows: `5`
- budget trace rows: `4`

## Throughput

| scenario | variant | benchmark | rows | avg micros/op | avg ops/sec | avg MB/s |
|---|---|---|---|---|---|---|
| baseline_budget_off | budget_off | fillrandom | 1 | 6.703 | 149183 | 148.000 |
| baseline_budget_off | budget_off | overwrite | 2 | 9.956 | 101146 | 100.300 |
| bmin_2_ptarget_0 | budget_on | fillrandom | 1 | 8.693 | 115038 | 114.100 |
| bmin_2_ptarget_0 | budget_on | overwrite | 2 | 12.248 | 82633 | 81.950 |
| bmin_2_ptarget_0p5 | budget_on | fillrandom | 1 | 7.114 | 140561 | 139.400 |
| bmin_2_ptarget_0p5 | budget_on | overwrite | 2 | 10.573 | 94794 | 94.000 |
| bmin_6_ptarget_0 | budget_on | fillrandom | 1 | 8.103 | 123408 | 122.400 |
| bmin_6_ptarget_0 | budget_on | overwrite | 2 | 10.430 | 96327 | 95.550 |
| bmin_6_ptarget_0p5 | budget_on | fillrandom | 1 | 8.496 | 117700 | 116.700 |
| bmin_6_ptarget_0p5 | budget_on | overwrite | 2 | 9.547 | 105023 | 104.150 |

## Fragmentation And Runtime Metrics

| scenario | variant | runs | avg active zones | avg empty zones | avg high-frag zones | avg resets | avg finishes | avg GC GB |
|---|---|---|---|---|---|---|---|---|
| baseline_budget_off | budget_off | 1 | 92.00 | 108.00 | 81.00 | 325.00 | 1.00 | 0.000 |
| bmin_2_ptarget_0 | budget_on | 1 | 48.00 | 152.00 | 24.00 | 275.00 | 16.00 | 0.000 |
| bmin_2_ptarget_0p5 | budget_on | 1 | 59.00 | 141.00 | 39.00 | 268.00 | 6.00 | 0.000 |
| bmin_6_ptarget_0 | budget_on | 1 | 43.00 | 157.00 | 15.00 | 270.00 | 5.00 | 0.000 |
| bmin_6_ptarget_0p5 | budget_on | 1 | 59.00 | 141.00 | 35.00 | 273.00 | 5.00 | 0.000 |

## Baseline Comparison

| scenario | benchmark | budget_on MB/s | budget_off MB/s | delta |
|---|---|---|---|---|
| bmin_2_ptarget_0 | fillrandom | 114.100 | 148.000 | -22.91% |
| bmin_2_ptarget_0 | overwrite | 81.950 | 100.300 | -18.30% |
| bmin_2_ptarget_0p5 | fillrandom | 139.400 | 148.000 | -5.81% |
| bmin_2_ptarget_0p5 | overwrite | 94.000 | 100.300 | -6.28% |
| bmin_6_ptarget_0 | fillrandom | 122.400 | 148.000 | -17.30% |
| bmin_6_ptarget_0 | overwrite | 95.550 | 100.300 | -4.74% |
| bmin_6_ptarget_0p5 | fillrandom | 116.700 | 148.000 | -21.15% |
| bmin_6_ptarget_0p5 | overwrite | 104.150 | 100.300 | 3.84% |

| scenario | active zones | active delta | high-frag zones | high-frag delta | resets | reset delta | finishes | finish delta |
|---|---|---|---|---|---|---|---|---|
| bmin_2_ptarget_0 | 48.00 | -47.83% | 24.00 | -70.37% | 275.00 | -15.38% | 16.00 | 1500.00% |
| bmin_2_ptarget_0p5 | 59.00 | -35.87% | 39.00 | -51.85% | 268.00 | -17.54% | 6.00 | 500.00% |
| bmin_6_ptarget_0 | 43.00 | -53.26% | 15.00 | -81.48% | 270.00 | -16.92% | 5.00 | 400.00% |
| bmin_6_ptarget_0p5 | 59.00 | -35.87% | 35.00 | -56.79% | 273.00 | -16.00% | 5.00 | 400.00% |

## Budget Traces

| run dir | scenario | samples | min budget | max budget | last budget | avg p_frag | max p_frag |
|---|---|---|---|---|---|---|---|
| /home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0/budget_on/run_1 | bmin_2_ptarget_0 | 17 | 3 | 12 | 3 | 0.534345 | 0.561897 |
| /home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_2_ptarget_0p5/budget_on/run_1 | bmin_2_ptarget_0p5 | 15 | 12 | 12 | 12 | 0.521263 | 0.554811 |
| /home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0/budget_on/run_1 | bmin_6_ptarget_0 | 15 | 6 | 12 | 6 | 0.515869 | 0.549419 |
| /home/femu/rocksdb/experiments/M2/results/20260429-055153-budget-sensitivity/bmin_6_ptarget_0p5/budget_on/run_1 | bmin_6_ptarget_0p5 | 14 | 12 | 12 | 12 | 0.526165 | 0.554105 |

## Notes

- `NA` means the run was produced before `faco_runtime_metrics.txt` export was available.
- M2 is expected to reduce active/high-fragment zones; throughput is a guardrail, not the only target.
