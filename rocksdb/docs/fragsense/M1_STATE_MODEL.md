# M1 Formal State Model

## Scope

M1 is sensing only. It may collect and export state, but it must not select a
victim, migrate an extent, change placement, finish a Zone solely for
FragSense, or add a new reset path. Native ZenFS remains the default and all M1
state is disabled unless one validated FragSense configuration enables sensing.

The motivation observer is not a source of truth for M1. Its extent-count
fragmentation, first-observed age, `capacity_ == 0` sealing heuristic, and
unsynchronized snapshots are experiment approximations and are not reused
automatically.

## Terminology and Zone generation

A Zone generation begins after a successful device reset and ends at the next
successful reset. Every event and activity-window entry is keyed by
`(zone_id, generation)`, not `zone_id` alone. This prevents stale Hot(z) events
from a previous generation from affecting a reused Zone.

In this document `written_bytes` means capacity consumed in the current
generation. It includes successfully appended physical bytes, append padding,
inline sparse headers, and any unwritten tail made unavailable by a successful
Zone finish. It is not a claim about user payload bytes or NAND-programmed bytes.

## Required state and source of truth

| Field | Exact definition and source of truth |
| --- | --- |
| `capacity_bytes` | `Zone::max_capacity_`, fixed for one generation |
| `written_bytes` | runtime consumed-capacity counter; rebuild as `capacity_bytes - remaining_capacity` from the backend, clamped to the generation capacity |
| `live_bytes` | sum of logical byte lengths in all current ZenFS file-to-extent mappings plus the current writable provisional extent; hard links do not duplicate the shared `ZoneFile` mapping |
| `invalid_bytes` | `written_bytes - live_bytes`; invariant failure is an error, never silently clamped |
| `valid_blocks` | cardinality of the union of block-aligned live intervals for the configured M1 block size |
| `live_segments` | number of maximal contiguous runs of valid blocks |
| `Frag(z)` | `live_segments / valid_blocks` when `valid_blocks > 0`, otherwise `0` |
| `Frag_current(t)` | `sum_z(live_segments(z)) / sum_z(valid_blocks(z))` over non-empty generations when the denominator is nonzero, otherwise `0` |
| `Hot(z)` | occurrences of `(z,generation)` in the last `W` eligible foreground append events |
| `Cold(z)` | `1 - Hot(z)/W`; requires validated `W > 0` |
| `Age(z)` | elapsed monotonic time since the current generation entered logical sealed state; zero when not sealed |
| `sealed` | generation is terminal for Native allocation and has no current writable owner; close alone is not sealed |
| `R(z)` | `invalid_bytes / capacity_bytes` when capacity is nonzero, otherwise `0` |
| `file_extent_count` | number of current finalized `ZoneExtent` objects whose mapping references the Zone; provisional active extent is tracked separately |

`live_bytes` is the formal name. Existing `used_capacity_` is a consistency
oracle, not the only M1 storage representation. At a quiescent snapshot they
must agree. During an active append, a documented provisional extent can make
M1 live state lead the finalized `ZoneExtent` list, but it must still agree with
the active file's recoverable range.

## Block-aligned liveness

### Granularity

Let `P` be `ZonedBlockDevice::GetBlockSize()` and `B` be the configured
FragSense block size.

Configuration validation must enforce:

```text
B >= P
B mod P == 0
B > 0
```

The default `B` is `P`. A coarser `B` is allowed only when metrics record the
value and the paper labels the result at that granularity. A Zone has
`ceil(capacity_bytes / B)` tracked blocks.

### Interval conversion

For a live physical interval `[offset, offset + length)` relative to Zone start:

```text
first_block = floor(offset / B)
end_block   = ceil((offset + length) / B)  // exclusive
```

The interval must be non-empty, lie within the generation, and be associated
with a stable extent identity. Sparse metadata and alignment padding are not
separate live payload, but a block intersecting live data remains valid.

Use per-block reference counts or an equivalent interval structure, not a bare
toggle with ambiguous overlapping updates. Each live identity is
`(file_id, extent_identity, zone_id, generation, offset, length)`:

- adding an already registered identity is idempotent;
- removing an unknown identity is a diagnostic error;
- decrementing a zero block reference is forbidden;
- a block is valid exactly when its reference count is nonzero.

### Incremental segment updates

The implementation must update `valid_blocks` and `live_segments` from the
changed interval boundaries. It must not scan the full Zone on every append or
invalidation.

