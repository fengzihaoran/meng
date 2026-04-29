# M3 EBCR Reorg Workflow

M3 implements FACO's ReorgPlanner (重组规划器), the second half of EBCR
(弹性预算与协同重组). It does not redo M1/M2:

- candidate source: M1 CFSM (跨层碎片状态模型) `RankVictimZones(top_k)`
- primary signal: RBD (回收收益密度) and `valid_bytes` (有效字节)
- budget input: M2 active-zone budget (主动区预算), used to scale
  `tau_trigger` (触发阈值)
- optional trend: ZVDR (有效占比衰减率) only inside `Net(z)`, not as the main
  decision signal

M3 replaces the legacy ZenFS invalid-ratio victim selection only when
`FACO_ENABLE_REORG=1`. Build with `FACO_ENABLE_REORG=0` to keep the original
GC fallback (回退路径).

## Files

- `run_reorg_tests.sh`: builds and runs `reorg_planner_test`.
- `run_reorg_workload.sh`: runs one ZNS/FEMU workload with CFSM, M2 budget, and
  M3 reorg enabled.
- `run_reorg_compare.sh`: builds `FACO_ENABLE_REORG=1/0` variants and compares
  M3 against the M2 fallback.
- `analyze_reorg_trace.py`: summarizes `faco_reorg_trace.csv` and renders
  `reorg_trace.svg`.
- `summarize_m3_results.py`: aggregates runtime counters, write amplification
  (写放大), and reorg decisions.
- `SPEC_QUESTIONS.md`: records the M3 ContentionPenalty (前台冲突惩罚)
  simplification that M4/M5 should revisit.
- `M3.sh`: runs unit tests by default; device workloads are opt-in.

## Typical Commands

Unit tests only:

```bash
bash experiments/M3/run_reorg_tests.sh
```

One M3 workload on FEMU/ZNS:

```bash
cmake --build cmake-build-release-reorg-on --target db_bench zenfs_tool -j$(nproc)

BUILD_RELEASE_DIR=$PWD/cmake-build-release-reorg-on \
DB_BENCH=$PWD/cmake-build-release-reorg-on/db_bench \
ZENFS_TOOL=$PWD/cmake-build-release-reorg-on/zenfs \
CONFIRM_MKFS=1 ZBD=nvme0n1 \
bash experiments/M3/run_reorg_workload.sh
```

M3 on/off comparison:

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 NUM=5000000 \
bash experiments/M3/run_reorg_compare.sh
```

Wrap up an existing result directory:

```bash
python3 experiments/M3/summarize_m3_results.py \
  experiments/M3/results/<result-dir> \
  experiments/M3/results/<result-dir>/m3_summary.md
```

## Default Parameters

M3 keeps the current M2 recommendation:

```text
FACO_BUDGET_B_MIN=6
FACO_BUDGET_P_TARGET=0
FACO_BUDGET_ZVDR_WEIGHT=0
```

Useful M3 knobs:

- `FACO_REORG_TOP_K`
- `FACO_REORG_TAU_TRIGGER_INIT`
- `FACO_REORG_TAU_MIN` / `FACO_REORG_TAU_MAX`
- `FACO_REORG_W1` / `FACO_REORG_W2` / `FACO_REORG_W3` / `FACO_REORG_W4`
- `FACO_REORG_WA_FACTOR`
- `FACO_REORG_T_HORIZON_US`
- `FACO_REORG_CONTENTION_PENALTY_BYTES`

Expected artifacts:

- New result directories use `YYYYMMDD-HHMMSS-purpose-vN`, for example
  `20260429-160000-reorg-compare-v1`.
- `faco_reorg_summary.txt`
- `faco_reorg_trace.csv`
- `reorg_trace_analysis.md`
- `reorg_trace.svg`
- `m3_summary.md`, `m3_run_metrics.csv`, and `m3_reorg_trace_summary.csv`
- CFSM and budget exports reused from M1/M2 workflows
