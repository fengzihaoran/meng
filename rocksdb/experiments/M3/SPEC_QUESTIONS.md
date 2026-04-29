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