For each block transition:

- `0 -> 1`: increment valid blocks; add one segment, then subtract one for each
  already-valid immediate neighbor;
- `1 -> 0`: decrement valid blocks; subtract one segment, then add one for each
  still-valid immediate neighbor.

Batch updates may use an interval tree or run-length map, but must preserve the
same result and complexity proportional to changed runs rather than Zone size.

## Update points

### Successful append

Inside `Zone::Append`, after each positive backend write result and the matching
write-pointer/capacity update, increment `written_bytes` by the actual consumed
bytes. This update occurs even if a later backend write in the same call fails;
already consumed capacity does not become free because the overall call returns
an error.

Only after the higher-level `ZoneFile` append confirms which payload bytes are
part of its recoverable active extent may M1 mark that payload interval live.
Bytes physically consumed before a failed logical append remain written but
invalid. The successful logical append then:

1. marks the payload portion live under a provisional active extent identity;
2. classifies origin from the `IOOptions` carried by `ZonedWritableFile`;
3. appends one Hot(z) event only for eligible `kFlush` origin.

No state event is emitted for bytes the backend did not accept.

If one logical append spans Zones, each successful per-Zone portion is a
separate storage append event. Padding changes consumed capacity but does not
create an additional Hot(z) occurrence.

### Extent finalization

`ZoneFile::PushExtent()` converts the provisional active identity into the
finalized `ZoneExtent` identity without changing block validity or double
counting live bytes. It increments `file_extent_count` once.

`BufferedAppend`, `SparseAppend`, `Recover`, and active-extent repair must
provide the exact payload interval and consumed physical interval. The state
API must not infer these by scanning all files.

### Metadata replay and replacement

`ZoneFile::DecodeFrom()` and `MergeUpdate()` rebuild/install finalized extent
identities. A `kFileReplace` update must remove the complete old identity set
and install the complete new set under the existing mapping lock protocol.

### Invalidation and deletion

`ZoneFile::ClearExtents()` and successful mapping replacement remove the
affected identities exactly once. They decrement live bytes, block references,
segments, and `file_extent_count`. Deletion origin is irrelevant: all mappings
become invalid even though `DeleteFile()` carries no reliable compaction tag.

### Finish and seal

A successful `Zone::Finish()` sets `written_bytes = capacity_bytes` because the
remaining tail is unavailable until reset. A full successful append produces
the same terminal state. Neither operation alone starts Age(z) while a writable
owner still holds the Zone.

### Reset

Only the existing Native reset path may call `Zone::Reset()` in M1. After the
backend reset succeeds, M1:

1. verifies zero live identities and zero provisional live bytes;
2. increments the in-memory generation;
3. clears written/live/invalid bytes, block references, segment count,
   finalized/provisional extent counts, seal state, and age;
4. exposes Hot(z)=0 for the new generation.

If backend reset fails, the old generation and all state remain intact.

## Hot(z) origin classification

RocksDB 8.11.3 propagates `Env::IOActivity` through `IOOptions` to filesystem
append calls. M1 uses this existing signal without changing placement:

| `IOActivity` | Hot(z) action |
| --- | --- |
| `kFlush` | add an eligible foreground-update storage append event |
| `kCompaction` | do not update Hot(z); still update liveness and written state |
| recovery, unknown, WAL, or other activity | do not update Hot(z); increment a structured origin diagnostic counter |

The activity window owns a global deque of the last `W` eligible
`(zone_id,generation)` events and a matching occurrence-count map. Insertion
increments the key count; eviction decrements it. Restart begins with an empty
window. This conservative loss of history makes Zones colder, so sealed age
protection remains mandatory.

The label "foreground" means flush-origin storage output, not direct user I/O
to ZenFS and not a precise key-lifetime predictor.

## Lifecycle and Age(z)

M1 distinguishes these lifecycle states:

| State | Meaning |
| --- | --- |
| `empty` | current generation has zero consumed bytes and no live identity |
| `writable` | Zone may receive Native appends or has a writable owner |
| `closed_reopenable` | backend/ZenFS closed it, but Native allocation may reopen it |
| `sealed` | full or successfully finished, unavailable to Native allocation, and no writable owner remains |

`closed_reopenable` is not a candidate and has `Age(z)=0`. M1 sensing must not
finish a partially written Zone to make it sealed.

