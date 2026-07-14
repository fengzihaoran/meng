# FragSense Implementation Plan for Codex 5.6 Sol

## 0. Purpose and implementation boundary

This plan converts the current FragSense paper design into an incremental engineering program for RocksDB-ZenFS on ZNS SSDs.

FragSense addresses a verified Native ZenFS motivation:

- Lazy Reset can leave Zones containing substantial invalid space but nonzero live extents;
- these blocked Zones cannot be reset naturally;
- current valid ratio alone does not reliably distinguish stable candidates from Zones likely to continue invalidating;
- an active path therefore needs fragmentation state, recent evolution state, candidate protection, migration cost control, and background-I/O coordination.

The first implementation target is a safe and measurable **evacuation-based reclaim path**. Layout-oriented multi-Zone regrouping is a later milestone, not a prerequisite for initial correctness or ablation experiments.

## 1. Global success criteria

FragSense is considered implementation-ready for evaluation only when all of the following hold:

1. Native mode is behaviorally unchanged and remains the default.
2. Real Zone state, block-aligned liveness, segments, `Frag(z)`, `Hot(z)`, age, and admission decisions are observable.
3. Dry-run CFF decisions can be replayed and compared with valid-ratio and reclaimable-space policies.
4. A selected source Zone can be evacuated with copy-before-switch semantics and safely reset.
5. Crash/fault injection does not cause missing, duplicated, or ambiguously mapped extents.
6. Policies and components are independently selectable for ablation.
7. Elastic budget and compaction-pressure control operate outside the foreground path.
8. Raw logs allow reconstruction of all reported paper metrics.

## 2. Recommended repository layout

Adapt paths to the existing repository, but keep concerns separated:

```text
plugin/zenfs/
  fragsense/
    fragsense_config.{h,cc}
    fragsense_zone_state.{h,cc}
    fragsense_activity_window.{h,cc}
    fragsense_policy.{h,cc}
    fragsense_controller.{h,cc}
    fragsense_migrator.{h,cc}
    fragsense_metrics.{h,cc}
    fragsense_recovery.{h,cc}
  tests/
    fragsense_zone_state_test.cc
    fragsense_activity_window_test.cc
    fragsense_policy_test.cc
    fragsense_migration_test.cc
    fragsense_recovery_test.cc
scripts/fragsense/
tools/fragsense/
docs/fragsense/
```

Do not force this structure if the repository has a stronger established convention.

---

# Milestone M0 — Audit, freeze, and design map

## Goal

Understand the exact ZenFS/RocksDB version and locate all safe hooks before changing behavior.

## Required audit

Identify and document:

- Zone class and state transitions;
- Zone write pointer, capacity, used-capacity counter, reset path;
- `ZoneFile`/extent representation and file-to-extent metadata format;
- extent allocation and append path;
- file deletion, compaction input deletion, and extent invalidation path;
- metadata log/manifest persistence and recovery reconstruction;
- locking hierarchy for files, extents, zones, allocation, and metadata;
- existing background threads, rate limiters, and statistics;
- RocksDB APIs/properties for:
  - flush vs compaction origin;
  - pending compaction bytes;
  - running compactions;
  - write stalls;
  - background I/O pressure;
- existing motivation observer modifications and whether they are committed.

## Deliverables

- `docs/fragsense/CODE_AUDIT.md`
- component diagram;
- exact symbols/files proposed for M1–M3;
- risk list;
- build and test commands;
- a clean baseline build/test result.

## Acceptance criteria

- No production behavior changes.
- No active reclaim implementation.
- Reviewer can trace file deletion to used-capacity decrement and Zone reset.
- Reviewer can trace how a migrated extent mapping would be persisted and recovered.

---

# Milestone M0.5 — Engineering gate before M1

## Goal

Close the repository-governance, provenance, reproducible-build, and state
semantics gaps found by M0 without changing ZenFS production behavior.

## Deliverables

- tracked root `AGENTS.md`, this plan, and `CODEX_RUNBOOK.md`;
- `docs/fragsense/ZENFS_PROVENANCE.md` with an upstream candidate and a
  reproducible vendored-tree comparison;
- `docs/fragsense/M1_STATE_MODEL.md` defining every M1 field, update point,
  invariant, lock rule, and restart behavior;
- `scripts/fragsense/build_linux.sh` and archived clean-build evidence;
- an explicit record of the existing optional `--enable_gc=true` path and its
  known safety risks;
