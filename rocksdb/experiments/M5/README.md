# M5 FACO Integration, Ablation, And Benchmark Protocol

M5 adds the final experiment surface for FACO-LACR:

- `plugin/zenfs/fs/faco_config.{h,cc}` loads fixed-schema `faco.json`
  parameters without adding a third-party JSON dependency.
- `plugin/zenfs/fs/faco_metrics.{h,cc}` exports FACO-local metrics as text,
  JSON, and Prometheus-style files in the ZenFS aux directory.
- `experiments/M5/run_exp*.sh` defines the benchmark protocol and refuses to
  run device benchmarks unless `CONFIRM_DEVICE_BENCH=1` is set.
- `experiments/M5/summarize_traces.py` aggregates copied FACO traces offline.

## Important Boundary

The original M5 task asks for RocksDB `Statistics::getTickerCount` integration.
That would require adding RocksDB core ticker/histogram enum values outside
`plugin/zenfs`, which violates the FACO project red line. M5 therefore exports
ZenFS-local FACO metric files:

- `faco_metrics.txt`
- `faco_metrics.json`
- `faco_metrics.prom`

Scripts consume these files directly.

## Current M4 Reality

M1-M4 are treated as alpha-frozen. The present M4 code exposes compaction-event
coordination traces through `FacoLacrState`, but it does not expose LACR L2
lifetime-prediction counters such as natural aging resets or prediction error.
`summarize_traces.py` reports those fields as `NA`; do not claim those KPI are
measured until M4 exposes them.

## Evaluation Claim Boundary

The paper-safe claim is narrow:

FACO is a fragmentation-aware control stack that controls zone reset pressure
and high-frag zones against matched FACO-internal baselines, while keeping
write amplification near 1 and preserving throughput within the benchmark
guardrail.

Do not claim that `full_faco` universally outperforms `native` or every
ablation. Native does not emit FACO-local fragmentation counters, so missing
FACO metrics must remain `NA`, not zero. Also do not claim LACR L2
natural-aging or lifetime-prediction improvements until M4 exposes those
counters.

If a paper-profile run still shows `without_lacr` lower than `full_faco` on
reset/high-frag pressure, the paper claim must be narrowed to the specific
submodules that improve the metric; do not force a full-stack dominance story.

Use the experiments this way:

| Experiment | Paper role |
|---|---|
| Exp-1 | performance guardrail for full FACO |
| Exp-2 | fragmentation evolution trace evidence |
| Exp-3 | LACR on/off movement behavior |
| Exp-4 | FACO-internal ablation and primary claim table |

`summarize_traces.py` writes four paper-table inputs:

- `m5_runtime_eval.csv`: grouped by experiment and variant, with mean/std/CI95.
- `m5_db_bench_eval.csv`: grouped by experiment, variant, and benchmark, with mean/std/CI95.
- `m5_baseline_comparison.csv`: baseline-normalized comparison rows.
- `m5_ablation_comparison.csv`: `full_faco` against each Exp-4 ablation.
- `m5_high_frag_diagnostics.csv`: per-run final high-frag zones joined with
  reorg victim traces.
- `m5_high_frag_summary.csv`: experiment/variant rollup of high-frag coverage
  diagnostics.
- `m5_reorg_candidate_rows.csv`: candidate-level top-k reorg diagnostics.
- `m5_reorg_candidate_summary.csv`: experiment/variant rollup of candidate
  selection diagnostics.

Do not use the raw `full_faco` aggregate across experiments for paper tables;
it mixes different comparison questions.

Baseline comparisons are intentionally split:

- `native` is a performance baseline only. Use it for db_bench throughput and
  latency metrics.
- `cfsm_only` is the fragmentation-observable baseline. Use it for
  `zone_reset_count`, `high_frag_zones`, and FACO-local fragmentation metrics.

Read `improvement_pct` directionally: positive means `full_faco` moves the
metric in the desired direction, negative means it regresses. If
`full_faco vs without_lacr` is negative on reset/high-frag pressure, do not
claim LACR improves those metrics.

