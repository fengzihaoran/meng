# M1 CFSM 10-run 对照实验分析

结果目录：`experiments/M1/results/20260428-123529`

## 结果

| variant | runs | avg micros/op | avg ops/sec | avg MB/s | ops/sec std | ops/sec CV |
|---|---:|---:|---:|---:|---:|---:|
| cfsm_on | 10 | 7.158 | 140471 | 139.320 | 10089.03 | 7.18% |
| cfsm_off | 10 | 7.076 | 141995 | 140.820 | 10033.74 | 7.07% |

相对 `cfsm_off`：

- `cfsm_on` 平均吞吐下降约 `1.07%`。
- `cfsm_on` 平均延迟上升约 `1.16%`。
- `cfsm_on` 平均带宽下降约 `1.07%`。

## 判断

这组 10 次结果比 3 次结果更可信。两组的 CV 都在 7% 左右，而均值差异只有约 1%，说明当前 M1 的 CFSM 观测开销很小，已经接近环境噪声范围。

因此 M1 当前可以证明两件事：

1. CFSM on/off 两个版本都能在真实 ZenFS + db_bench 路径上稳定运行。
2. CFSM 状态维护的直接性能成本较低，不会明显拖慢纯写入路径。

但这组 workload 仍然主要是 `fillrandom`，它对碎片状态的证明不足。M1 的真正价值不是提升 fillrandom 吞吐，而是输出 zone 级碎片信号，给后续 GC/迁移策略使用。

## 下一步

已经补充代码，让 ZenFS 在关闭时导出 CFSM 状态：

- `faco_cfsm_summary.txt`
- `faco_cfsm_zones.csv`

下一步应运行碎片 workload，验证 CFSM 能观察到有效字节下降、RBD 差异和 top victim zones：

```bash
cd ~/rocksdb
cmake --build cmake-build-release-cfsm-on --target db_bench zenfs_tool -j$(nproc)
BUILD_RELEASE_DIR=$PWD/cmake-build-release-cfsm-on \
DB_BENCH=$PWD/cmake-build-release-cfsm-on/db_bench \
ZENFS_TOOL=$PWD/cmake-build-release-cfsm-on/zenfs \
CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fragmentation_workload.sh
```