- `docs/fragsense/M05_REPORT.md` with unresolved blockers and the M1 start
  decision.

## Acceptance criteria

- No diff under `plugin/zenfs`.
- Generated bundles, bytecode, and build directories are excluded from the
  milestone commit.
- The Linux build is either freshly verified or explicitly `BLOCKED`; copied
  artifacts are not accepted as evidence.
- M1 definitions do not silently reuse motivation-observer approximations.
- `Upstream Active-GC` is retained in the future baseline matrix.
- Human review approves the state model and clean Linux evidence before M1
  production code starts.

---

# Milestone M1 — Runtime state sensing

## Goal

Implement accurate, low-overhead state collection without active migration.

## M1.1 Zone state model

Create a per-Zone state structure equivalent to:

```cpp
struct FragSenseZoneState {
  uint64_t zone_id;
  uint64_t capacity_bytes;
  uint64_t written_bytes;
  uint64_t live_bytes;
  uint64_t invalid_bytes;
  uint64_t valid_blocks;
  uint64_t live_segments;
  uint64_t last_foreground_event_seq;
  uint64_t sealed_event_seq;
  uint64_t sealed_time_micros;
  ZoneLifecycleState lifecycle_state;
};
```

Derived values:

```text
invalid_bytes = written_bytes - live_bytes
R(z)       = invalid_bytes / capacity_bytes
Frag(z)    = live_segments / valid_blocks, valid_blocks > 0
Cost(z)    = live_bytes
Age(z)     = current_time_or_event - sealed_time_or_event
```

Here `written_bytes` means capacity consumed in the current Zone generation,
including append padding and any tail discarded by a successful Zone finish.
Therefore `R(z) = (capacity_bytes - live_bytes) / capacity_bytes` is equivalent
only for a fully consumed or finished Zone. Using that expression for a
partially writable Zone would incorrectly count unwritten capacity as
reclaimable invalid data.

A Zone with zero live bytes must bypass CFF and remain eligible for direct reset through the existing safe path.

## M1.2 Block-aligned liveness

Implement block-aligned liveness based on ZenFS file/extent-to-Zone-offset mappings.

Requirements:

- configurable block size, default aligned with the repository/device constraints;
- one bit per block or an equivalent compact representation;
- allocation marks an appended interval live;
- file/extent invalidation clears the interval;
- reset clears the state;
- restart rebuilds it from durable ZenFS mappings;
- no key-level or SSTable-record-level liveness claims.

For very large Zones, document memory usage and allow a coarser block size for experiments, while keeping the paper’s granularity explicit.

## M1.3 Incremental segment accounting

Maintain continuous live segment count without scanning the full Zone on every event.

Cover these cases with tests:

- append contiguous to an existing segment;
- append creating a new segment;
- invalidate middle of a segment, splitting it into two;
- invalidate segment head/tail;
- invalidate an entire isolated segment;
- repeated/overlapping invalidation is idempotent or rejected safely.

## M1.4 Real Hot(z) activity window

Implement a global sliding window of the last `W` **foreground-update-related storage append events**.

Preferred origin classification order:

1. reuse an existing RocksDB `IOActivity`, file-creation reason, job context, or equivalent reliable tag;
2. add a minimal cross-layer origin tag propagated to ZenFS;
3. only as a temporary fallback, infer origin using a documented conservative rule.

Rules:

- foreground flush/SST output caused by user updates updates `Hot(z)`;
- compaction output does not update `Hot(z)`;
- compaction invalidation updates liveness and fragmentation only;
- newly sealed or compaction-created Zones are protected by age even if `Hot(z)=0`.

Define:

```text
Hot(z)  = occurrences of z in the last W eligible events
Cold(z) = 1 - Hot(z) / W
```

## M1.5 Sealed and age semantics

Do not confuse a FragSense logical `sealed` state with a raw NVMe Zone state if they differ.

Document exactly when a Zone becomes eligible for age counting and when age resets.

## M1.6 Metrics and snapshots

Export structured state for every control cycle. Include source granularity and origin classification.

## M1 tests

- state accounting unit tests;
- bitmap/interval tests;
- segment tests;
- sliding-window tests;
- rebuild-after-restart tests;
- Native-disabled regression;
- overhead microbenchmark for foreground state update.

## M1 acceptance criteria

