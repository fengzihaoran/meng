# M0.5 Engineering Gate Report

## Scope

M0.5 performs repository governance, provenance capture, build-gate setup, and
formal M1 state definition. It contains no FragSense production implementation
and no changes under `plugin/zenfs`.

## Changed files

- `/AGENTS.md`
- `docs/fragsense/IMPLEMENTATION_PLAN.md`
- `docs/fragsense/CODEX_RUNBOOK.md`
- `docs/fragsense/ZENFS_PROVENANCE.md`
- `docs/fragsense/M1_STATE_MODEL.md`
- `docs/fragsense/M05_REPORT.md`
- `scripts/fragsense/build_linux.sh`
- `/.gitignore`

The temporary `fragsense_codex_56_sol_bundle` directory and its ZIP archive are
not part of the M0.5 commit. Python bytecode and bundle paths are ignored.

## Build status

**BLOCKED in the current Windows environment.** ZenFS requires Linux and
`libzbd >= 1.5.0`. M0.5 does not fabricate a Linux build result from copied or
incremental artifacts.

Run on the FEMU/Ubuntu RocksDB checkout:

```bash
cd /home/femu/rocksdb
bash scripts/fragsense/build_linux.sh --probe-only
bash scripts/fragsense/build_linux.sh --jobs "$(nproc)"
```

The probe accepts ZenFS either as a tracked vendored subtree or as the
independent nested Git repository used by the official ZenFS installation
workflow. It rejects an untracked, non-repository `plugin/zenfs` directory
because that layout cannot provide exact source provenance.

The script performs a clean CMake configure and builds `rocksdb-shared`,
`zenfs_tool`, `db_bench`, `db_basic_test`, `env_basic_test`, and `c_test`. It
then discovers and runs the configured non-device CTest suite from the build
directory. Zero discovered tests is a gate failure, not a pass. Commands,
environment, repository state, logs, build/test statuses, and final status are
saved under:

```text
cmake-build-fragsense-m05/fragsense-m05-evidence/
```

The script performs no `mkfs`, Zone Reset, or block-device write.

## Test status

The repository-side acceptance checks are:

- Bash syntax and help output for `build_linux.sh`;
- `git diff --check`;
- required-document and baseline-matrix checks;
- proof that the M0.5 diff contains no `plugin/zenfs` path;
- proof that temporary bundle and bytecode files are excluded from the commit.

The clean Linux build and CTest result remain open until the evidence directory
from a Linux run is reviewed. Device integration and active-GC tests remain
unrun because they are destructive and require an explicitly approved
disposable target.

### Rejected FEMU attempt: 2026-07-14

The first FEMU run compiled `rocksdb-shared`, `zenfs_tool`, and `db_bench`, but
does not satisfy the gate:

- CTest printed `No tests were found!!!`; the old script incorrectly wrote
  `PASSED` because CTest returned zero;
- CMake/CTest 3.16 ran the old `--test-dir` invocation from the source directory
  instead of the intended build directory;
- RocksDB commit `c2467b141e840fdba5b3a1810763043e56449fb9` was detached and
  had tracked/untracked source changes;
- nested ZenFS commit `919c2ebbcdc170525a9abffb8b61a3795b1e6ae5` also had extensive
  tracked/untracked changes.

This is useful compile evidence for that dirty working tree, but it is not a
reproducible clean-source M0.5 result. The gate remains `BLOCKED`.

## Unresolved blockers

1. Clean Linux build/CTest evidence is not yet attached to this commit.
2. The vendored ZenFS import has no submodule/subtree history; provenance is a
   candidate commit plus a reproducible tree comparison, not an exact import
   proof.
3. The current observer adds disabled-mode callback branches and has
   unsynchronized/approximate fields; accepting or replacing that overhead is
   a Gate A decision.
4. Existing active-GC migration ordering remains unsafe and cannot be reused by
   M3 without a separate transaction/durability repair.

## M1 start decision

M1 production code must not start until a human reviews
`M1_STATE_MODEL.md` and the clean Linux build reports `PASSED`. After those two
conditions, M1 may start as sensing-only work. Selection, migration, new reset
behavior, elastic control, and layout regrouping remain out of scope.