The sealed transition occurs when both conditions become true:

1. the generation is terminal because a successful append exhausted capacity,
   a successful Native finish retired the tail, or recovery reports zero
   remaining capacity; and
2. no `ZoneFile::active_zone_` or allocator reservation can append to it.

At the transition, store a monotonic `sealed_time_micros` and event sequence.
Age is sampled from the same monotonic clock. Wall-clock time is for logs only.

On restart, seal state is rebuilt from backend capacity and writable ownership.
The historical seal timestamp is not persisted in M1. A rebuilt sealed Zone is
assigned the mount completion time, so `Age(z)=0` and it must pass the full age
protection interval again. This is conservative and avoids an on-disk format
change.

## Concurrency rules

Each Zone generation has one state mutex protecting lifecycle, consumed/live
bytes, identity registration, block references, segment count, counts, and seal
time. A state snapshot copies these fields while holding that mutex briefly.

Lock ordering is one way:

```text
existing ZenFS namespace/file/Zone ownership lock
  -> per-Zone FragSense state mutex
```

FragSense state code must never acquire `files_mtx_`, a `ZoneFile` read/write
lock, or an allocator token while holding the state mutex. This prevents a
reverse edge against existing metadata and deletion paths.

The global activity-window mutex protects only the deque and Hot-count map. It
must not acquire a per-Zone state mutex. Activity keys include generation, so a
reset does not need to scan or mutate the window. Snapshot code may read the
per-Zone state and Hot count at adjacent epochs; it records both epoch numbers
instead of pretending the two locks form one atomic global snapshot.

Foreground updates perform work proportional to the changed interval and at
most one activity-window insertion/eviction. Whole-Zone or whole-file scans are
for recovery and explicit background snapshots only.

## Recovery and persistence

M1 adds no on-disk metadata fields. All FragSense state is auxiliary and
rebuildable:

| State | Rebuild rule |
| --- | --- |
| capacity/written/terminal | backend Zone report after mount |
| finalized live identities | replayed ZenFS file/extent mappings |
| provisional active identity | existing active-extent repair path |
| valid blocks/segments/live bytes/count | deterministic rebuild from identities |
| Hot(z) window | empty |
| sealed time/Age(z) | mount completion time/zero for rebuilt sealed Zones |
| generation | new mount epoch plus in-process reset sequence |

Rebuild occurs before sensing snapshots are declared ready. It uses a separate
builder and publishes a complete state set only after all metadata replay and
active-extent repair succeeds. A failed rebuild disables FragSense sensing and
must not prevent Native mount unless the new code has already altered Native
state; no partially built state may drive a decision.

## Formal invariants

For every ready Zone generation:

```text
0 <= live_bytes <= written_bytes <= capacity_bytes
invalid_bytes = written_bytes - live_bytes
0 <= valid_blocks <= ceil(capacity_bytes / B)
0 <= live_segments <= valid_blocks
Frag(z) = 0                              when valid_blocks = 0
Frag(z) = live_segments / valid_blocks  otherwise
0 <= R(z) <= 1
0 <= Hot(z) <= W
Age(z) = 0 when sealed = false
file_extent_count = number of registered finalized extent identities
```

At quiescence:

```text
live_bytes = Zone::used_capacity_
```

A Zone with `live_bytes == 0` is not sent to CFF. It remains eligible only for
the existing `ResetUnusedIOZones()`/`Zone::Reset()` proof and path.

Any invariant failure marks the snapshot invalid, emits a structured diagnostic,
and prevents future policy admission. M1 itself still performs no policy action.

## Required M1 tests derived from this model

- add/finalize/remove extent accounting and idempotent identity handling;
- contiguous add, isolated add, middle split, head/tail removal, whole-run
  removal, and overlapping-block reference counts;
- sparse-header, padding, Zone-boundary, and coarse-block cases;
- successful/short/failed append accounting;
- finish, close-reopenable, full, writer release, failed reset, and successful
  reset lifecycle transitions;
- `kFlush` insertion, `kCompaction` exclusion, unknown-origin diagnostics,
  event eviction, and generation reuse;
- metadata replay plus active-extent repair rebuild;
- restart Hot(z)=0 and Age(z)=0 protection;
- snapshot invariant failures;
- disabled-mode Native regression and foreground overhead microbenchmark.

Passing these tests is a Gate A requirement. This document is a specification,
not evidence that M1 has been implemented or accepted.
