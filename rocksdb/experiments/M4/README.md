# M4 LACR

M4 adds LACR (Level/Latency-Aware Compaction-Reorg Coordination，层级/延迟感知的压实-重组协同) on top of the frozen M3-alpha planner.

Scope:

- Compaction state (压实状态): collect RocksDB compaction begin/end metadata through the existing compaction notification path.
- File-zone mapping (文件到分区映射): reuse ZenFS file extent metadata to map SST input files to zone IDs.
- Net adjustment (净收益调整): keep M3 `Net(z)` as the base and apply an M4 adjustment only when both gates are enabled.
- Trace extension (跟踪扩展): preserve M3 trace columns and append LACR fields only when `FACO_LACR_ENABLE=1`.

Gates:

- Compile-time gate (编译期门控): `FACO_ENABLE_LACR=1`
- Runtime gate (运行期门控): `FACO_LACR_ENABLE=1`

With either gate off, M3 behavior is unchanged.

Primary commands:

```bash
bash experiments/M4/run_lacr_tests.sh
bash experiments/M3/run_reorg_tests.sh
```

Do not run `run_lacr_compare.sh` until the full module set is implemented and the device benchmark window is intentional.
