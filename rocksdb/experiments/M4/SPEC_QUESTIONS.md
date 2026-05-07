# M4 LACR Spec Questions

These are the questions that should be answered before performance tuning.

1. Event source (事件来源): is DB-level compaction notification sufficient, or do we need a user-visible `EventListener` (事件监听器) object for external deployments?
2. Filename normalization (文件名规范化): should LACR rely on SST basenames, full DB paths, or both when matching RocksDB compaction files to ZenFS files?
3. Invalidation attribution (失效归因): is `total_input_bytes - total_output_bytes` a good enough proxy for bytes invalidated by compaction, or should it use table properties (表属性) later?
4. Latency penalty (延迟惩罚): should the first production model use active compaction bytes, compaction file count, or measured foreground latency?
5. Synergy window (协同窗口): should recent compaction invalidation decay by time, by GC cycle, or by successful reorg execution?
6. Ablation (消融实验): compare M3, M4 without synergy, M4 without waste penalty, and full M4 after all modules are implemented.
