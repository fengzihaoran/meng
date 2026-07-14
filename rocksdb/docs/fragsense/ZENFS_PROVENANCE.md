# ZenFS Provenance and Existing Active-GC Baseline

## Scope and conclusion

This repository vendors ZenFS under `plugin/zenfs`; it is not a Git submodule
and has no subtree metadata or imported patch series. The generated and ignored
`plugin/zenfs/fs/version.h` reports:

```text
v2.1.0-65-g919c2eb-dirty
```

That string identifies an upstream candidate, not an exact source proof. The
official ZenFS object exists at commit
`919c2ebbcdc170525a9abffb8b61a3795b1e6ae5`, authored on 2025-01-21 with subject
`fs: fix compile error on latest RocksDB`. The current vendored tree differs
from that object. Exact historical equivalence before the local changes cannot
be proved from this repository because the import metadata is absent.

Official source:

- Repository: <https://github.com/westerndigitalcorporation/zenfs>
- Candidate commit:
  <https://github.com/westerndigitalcorporation/zenfs/commit/919c2ebbcdc170525a9abffb8b61a3795b1e6ae5>

## RocksDB and repository revisions

The M0.5 comparison uses these revisions:

| Item | Revision/tree |
| --- | --- |
| RocksDB version | `8.11.3` from `include/rocksdb/version.h` |
| repository baseline | `d7d37643a610d19d58c714763737177ea6b75be3` (`main`) |
| M0 audit base | `145f2b88e705ee8ea856e16865152b587a140b0a` |
| upstream ZenFS candidate | `919c2ebbcdc170525a9abffb8b61a3795b1e6ae5` |
| upstream candidate tree | `01d81d303daefde9345156e8ffc30e770fdea365` |
| baseline vendored ZenFS tree | `0677da6472f5184a15915da8f0ee64abbc228454` |
| M0 vendored ZenFS tree | `b9100eb0acc74ba330b9479b97837d6cfb445169` |

The on-disk ZenFS superblock format is version 2. The software version and
on-disk format version are different identifiers and must not be conflated.

## Reproducible comparison

Fetch the exact upstream object without adding a persistent remote:

```bash
git fetch --no-tags \
  https://github.com/westerndigitalcorporation/zenfs.git \
  919c2ebbcdc170525a9abffb8b61a3795b1e6ae5
```

Compare the upstream root tree with the vendored ZenFS subtree:

```bash
UPSTREAM=919c2ebbcdc170525a9abffb8b61a3795b1e6ae5

git rev-parse "$UPSTREAM^{tree}"
git rev-parse main:rocksdb/plugin/zenfs
git rev-parse HEAD:rocksdb/plugin/zenfs

git diff --shortstat \
  "$UPSTREAM^{tree}" main:rocksdb/plugin/zenfs
git diff --name-status \
  "$UPSTREAM^{tree}" main:rocksdb/plugin/zenfs
git diff --summary \
  "$UPSTREAM^{tree}" main:rocksdb/plugin/zenfs

git diff --shortstat main HEAD -- rocksdb/plugin/zenfs
git diff --name-status main HEAD -- rocksdb/plugin/zenfs
```

Observed at M0.5:

| Comparison | Result |
| --- | --- |
| upstream candidate to repository baseline | 32 paths, 171 insertions, 167 deletions |
| repository baseline to M0 audit base | 6 files, 543 insertions, 6 deletions |

The upstream-to-baseline difference contains several distinct classes and must
not be described as one FragSense patch:

- a committed generated `plugin/zenfs/Makefile` containing historical
  `/home/femu/rocksdb` paths;
- local utility build changes in `util/Makefile` and `util/zenfs.cc`;
- deletion of `util/zenfs.8`;
- `.gitignore` and end-of-file differences;
- executable-bit removal from shell and Python scripts;
- local changes in `fs/snapshot.h`, `fs/zbd_zenfs.cc`, and
  `fs/zbd_zenfs.h`;
- a later six-file motivation observer patch in
  `fs/fs_zenfs.{h,cc}`, `fs/io_zenfs.cc`, `fs/snapshot.h`, and
  `fs/zbd_zenfs.{h,cc}`.

For a content patch that excludes mode-only changes, run:

```bash
git diff --no-ext-diff --binary \
  "$UPSTREAM^{tree}" main:rocksdb/plugin/zenfs \
  > zenfs-upstream-to-baseline.patch

git diff --no-ext-diff --binary main HEAD -- rocksdb/plugin/zenfs \
  > zenfs-baseline-to-current.patch
```

