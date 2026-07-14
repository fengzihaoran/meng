# Codex 5.6 Sol Runbook for FragSense

## Repository authority

The authoritative tracked files are:

```text
AGENTS.md

docs/fragsense/IMPLEMENTATION_PLAN.md
docs/fragsense/CODEX_RUNBOOK.md
```

Keep `AGENTS.md` at the repository root. Start Codex from that
repository/worktree. Generated bundle directories and ZIP archives are input
artifacts and must not be committed.

Use one task/thread per milestone. Do not ask a single thread to implement Full FragSense.

---

# Task 0 — Repository audit only

Paste this into Codex:

```text
Read the root AGENTS.md and docs/fragsense/IMPLEMENTATION_PLAN.md.
Execute Milestone M0 only.

Do not modify production behavior and do not implement FragSense logic yet.
Audit the exact RocksDB/ZenFS version, existing motivation observer patches, Zone and extent data structures, used-capacity decrement/reset flow, metadata persistence/recovery, locking, background tasks, configuration system, and available RocksDB compaction-pressure/origin signals.

Create docs/fragsense/CODE_AUDIT.md containing:
1. repository/build/test topology;
2. exact files/classes/functions relevant to M1-M3;
3. current file deletion -> extent invalidation -> used-capacity decrement -> reset sequence;
4. proposed safe hooks for liveness, segment accounting, Hot(z), sealed age, policy control, and migration;
5. proposed copy-before-switch metadata protocol and recovery implications;
6. risks and unresolved questions;
7. exact build and test commands;
8. a milestone-sized patch plan.

Run a clean baseline build and the narrowest existing ZenFS tests. Report results. Stop after M0 and wait for review.
```

### Review before proceeding

Check that Codex correctly identifies Native Lazy Reset and a credible durable mapping update path.

---

# Task 0.5 — Engineering gate before M1

```text
Read AGENTS.md, CODE_AUDIT.md, and IMPLEMENTATION_PLAN.md.
Execute Milestone M0.5 only. Do not start M1 and do not modify plugin/zenfs.

Track the repository instructions and plans, document ZenFS provenance and the
existing optional active-GC path, define the formal M1 state model, and add a
non-destructive clean Linux build script. A generated version string is not
exact provenance. A copied or incremental build is not a clean-build result.

Run repository-side syntax/diff checks. If Linux is unavailable, mark the
Linux build BLOCKED and provide the exact command and evidence path. Stop after
M0.5 for human review.
```

### Review before proceeding

- Does `M1_STATE_MODEL.md` define source of truth, update point, concurrency,
  recovery, and invariants for every required field?
- Does the Linux evidence identify compiler, kernel, libzbd, repository commit,
  commands, logs, tests, and final status?
- Is the M0.5 diff empty under `plugin/zenfs`?
- Is `Upstream Active-GC` documented without claiming that it is safe?

Do not start Task 1 until the state model is approved and the clean Linux build
reports `PASSED`.

---

# Task 1 — Runtime state sensing

```text
Read AGENTS.md, CODE_AUDIT.md, and IMPLEMENTATION_PLAN.md.
Implement Milestone M1 only: runtime state sensing with no active migration.

Requirements:
- feature disabled by default;
- block-aligned liveness derived from durable ZenFS extent mappings;
- incremental valid bytes/blocks and live segment count;
- accurate R(z), Frag(z), Cost(z), logical sealed state, and age;
- a real global sliding window for Hot(z), counting foreground-update-related append events but excluding compaction outputs;
- conservative age protection for compaction-created/newly sealed Zones;
- restart reconstruction from durable mappings;
- structured metrics/snapshot output;
- no new active reset/migration path;
- no whole-Zone scan on every foreground append.

Add focused unit/integration tests for allocation, invalidation, segment split/merge, sliding-window eviction and origin separation, reset, restart rebuild, and Native-disabled behavior.

Before editing, write a short implementation note. After editing, run formatting/build/tests and report exact commands, changed files, overhead risks, and known limitations. Stop after M1.
```

### Gate A checklist

- Does Hot(z) genuinely distinguish foreground flush output from compaction output?
- Is the granularity block-aligned, not falsely key-level?
- Is the restart rebuild correct?
- Is Native disabled mode unchanged?

---

# Task 2 — Dry-run CFF and baselines

```text
Read AGENTS.md, CODE_AUDIT.md, and IMPLEMENTATION_PLAN.md.
Implement Milestone M2 only.

Build a policy interface supporting native, reclaimable-space, valid-ratio, frag-only, cff, and cff_filter dry-run modes.

Implement:
R(z) = reclaimable ratio
Frag(z) = live segments / valid blocks
Cold(z) = 1 - Hot(z)/W
P(z) = Frag(z) * Cold(z)
U_rec(z) = R(z) * P(z)
Cost(z) = valid bytes
Density(z) = U_mode(z)/(Cost(z)+epsilon)

Candidate admission must separately enforce nonzero live bytes, logical sealed state, reclaim threshold, minimum age, and execution threshold. Log one explicit reject reason.

Dry-run must have a hard guarantee that it cannot migrate data or reset a source Zone. Add tests for that guarantee.

Export per-cycle per-Zone component values, rank, selected flag, policy, and budget. Update the offline analysis tool to compare policies using future invalidation, natural reset delay, redundant fraction, and estimated released bytes per migrated byte on identical snapshots.

Run tests and generate a small dry-run report from an existing motivation trace or safe local fixture. Do not claim CFF superiority unless the data show it. Stop after M2.
```

### Gate B checklist

- Are all score components visible?
- Do identical admission conditions make baseline comparison fair?
- Is no migration possible in dry-run?
- Does production Hot(z), not the earlier proxy, appear in evaluation?

---

# Task 3 — Single-Zone evacuation prototype