- FragSense disabled: no behavior change.
- FragSense sensing enabled: no active migration/reset beyond Native behavior.
- State reconstructed after restart matches durable extents.
- `Hot(z)` excludes compaction output in tests.
- No whole-Zone scan is added to each foreground append.

---

# Milestone M2 — Dry-run policy engine

## Goal

Implement all admission and ranking logic while guaranteeing no data migration.

## M2.1 Candidate admission

Space-reclaim candidate:

```text
ValidBytes(z) > 0
state(z) == sealed
R(z) >= T_rec
Age(z) >= T_age
```

Record a single explicit reject reason:

- not sealed;
- zero live bytes/direct reset;
- reclaimable space below threshold;
- age protection;
- score below execution threshold;
- budget infeasible;
- background pressure;
- inconsistent state.

## M2.2 Scores

```text
P(z)     = Frag(z) * Cold(z)
U_rec(z) = R(z) * P(z)
Density  = U_mode(z) / (Cost(z) + epsilon)
```

Keep each component in the log. Never log only the final score.

## M2.3 Policies for comparison

Implement a common policy interface for:

- `native`: no active selection;
- `reclaimable`: highest `R(z)`;
- `valid_ratio`: lowest valid ratio / valid bytes under identical admission conditions;
- `frag`: highest `Frag(z)`;
- `cff`: `P(z)`, fixed budget, minimal admission;
- `cff_filter`: `U_rec(z)` plus sealed/age/reclaim threshold;
- later `cff_elastic` and `full`.

## M2.4 Dry-run decision log

For each cycle and Zone, output:

```text
cycle, zone_id, state, age, live_bytes, invalid_bytes,
R, valid_blocks, segments, Frag, Hot, Cold,
P, U_rec, Cost, Density,
admitted, reject_reason, rank, selected,
policy, budget_bytes
```

Add a hard assertion: dry-run mode cannot invoke migrator or source reset.

## M2.5 Offline replay

Update the existing analysis pipeline to compare selected candidates against future invalidation and natural-reset delay.

Report:

- future-invalidated / migrated-live;
- natural-reset probability/delay;
- released-space estimate per migrated byte;
- overlap among policies;
- score component distributions.

Do not require CFF to win every run; report failures honestly.

## M2 acceptance criteria

- No active writes/resets in dry-run.
- Every selected candidate is explainable from logged components.
- Policies can be selected without recompilation.
- Native and dry-run output can be replayed reproducibly.
- A report compares `valid_ratio`, `reclaimable`, `frag`, and `cff_filter` on identical snapshots.

---

# Milestone M3 — Safe single-Zone evacuation prototype

## Goal

Safely evacuate one source Zone at a time and reset it, using a manually triggered or tightly gated path.

## Explicit scope

Implement only **evacuation-based reclaim** first.

Do not yet implement:

- multi-source regrouping;
- dynamic programming;
- adaptive budget;
- compaction coordination beyond a conservative pause gate;
- concurrent migrations.

## M3.1 Preconditions

Before migration:

- FragSense enabled and dry-run disabled;
- source is admitted and logically sealed;
- source is not a metadata/reserved Zone;
- source has nonzero live extents;
- destination capacity is reserved;
- open/active Zone limits permit the operation;
- no conflicting migration is active;
- source mapping snapshot is internally consistent.

## M3.2 Copy-before-switch protocol

Preferred sequence:

1. acquire a stable source-file/extent view using the repository’s locking conventions;
2. reserve destination space;
3. append-copy each live extent to the destination;
4. validate written length/checksum/metadata as available;
5. build a new mapping record without exposing it to readers;
6. persist and sync the new mapping/transaction marker;
7. atomically publish the new file-to-extent mapping;
8. persist completion state;
9. verify no durable/live reference points to the source Zone;
10. mark source reclaimable;
11. reset source through the existing safe reset path;
12. persist cleanup/completion metrics.

If the existing ZenFS metadata log cannot make this transition atomic enough, stop and propose a small relocation transaction record rather than improvising unsafe behavior.

## M3.3 Concurrency

Define behavior for:

- reads during copy;
- file deletion during copy;
- compaction deleting a source file during copy;
- shutdown during copy;
- source becoming empty naturally;
- destination allocation failure;
- metadata sync failure;
- reset failure.

Prefer abort/retry over complex lock expansion.

## M3.4 Fault injection

Inject failure at:

