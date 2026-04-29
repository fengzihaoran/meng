# M2 EBCR Budget Workflow

M2 implements FACO's elastic active-zone budget controller. It consumes M1's
CFSM outputs in-process through `FragmentationStateTable`:

- primary signal: RBD-ranked top victim zones
- supporting signal: per-zone `valid_bytes` through the RBD calculation
- optional trend: ZVDR only when `FACO_BUDGET_ZVDR_WEIGHT` is non-zero

M2 does not replace GC victim selection or data migration. Those are M3 scope.

## Files

- `run_budget_ctrl_tests.sh`: builds and runs `frag_state_table_test` and
  `zone_budget_ctrl_test`.
- `run_budget_workload.sh`: runs a ZenFS `db_bench` workload with GC enabled so
  the GC worker periodically calls the budget controller.
- `run_budget_compare.sh`: builds `FACO_ENABLE_BUDGET=1` and
  `FACO_ENABLE_BUDGET=0` release variants, runs the same workload, and writes
  `summary.csv` / `summary.md`.
- `analyze_budget_trace.py`: summarizes `faco_budget_trace.csv` and emits
  `budget_trace_analysis.md` plus `budget_trace.svg`.
- `summarize_m2_results.py`: aggregates throughput, CFSM, budget trace, and
  runtime reset/finish/GC metrics into one wrap-up report.
- `run_budget_sensitivity.sh`: runs a small `B_min` / `P_target` matrix and a
  budget-off baseline by default.
- `M2.sh`: runs unit tests by default; set `RUN_DEVICE_WORKLOAD=1` for the ZNS
  workload.

## Typical Commands

Unit tests only:

```bash
bash experiments/M2/run_budget_ctrl_tests.sh
```

Budget workload on FEMU/ZNS:

```bash
cmake --build cmake-build-debug --target zone_budget_ctrl_test -j$(nproc)
cmake --build cmake-build-release-cfsm-on --target db_bench zenfs_tool -j$(nproc)

BUILD_RELEASE_DIR=$PWD/cmake-build-release-cfsm-on \
DB_BENCH=$PWD/cmake-build-release-cfsm-on/db_bench \
ZENFS_TOOL=$PWD/cmake-build-release-cfsm-on/zenfs \
CONFIRM_MKFS=1 ZBD=nvme0n1 \
bash experiments/M2/run_budget_workload.sh
```

Budget on/off comparison:

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=3 \
bash experiments/M2/run_budget_compare.sh
```

Wrap up an existing result directory:

```bash
python3 experiments/M2/summarize_m2_results.py \
  experiments/M2/results/<result-dir> \
  experiments/M2/results/<result-dir>/m2_wrapup_summary.md
```

Small sensitivity run:

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 NUM=1000000 \
bash experiments/M2/run_budget_sensitivity.sh
```

Custom sensitivity grid:

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 NUM=5000000 \
B_MIN_LIST="2 6" P_TARGET_LIST="0 0.5" \
bash experiments/M2/run_budget_sensitivity.sh
```

Skip the budget-off baseline only when you are doing a smoke test:

```bash
RUN_BASELINE=0 CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 NUM=1000000 \
bash experiments/M2/run_budget_sensitivity.sh
```

Useful budget knobs:

- `FACO_BUDGET_B_MIN` / `FACO_BUDGET_B_MAX`
- `FACO_BUDGET_KP` / `FACO_BUDGET_KI`
- `FACO_BUDGET_P_TARGET`
- `FACO_BUDGET_TOP_K`
- `FACO_BUDGET_RBD_THRESHOLD`
- `FACO_BUDGET_ZVDR_WEIGHT`

Expected artifacts:

- New result directories use `YYYYMMDD-HHMMSS-purpose`, for example
  `20260429-030000-budget-compare`.
- `faco_budget_summary.txt`
- `faco_budget_trace.csv`
- `faco_runtime_metrics.txt`
- `budget_trace_analysis.md`
- `budget_trace.svg`
- `summary.csv` and `summary.md` from `run_budget_compare.sh`
- `m2_wrapup_summary.md`, `m2_run_metrics.csv`, and
  `m2_budget_trace_summary.csv` from `summarize_m2_results.py`
- M1 CFSM exports copied by the reused M1 db_bench runner