## Offline Checks

These commands do not touch a ZNS device:

```bash
bash -n experiments/M5/run_exp_common.sh
bash -n experiments/M5/run_exp1.sh
bash -n experiments/M5/run_exp2.sh
bash -n experiments/M5/run_exp3.sh
bash -n experiments/M5/run_exp4.sh
bash -n experiments/M5/run_all.sh
python3 experiments/M5/summarize_traces.py experiments/M3/results/<run> /tmp/m5_summary.md
```

For the current M5 evaluation flow, prefer the bundled validator:

```bash
bash experiments/M5/validate_eval_flow.sh offline \
  experiments/M5/results/20260510-060423-run-all \
  /tmp/faco_m5_eval_flow_check
```

It verifies shell syntax, regenerates an offline summary, and checks that:

- `native` runtime FACO-local metrics stay `not_comparable`.
- `cfsm_only` is used for fragmentation baseline comparisons.
- `without_lacr` ablation rows are present and not averaged away.
- `full_faco` rows keep their experiment identity.
- high-frag diagnostic artifacts are present for the Exp-4 FACO variants.

## Device Protocol

Only run this after scripts and summaries are complete:

```bash
CONFIRM_DEVICE_BENCH=1 \
ZBD=nvme0n1 \
ZENFS_AUX_PATH=/home/femu/mnt/zenfs_aux \
DB_PATH=rocksdbtest/dbbench \
M5_PROFILE=smoke \
bash experiments/M5/run_all.sh
```

`M5_PROFILE` controls the default workload size:

| Profile | Runs | NUM | Default intent |
|---|---:|---:|---|
| `smoke` | 1 | 100000 | quick device sanity; no `stats` or `readwhilewriting` by default |
| `dev` | 1 | 1000000 | development-scale protocol; no `stats` or `readrandom` by default |
| `paper` | 5 | 5000000 | full long protocol |

Use `M5_PROFILE=paper` only for final unattended runs. A paper-profile Exp-1
run writes several GiB per fill/overwrite phase and `readwhilewriting` can run
for more than an hour on FEMU.

Run read-heavy probes explicitly after the write/trace path is stable:

```bash
CONFIRM_DEVICE_BENCH=1 \
ZBD=nvme0n1 \
M5_PROFILE=dev \
M5_EXP1_BENCHMARKS=fillseq,fillrandom,overwrite,readrandom,stats \
bash experiments/M5/run_exp1.sh
```

Run `stats` explicitly only when debugging RocksDB property output. M5 summary
uses db_bench operation lines and FACO aux files, so the `stats` benchmark is
not required for FACO trace analysis.

`run_all.sh` executes:

1. `run_exp1.sh`: single-tenant performance guardrail.
2. `run_exp2.sh`: fragmentation evolution traces.
3. `run_exp3.sh`: LACR movement breakdown through the M4 compare wrapper.
4. `run_exp4.sh`: FACO-internal ablation matrix.
5. `summarize_traces.py`: final CSV and Markdown summaries.

After a device run, validate the generated result directory:

```bash
RESULT=experiments/M5/results/<new-run-all-dir>
bash experiments/M5/validate_eval_flow.sh check-result "${RESULT}"
bash experiments/M5/validate_eval_flow.sh check-dev-gates "${RESULT}"
```

`check-result` only verifies artifact structure and comparison protocol.
`check-dev-gates` is the stricter pre-paper gate: `full_faco` must improve
Exp-4 reset and high-frag metrics versus `cfsm_only`, keep WA below `1.01`,
reach at least two candidate evaluations per run, accept at least two reorg
plans per run, keep high-frag selected/accepted percentages near 100%, and
raise final high-frag coverage above the configured threshold. Override gate
thresholds with `M5_GATE_MIN_EVALS`, `M5_GATE_MIN_WA_MAX`,
`M5_GATE_MIN_FINAL_COVERAGE_PCT`, `M5_GATE_MIN_HIGH_FRAG_SELECTED_PCT`, and
`M5_GATE_MIN_HIGH_FRAG_ACCEPTED_PCT`.