- before first copy;
- mid-copy;
- after copy before mapping persistence;
- after mapping persistence before publish;
- after publish before source reset;
- during reset.

After recovery:

- every file must resolve to valid data;
- exactly one durable mapping version must be authoritative;
- leaked destination/source space may be cleaned conservatively, but user data must remain correct.

## M3.5 Manual trigger

Provide a debug/admin trigger selecting an explicit Zone ID. Do not initially allow autonomous migration.

## M3 acceptance criteria

- Single-Zone evacuation passes correctness checks.
- Source reset occurs only after durable mapping switch.
- Fault-injection recovery passes.
- FragSense disabled remains Native.
- Real-device destructive commands are documented, not run by the agent.

---

# Milestone M4 — Policy-controlled active reclaim and ablations

## Goal

Connect policy selection to the proven M3 migrator and create paper baselines.

## Required modes

- Native ZenFS (`--enable_gc=false`);
- Upstream Active-GC (`--enable_gc=true`), isolated from FragSense and used
  only after its known migration-ordering risks have been tested on a
  disposable device;
- Reclaimable-space Greedy;
- Valid-ratio Greedy;
- Frag-only;
- CFF-only with fixed budget;
- CFF + reclaim/age/sealed filter;
- CFF + fixed bandwidth limit.

All modes must use the same migrator, safety protocol, control period, and destination allocator. Only selection/admission logic should differ.

## Required metrics

- selected/rejected Zones;
- valid bytes migrated;
- source Zones fully evacuated;
- source reset success;
- released bytes;
- released bytes per migrated byte;
- short-window reinvalidation;
- migration duration/bandwidth;
- total/compaction/migration write bytes;
- blocked Zones and blocked invalid space;
- free Zones;
- foreground throughput and latency;
- stalls and compaction pressure.

## M4 acceptance criteria

- All ablation policies run from configuration.
- Same workload/seed can be rerun with a different policy.
- Migration accounting is separated from compaction/device writes.
- No policy bypasses safety/admission invariants.

---

# Milestone M5 — Elastic budget and Compaction-aware I/O coordination

## Goal

Add feedback-controlled migration intensity after reclaim correctness and baselines are stable.

## M5.1 Budget semantics

Budget unit: valid bytes allowed to migrate in the current control period.

```text
if Frag_current(t) <= Frag_target:
    B_raw(t) = B_base
else:
    B_raw(t) = B_base * Frag_current(t) / Frag_target

B_smooth(t) = alpha * B_smooth(t-1)
              + (1-alpha) * B_raw(t)

B(t) = clamp(B_smooth(t), B_min, B_max)
```

Budget is permission, not an obligation. No candidate or low benefit means no migration.

Log raw, smooth, clamped, consumed, and unused budget.

## M5.2 Trigger priority

1. space-pressure reclaim;
2. fragmentation-pressure maintenance while background idle;
3. lightweight proactive processing for high-score candidates;
4. otherwise no action.

Emergency space pressure may prioritize high reclaimable space and low cost over CFF, but must remain safe and logged as an emergency mode.

## M5.3 Compaction pressure interface

Prefer real RocksDB properties/callbacks, for example repository-version equivalents of:

- estimated pending compaction bytes;
- running/scheduled compaction jobs;
- L0 file pressure;
- write-stall state;
- background errors.

Expose a small provider interface so policy/controller code does not depend directly on ad hoc log parsing.

## M5.4 Rate control

Use an existing RocksDB/ZenFS rate limiter if suitable. Otherwise add a dedicated background token bucket outside foreground I/O.

When compaction pressure crosses threshold:

- reduce migration bandwidth;
- pause proactive regrouping;
- allow only emergency reclaim if necessary;
- resume gradually to prevent oscillation.

## M5 tests

- budget formula and clamping;
- smoothing response to rising/falling fragmentation;
- no forced work at low pressure;
- compaction-pressure pause/resume;
- emergency reclaim priority;
- rate-limiter accounting;
- controller restart initialization.

## M5 acceptance criteria

- Fixed and elastic budgets are separately selectable.
- Compaction coordination can be disabled for ablation.
- No foreground thread sleeps on the migration rate limiter.
- Control decisions are reconstructable from metrics.

---

# Milestone M6 — Layout-oriented regrouping

## Goal

Only after evacuation is stable, add multi-source live-extent regrouping for long-term reclaimability.

## Scope rules