The generated patch files are evidence artifacts and must not be committed
without review.

The Linux build script also supports the official installation layout where
`plugin/zenfs` is an independent nested Git repository. In that case the
evidence records the RocksDB commit/status and the ZenFS commit/tree/status
separately. Run `scripts/fragsense/build_linux.sh --probe-only` before the build
to verify which layout was detected. An untracked ZenFS directory without its
own Git metadata is rejected rather than assigned fabricated provenance.

The build gate also rejects a zero-test CTest discovery. CMake 3.16 does not
reliably support the newer `ctest --test-dir` invocation used by the first
script revision, so the script uses `cmake -E chdir <build> ctest ...` and
records the discovered test count before execution.

## Existing optional active GC

### Enablement and trigger

`zenfs mkfs --enable_gc` persists `Superblock::FLAGS_ENABLE_GC`. The command
default is `false`. On a writable mount, `ZenFS::Mount()` starts
`ZenFS::GCWorker()` only when the persisted bit is set.

The worker:

1. sleeps for 10 seconds per cycle;
2. computes free-space percentage from ZenFS counters;
3. does nothing while free space is above `GC_START_LEVEL=20` percent;
4. computes `threshold = 100 - 3 * (20 - free_percent)`;
5. considers only full Zones (`capacity == 0`);
6. selects Zones whose approximate garbage percentage is strictly greater
   than the threshold and strictly less than 100 percent.

Completely invalid Zones are excluded from migration because the existing Lazy
Reset path can reset them without copying live data.

### Victim and extent policy

The victim score is approximate garbage percentage:

```text
100 - 100 * used_capacity / max_capacity
```

There is no Hot(z), age protection, fragmentation score, cost density, or
compaction-pressure input. Extents from selected Zones are grouped by filename,
and only filenames ending in `.sst` are migrated.

### Copy, mapping, and reset ordering

For each selected file, the current implementation:

1. gets the file and acquires its write-reopen lock;
2. clones the current in-memory extent vector;
3. allocates a migration Zone for each selected extent;
4. copies data to the destination;
5. mutates the cloned extent to point to the destination and increments its
   live-capacity accounting;
6. calls `SyncFileExtents()`;
7. releases the file lock;
8. calls `ResetUnusedIOZones()` after each migrated file.

`SyncFileExtents()` currently publishes the replacement extent vector before
writing the `kFileReplace` metadata record. Only after the metadata call
succeeds does it decrement old-Zone accounting. `ResetUnusedIOZones()` resets a
Zone only when it is non-empty and `used_capacity_ == 0`.

### Known safety risks

This active-GC path is not a safe implementation base for M3 without repair:

- `ZoneFile::MigrateData()` write status and short I/O are not enforced by the
  caller;
- some allocation/copy failures continue with a partial replacement vector;
- `GetFileNoLock()` is called without a proven `files_mtx_` ownership at the
  late existence check;
- `SyncFileExtents()` publishes in memory before persistence;
- metadata failure leaves the new in-memory mapping published;
- `MigrateFileExtents()` ignores `SyncFileExtents()` status and returns OK;
- the write-reopen lock does not define a complete delete/migrate protocol;
- no explicit metadata flush/FUA/barrier proves durability before source reset;
- `run_gc_worker_` is a plain cross-thread `bool`.

No destructive active-GC test was run during M0.5.

## Future baseline matrix

The paper evaluation must retain these distinct entries:

| Baseline | Required configuration |
| --- | --- |
| Native ZenFS | `--enable_gc=false`, FragSense disabled |
| Upstream Active-GC | `--enable_gc=true`, FragSense disabled |
| Reclaimable-space Greedy | common hardened migrator, reclaimable policy |
| Valid-ratio Greedy | common hardened migrator, valid-ratio policy |
| Frag-only | common hardened migrator, fragmentation policy |
| CFF-only | common hardened migrator, fixed budget |
| Full FragSense | CFF, filters, elastic budget, and I/O coordination |

The Active-GC row may be reported as unavailable only after a disposable ZNS
test records a reproducible correctness or stability failure. Its existence
must not be omitted from the Native ZenFS description.

## Remaining provenance limitations

- There is no submodule pointer, subtree marker, or upstream patch series.
- The ignored generated version header can be regenerated from a dirty working
  tree and therefore cannot prove byte identity.
- The committed generated Makefile and mode changes obscure the original import
  process.
- Tree comparison proves current differences from the candidate commit; it does
  not prove that the candidate commit was the actual historical import base.