```text
Read AGENTS.md, CODE_AUDIT.md, and IMPLEMENTATION_PLAN.md.
Implement Milestone M3 only: a manually triggered single-Zone evacuation prototype.

Do not implement autonomous selection, multi-Zone regrouping, elastic budget, DP, or full I/O coordination.

Use copy-before-switch semantics:
1. take a stable view of source live extents;
2. reserve destination capacity;
3. append-copy data;
4. validate copy;
5. persist a new mapping/relocation record;
6. atomically publish the new mapping;
7. prove no live reference remains on the source;
8. reset through the existing safe reset path.

If the existing metadata format cannot safely express relocation, stop and propose a minimal relocation transaction format before proceeding.

Add an explicit debug/admin trigger requiring a source Zone ID. Keep active behavior disabled by default.

Add fault injection for failures before/mid/after copy, before/after mapping switch, and before reset. Add recovery tests verifying data correctness and exactly one authoritative mapping.

Do not run destructive commands on a real device. Provide them for human execution only.

Report concurrency decisions for reads, deletion, compaction invalidation, shutdown, allocation failure, metadata failure, and reset failure. Stop after M3.
```

### Gate C checklist

- Is reset impossible before durable mapping switch?
- Do crash tests pass?
- Are reads correct during migration?
- Can deletion/compaction safely race with migration?

---

# Task 4 — Active policy baselines and ablation modes

```text
Read AGENTS.md and IMPLEMENTATION_PLAN.md.
Implement Milestone M4 only, using the already validated M3 migrator.

Connect configuration-selectable policies:
native, reclaimable, valid_ratio, frag, cff, cff_filter.

All policies must share the same migrator, control period, destination allocation, safety checks, and accounting. Only admission/ranking logic may differ.

Add a fixed byte budget and fixed migration bandwidth cap, both disabled/zero by default unless FragSense active mode is explicitly enabled.

Export migration and benefit accounting: valid bytes moved, source evacuation success, reset success, released bytes, released/migrated ratio, short-window reinvalidation, migration bandwidth/duration, free/blocked Zones, and separate migration/compaction/device write bytes.

Add scripts that run identical workload/seed/configuration across policies. Do not run a real device without explicit user action. Stop after M4.
```

---

# Task 5 — Elastic budget and Compaction-aware control

```text
Read AGENTS.md and IMPLEMENTATION_PLAN.md.
Implement Milestone M5 only.

Add a control-period byte budget with raw, smoothed, clamped, consumed, and unused values. Implement the paper formula with configurable B_base, B_min, B_max, Frag_target, alpha, and period.

Budget is permission, not mandatory work. No high-benefit candidate means no migration.

Add a small RocksDB compaction-pressure provider using reliable runtime properties/callbacks available in this repository version. Prefer pending compaction bytes, running jobs, L0 pressure, and stall state over log parsing.

Add background-only bandwidth control. Under high compaction pressure, reduce or pause proactive FragSense work; permit only clearly logged emergency reclaim when free Zones cross the safety threshold. Resume gradually.

Keep fixed-budget and no-I/O-coordination modes selectable for ablation.

Add deterministic controller tests for rising/falling fragmentation, clamping, smoothing, no-forced-work, pressure pause/resume, emergency priority, and restart initialization. Stop after M5.
```

---

# Task 6 — Layout regrouping, only after M3-M5 are stable

```text
Read AGENTS.md and IMPLEMENTATION_PLAN.md.
Implement Milestone M6 only.

Add bounded layout-oriented regrouping as a separate operation from immediate evacuation. Combine live extents from a small number of high-fragmentation, low-activity sources into controlled destination placement.

Report separately:
- source Zones fully evacuated/reset immediately;
- sources partially compacted but not released;
- before/after segment metrics;
- destination packing;
- later natural-reset/reclaimability outcomes.

Reuse copy-before-switch for every mapping. Keep regrouping independently disabled by default. Add partial-operation recovery tests. Stop after M6.
```

---

# Task 7 — Experiment automation and audit

```text
Read AGENTS.md and Milestone M7.
Do not change core policy semantics.

Build reproducible experiment runners and analyzers for:
- Native motivation continuity;
- end-to-end Native/Reclaimable/Valid-ratio/CFF+Elastic/Full;
- long-term stability;
- ablation;
- sensitivity and workload robustness;
- runtime overhead;
- crash/recovery.

Every run must archive exact commands, seed, git commit, dirty status, configuration, hardware/device parameters, raw RocksDB/ZenFS logs, Zone snapshots, decision logs, migration logs, compaction pressure, CPU/I/O, and exclusions.

Generate paper-ready CSV/PDF figures with clearly stated n and confidence intervals. Never fill paper claims with synthetic or placeholder percentages. Produce an analysis report with Supported / Unsupported / Missing Evidence sections. Stop after the tooling and a safe smoke test.
```

---

# Recommended task sequencing

```text
M0 audit
  -> human review
M1 sensing
  -> Gate A
M2 dry-run
  -> Gate B and replay evidence
M3 single-Zone evacuation
  -> Gate C and crash tests
M4 active baselines
  -> first controlled experiments
M5 elastic + I/O coordination
  -> ablation and long-term tests
M6 regrouping
  -> only if title/contribution requires it and M3-M5 are stable
M7 full evaluation tooling
M8 optional DP
```

# Suggested Codex subagent delegation

When supported, ask the main Codex task to delegate independent review work, not overlapping code edits:

- **Audit subagent:** metadata/recovery and locking map;
- **Test subagent:** test matrix and fault-injection review;
- **Metrics subagent:** schemas and experiment reproducibility;
- **Code-review subagent:** Native-mode regression and safety invariants.

The main agent must integrate and own the final patch. Avoid two subagents editing the same files concurrently.