- treat regrouping as a separate operation from immediate source evacuation;
- record whether each source Zone was fully emptied/reset;
- do not claim immediate released space when sources remain nonempty;
- choose destinations to avoid recreating lifetime mixing;
- preserve copy-before-switch safety per extent/file;
- cap the number of sources and total bytes per transaction/cycle.

## Candidate/benefit metrics

- `P(z)` for fragmented, relatively cold Zones;
- expected source evacuation count;
- average live segment size before/after;
- destination packing density;
- later natural reset probability/delay;
- future migration cost.

## M6 acceptance criteria

- Regrouping can be disabled independently.
- Immediate reclaim and long-term layout benefits are reported separately.
- Recovery handles partial multi-source work conservatively.
- No source reset occurs unless fully evacuated.

---

# Milestone M7 — Experiment automation and paper-grade output

## Required experiment matrix

### Motivation baseline

Keep current Native ZenFS results archived and reproducible.

Also retain an `Upstream Active-GC` baseline entry. If the existing
`--enable_gc=true` path cannot complete safely or reliably on a disposable
test device, archive the failure evidence and report it as an unavailable
baseline rather than silently omitting it.

### End-to-end

- Native;
- Upstream Active-GC;
- Reclaimable Greedy;
- Valid-ratio Greedy;
- CFF + Elastic;
- Full FragSense;
- optional lifetime-aware/OAZA-like baseline with clearly stated approximation.

Workloads:

- YCSB-A;
- YCSB-B;
- YCSB-F;
- db_bench overwrite for continuity with motivation.

### Long-term

At least YCSB-A, optionally YCSB-F, across multiple compaction/Zone lifecycle cycles.

### Ablation

- Native;
- Frag-only;
- CFF-only;
- CFF + Filter;
- CFF + Elastic;
- CFF + Elastic + I/O coordination;
- Full.

### Sensitivity

- `W`: 1K/4K/16K/64K events;
- admission: Loose/Default/Strict;
- budget: Fixed/Conservative/Default/Aggressive;
- Zipf/update pressure variants.

### Overhead

- bitmap memory;
- window memory/update time;
- foreground state-update overhead;
- candidate scan/score/select time;
- migration CPU and bandwidth;
- recovery rebuild time;
- optional DP trigger/time if DP is later added.

## Statistical requirements

- at least 3 runs; prefer 5 for principal figures;
- archive seeds, exact commands, git commit, dirty status, configuration, and device parameters;
- report mean and 95% CI or clearly stated alternative;
- separate observational correlation from causal controlled comparison;
- do not hide failed/aborted runs; record exclusion reasons.

## Required raw artifacts

- RocksDB LOG/statistics;
- exact commands;
- Zone snapshots;
- decision logs;
- migration transaction logs;
- compaction-pressure and budget logs;
- system I/O/CPU logs;
- per-operation latency when available;
- metadata/config/commit manifest.

---

# Milestone M8 — Optional optimizer and advanced recovery

Dynamic programming is optional and should be last.

Only implement it if:

- candidate count is small;
- discrete budget state is bounded;
- the greedy-to-upper-bound gap is meaningful;
- background is idle;
- measured benefit exceeds complexity and runtime cost.

The paper’s core contribution must not depend on DP.

---

# 3. Review gates

## Gate A — after M1

Review:

- state correctness;
- origin tagging for Hot(z);
- memory overhead;
- Native regression;
- restart reconstruction.

## Gate B — after M2

Review:

- whether CFF components match paper semantics;
- dry-run policy evidence;
- proxy/oracle claims removed from production Hot(z) evaluation;
- candidate reject explanations.

## Gate C — after M3

Review:

- migration protocol;
- metadata durability;
- reset safety;
- fault-injection results;
- concurrency behavior.

## Gate D — after M4/M5

Review:

- fair baseline implementation;
- separated write-amplification accounting;
- elastic control stability;
- compaction interference;
- paper experiment readiness.

---

# 4. Claims that code and experiments must not make prematurely

Do not claim until measured and verified:

- a 41% write-amplification reduction;
- a 46% P99 latency reduction;
- a 62% fragmentation reduction;
- a 58% Zone-management reduction;
- block-level fragmentation when only extent-level state exists;
- exact prediction of future invalidation by Hot(z);
- causal latency improvement based only on correlation;
- unchanged crash consistency without fault-injection evidence;
- successful layout regrouping if only single-Zone evacuation is implemented.
