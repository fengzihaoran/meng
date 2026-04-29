# M3 SPEC Questions

## ContentionPenalty Simplification

M3 uses a simplified ContentionPenalty (前台冲突惩罚):

```text
ContentionPenalty(z) = FACO_REORG_CONTENTION_PENALTY_BYTES
                       * (1 - current_active_zone_budget / hard_active_zone_limit)
```

Default:

```text
FACO_REORG_CONTENTION_PENALTY_BYTES=4194304
```

Reasoning:

- M2 already emits an active-zone budget (主动区预算), so a smaller budget is a
  usable low-cost proxy for foreground pressure.
- There is not yet a reliable p99 read latency (第 99 百分位读延迟) signal inside
  ZenFS. M4/M5 should replace or validate this proxy with real latency metrics.
- ZVDR (有效占比衰减率) is not promoted to a primary trigger in M3.

## Reorg Evaluation Trigger

M3 evaluation (评估) is intentionally decoupled from the legacy GC free-space
gate. The planner can evaluate every GC worker cycle, while legacy GC remains a
fallback only when free space is low and M3 did not attempt a plan.

Knobs:

```text
FACO_REORG_FORCE_EVAL=1
FACO_REORG_FREE_SPACE_TRIGGER_PERCENT=100
```

The scripts use these values for smoke tests. Set `FORCE_EVAL=0` and
`FREE_SPACE_TRIGGER_PERCENT=20` to mimic the old conservative trigger.

## Tiny Victim and Cooldown Gates

The 20260429 smoke run proved that force-evaluation alone is not enough: M3 can
accept every cycle and repeatedly select the same tiny top-RBD victim. M3 now
uses two defensive gates:

```text
FACO_REORG_MIN_MIGRATE_RATIO=0.03125
FACO_REORG_VICTIM_COOLDOWN_US=120000000
```

`MIN_MIGRATE_RATIO` derives the default byte threshold from zone capacity. For a
256 MiB zone, the default is 8 MiB. `FACO_REORG_MIN_MIGRATE_BYTES` still works
as an absolute override. `VICTIM_COOLDOWN_US` suppresses a victim after a
successful execution so the next GC worker cycle can consider other top-RBD
zones instead of immediately repeating the same zone.

M3 `migrated_bytes` now records the `gc_bytes_written` before/after delta around
the ZenFS migration call. It is intentionally not the planned extent length,
because non-SST extents and deleted files can otherwise make the planner
overstate actual data movement.

## Adaptive Tau Admission

Fixed `FACO_REORG_TAU_TRIGGER_INIT` is useful for debugging but not robust
across workloads. M3 scripts now default to adaptive admission:

```text
FACO_REORG_TAU_MODE=adaptive
FACO_REORG_ADAPTIVE_HISTORY_SIZE=32
FACO_REORG_ADAPTIVE_Q_BASE=0.85
FACO_REORG_ADAPTIVE_Q_MIN=0.70
FACO_REORG_ADAPTIVE_Q_MAX=0.90
FACO_REORG_ADAPTIVE_Q_BUDGET_GAIN=0.15
FACO_REORG_ACCEPT_HYSTERESIS=0.01
FACO_REORG_ADAPTIVE_WARMUP_EVALS=5
FACO_REORG_MIN_EXEC_INTERVAL_US=60000000
```

The planner records only the best eligible candidate from each evaluation. Tiny
and per-victim-cooldown-skipped candidates do not enter the adaptive history.
During warmup, candidates populate history but reject with `reason=warmup`.
After warmup, M3 accepts only candidates above a rolling percentile plus
hysteresis, and the global rate limiter rejects back-to-back executions with
`reason=rate_limited`.
