# M1 CFSM 对照实验分析

结果目录：`experiments/M1/results/20260428-122921`

## 实验配置

- 设备：`nvme0n1`
- ZenFS aux：`/home/femu/mnt/zenfs_aux`
- workload：`db_bench fillrandom`
- KV 数量：`1,000,000`
- value size：`1024`
- 压缩：`NoCompression`
- 重复次数：`3`
- 对照版本：
  - `cfsm_on`: `FACO_ENABLE_CFSM=1`
  - `cfsm_off`: `FACO_ENABLE_CFSM=0`

## 汇总结果

| variant | runs | avg micros/op | avg ops/sec | avg MB/s | ops/sec std | ops/sec CV |
|---|---:|---:|---:|---:|---:|---:|
| cfsm_on | 3 | 7.040 | 144026 | 142.867 | 17633.61 | 12.24% |
| cfsm_off | 3 | 6.432 | 155664 | 154.400 | 5578.41 | 3.58% |

相对 `cfsm_off`：

- `cfsm_on` 平均吞吐下降约 `7.48%`。
- `cfsm_on` 平均延迟上升约 `9.45%`。
- `cfsm_on` 平均带宽下降约 `7.47%`。

## 结论

这组结果说明 M1 的 CFSM hook 已经可以在真实 ZenFS + db_bench 路径上运行，并且不会导致功能错误。`mkfs -> db_bench fillrandom -> zenfs list` 全链路通过。

性能上，当前 M1 版本主要是观测和维护状态表，还没有改变 GC 或写入策略，因此它本身不应该期望带来性能收益。当前看到的 `7%~10%` 开销来自 append/delete/reset hook、side table 查询、锁保护和状态更新，这是合理的第一版观测开销。

同时，`cfsm_on` 的第 3 次明显快于前 2 次，导致波动较大，说明 3 次样本还不足以作为最终论文级结论。建议后续至少做 5 到 10 次重复，或者先固定更大的 workload，降低单次运行噪声。

## M1 的作用

M1 做的是 CFSM（Cross-layer Fragmentation State Model），它不是最终优化策略，而是后续 M2/M3/M4 的基础观测层。

当前实现的核心作用：

1. 在 ZenFS zone 维度维护有效字节数。
2. 在文件删除、zone reset、append 路径更新碎片状态。
3. 计算 zone-level validity decay rate，也就是 ZVDR。
4. 计算 remaining bytes density，也就是 RBD。
5. 根据文件跨 zone 分布计算 file fragmentation degree，也就是 FFD。
6. 提供 victim zone 排序能力，为后续 GC/迁移策略使用。

简单说：M1 让系统知道“哪些 zone 正在碎片化、碎片化速度如何、文件跨 zone 分散程度如何”。后面的 M2/M3/M4 才会利用这些信号做调度、预算控制和冷热分离。

## 下一步

不建议直接把这组数据当最终 M1 结果。建议下一步先做两件事：

1. 增加 M1 的内部状态输出，例如输出 zone 总数、tracked valid bytes、ZVDR/RBD top-k victim zones。
2. 增加更能制造碎片的 workload，例如 `fillrandom,overwrite,delrange` 或 `fillrandom,readrandom,overwrite`，而不是只看 fillrandom。

完成这两步后，再进入 M2。M2 才是使用 M1 的碎片状态去做更主动的策略控制。