Print, but do not run, the next device commands with:

```bash
bash experiments/M5/validate_eval_flow.sh print-dev-command
bash experiments/M5/validate_eval_flow.sh print-paper-command
```

Before changing reorg policy, inspect high-frag diagnostics:

```bash
RESULT=experiments/M5/results/<new-run-all-dir>
column -s, -t "${RESULT}/m5_high_frag_summary.csv" | less -S
column -s, -t "${RESULT}/m5_reorg_candidate_summary.csv" | less -S
grep '^exp4,full_faco' "${RESULT}/m5_high_frag_diagnostics.csv"
grep '^exp4,without_lacr' "${RESULT}/m5_high_frag_diagnostics.csv"
grep '^exp4,full_faco' "${RESULT}/m5_reorg_candidate_rows.csv" | head -40
```

If `accepted_reorg_samples` is zero or
`accepted_final_high_frag_coverage_pct` is low while `high_frag_zone_count` is
high, use `faco_reorg_candidates.csv` and `m5_reorg_candidate_rows.csv` to check
which top-k candidates were filtered, selected, penalized by LACR, or accepted.
Fix candidate selection or LACR gating before running paper experiments.

When dev workloads are too short to trigger stable reorg cadence, set
`FACO_GC_INTERVAL_US` explicitly. The default ZenFS GC worker interval remains
10 seconds; M5 diagnosis commonly uses 5 seconds first, then 3 seconds if the
candidate evaluation count is still unstable:

```bash
CONFIRM_DEVICE_BENCH=1 \
ZBD=nvme0n1 \
M5_PROFILE=dev \
M5_RUNS=3 \
FACO_GC_INTERVAL_US=5000000 \
M5_EXP4_BENCHMARKS=fillrandom,overwrite,overwrite,overwrite \
bash experiments/M5/run_exp4.sh
```

If 5s keeps write amplification and throughput stable but misses final
high-frag coverage, test a bounded backlog trigger rather than shortening the
whole GC interval:

```bash
CONFIRM_DEVICE_BENCH=1 \
ZBD=nvme0n1 \
M5_PROFILE=dev \
M5_RUNS=3 \
FACO_GC_INTERVAL_US=5000000 \
FACO_HIGH_FRAG_BACKLOG_TRIGGER=20 \
FACO_REORG_EXTRA_PER_GC=1 \
FACO_REORG_MAX_EXTRA_VALID_MB=64 \
M5_EXP4_BENCHMARKS=fillrandom,overwrite,overwrite,overwrite \
bash experiments/M5/run_exp4.sh
```

`FACO_HIGH_FRAG_BACKLOG_TRIGGER` enables the extra pass only when the current
CFSM high-frag backlog is at or above the threshold. `FACO_REORG_EXTRA_PER_GC`
caps extra accepted plans per GC wakeup, and `FACO_REORG_MAX_EXTRA_VALID_MB`
caps the added estimated live-data migration. Leave these unset for the
alpha-frozen baseline behavior.

## Ablation Matrix

`run_exp4.sh` currently covers FACO-internal variants:

| Variant | Compile Gates | Runtime LACR |
|---|---|---:|
| `native` | CFSM=0, Budget=0, Reorg=0, LACR=0 | 0 |
| `cfsm_only` | CFSM=1, Budget=0, Reorg=0, LACR=0 | 0 |
| `without_ebcr` | CFSM=1, Budget=0, Reorg=0, LACR=1 | 1 |
| `without_lacr` | CFSM=1, Budget=1, Reorg=1, LACR=1 | 0 |
| `full_faco` | CFSM=1, Budget=1, Reorg=1, LACR=1 | 1 |

CAZA and static lifetime-hint baselines are not implemented by these scripts;
they need separate baseline implementations before they can be claimed.
