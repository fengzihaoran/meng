# M1 CFSM sanity workflow

本目录只保留 M1 当前需要的正式脚本：

- `run_unit_tests.sh`: 构建并运行 `frag_state_table_test`，验证 CFSM 的核心状态表逻辑。
- `run_fillrandom_sanity.sh`: 对 ZNS 设备执行 ZenFS `mkfs`，再用配置好的 `db_bench` workload 验证 RocksDB 可以通过 ZenFS 正常写入。
- `run_fragmentation_workload.sh`: 跑 `fillrandom,overwrite,overwrite,compact,stats`，用于制造无效 SST 和更明显的碎片状态。
- `run_cfsm_compare.sh`: 构建 CFSM 开启/关闭两个 release 版本，跑同样 workload，并输出 CSV/Markdown 对照数据。
- `M1.sh`: 顺序执行单元测试和 fillrandom sanity。
- `common.sh`: 所有 M1 脚本共用的路径、设备、结果目录和执行参数。
- `results/`: 每次运行自动生成的日志目录。

## Why compression_type=none

当前 release 版 `db_bench` 没有链接 Snappy。如果不显式指定压缩类型，`db_bench` 会使用默认 Snappy 并报错：

```text
Compression type Snappy is not linked with the binary.
```

M1 的目的不是测试压缩性能，而是确认 CFSM hook、ZenFS 写入路径和基本日志采集能跑通，所以脚本默认使用：

```bash
--compression_type=none
```

如果后续重新编译并链接了 Snappy，可以用环境变量覆盖：

```bash
COMPRESSION_TYPE=snappy CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fillrandom_sanity.sh
```

## Run M1

从 RocksDB 仓库根目录执行：

```bash
chmod +x experiments/M1/*.sh
CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/M1.sh
```

只跑 fillrandom sanity：

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fillrandom_sanity.sh
```

只跑 CFSM 单元测试：

```bash
experiments/M1/run_unit_tests.sh
```

跑 M1 碎片状态观测 workload：

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fragmentation_workload.sh
```

输出文件：

```text
experiments/M1/results/<timestamp>/faco_cfsm_summary.txt
experiments/M1/results/<timestamp>/faco_cfsm_zones.csv
```

跑 M1 CFSM 开关对照实验：

```bash
CONFIRM_MKFS=1 ZBD=nvme0n1 COMPARE_RUNS=1 experiments/M1/run_cfsm_compare.sh
```

输出文件：

```text
experiments/M1/results/<timestamp>/summary.csv
experiments/M1/results/<timestamp>/summary.md
```

如果已经构建过 `cmake-build-release-cfsm-on` 和 `cmake-build-release-cfsm-off`，可以跳过构建：

```bash
SKIP_BUILD=1 CONFIRM_MKFS=1 ZBD=nvme0n1 experiments/M1/run_cfsm_compare.sh
```

如果只想重用已经 `mkfs` 过的 ZenFS 文件系统：

```bash
SKIP_MKFS=1 ZBD=nvme0n1 experiments/M1/run_fillrandom_sanity.sh
```

如果 `zenfs list` 在当前环境里仍有问题，但 db_bench 已经跑通，可以先跳过 list：

```bash
CONFIRM_MKFS=1 SKIP_LIST=1 ZBD=nvme0n1 experiments/M1/run_fillrandom_sanity.sh
```

## Useful parameters

- `ZBD=nvme0n1`: ZNS 设备名。这里不要写 `/dev/nvme0n1`，ZenFS zbdlib backend 会自己拼 `/dev/`。
- `BENCHMARKS=fillrandom`: db_bench benchmark 列表。
- `NUM=1000000`: db_bench 写入 KV 数量。
- `VALUE_SIZE=1024`: value 大小。
- `COMPRESSION_TYPE=none`: db_bench 压缩类型，默认关闭压缩。
- `ZENFS_ENABLE_GC=0`: 设为 `1` 时，`mkfs` 会带 `--enable_gc`。
- `EXTRA_DB_BENCH_ARGS=...`: 附加传给 db_bench 的参数。
- `ZENFS_AUX_PATH=/home/femu/mnt/zenfs_aux`: ZenFS 辅助文件目录。
- `RESULT_DIR=...`: 指定日志输出目录。
- `RESULT_PURPOSE=...`: 没有显式设置 `RESULT_DIR` 时，结果目录使用
  `YYYYMMDD-HHMMSS-${RESULT_PURPOSE}`，避免只有时间戳。

`run_fillrandom_sanity.sh` 也会透传 `FACO_BUDGET_*` 和 `FACO_REORG_*`
环境变量，供 M2/M3 复用同一个 db_bench runner。

每次执行 fresh `mkfs` 前，脚本会检查 `${ZENFS_AUX_PATH}` 是否为空。如果不为空，会先删除其中旧内容，避免 ZenFS 报：

```text
Aux directory /home/femu/mnt/zenfs_aux is not empty.
```

这个清理只在 `SKIP_MKFS` 不是 `1` 时执行。脚本也会自动创建 `${ZENFS_AUX_PATH}` 下与 `DB_PATH` 对应的父目录，例如默认会创建：

```text
/home/femu/mnt/zenfs_aux/rocksdbtest
```

这是因为 ZenFS 的辅助路径保存 lock/log/metadata 文件，而 RocksDB 打开 DB 时不会递归创建所有上层目录。

## Output

每次运行会生成一个带作用名的目录，例如：

```text
experiments/M1/results/20260428-102509-fillrandom-sanity-v1/
```

重点看这几个文件：

- `db_bench_config.txt`: 本次实验使用的路径、设备和参数。
- `mkfs.log`: ZenFS mkfs 输出。
- `db_bench.log`: db_bench 的吞吐、延迟和错误信息。
- `zenfs_list.log`: ZenFS 文件列表输出。
- `faco_cfsm_summary.txt`: CFSM 汇总状态和 top RBD zone。
- `faco_cfsm_zones.csv`: 每个 zone 的 valid bytes、ZVDR、RBD 和 fragment class。

## Current next step

M1 sanity 跑通后，下一步是做 M1 的对照实验：同一套 workload 下分别运行 CFSM 开启和关闭版本，收集吞吐、延迟、写入放大、GC 行为和空间状态日志。之后再进入 M2/M3/M4 的实现。
