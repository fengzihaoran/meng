struct UnifiedVZoneMeta {
    // eZNS字段 - 用于资源管理和调度
    uint32_t vzone_id;           // 虚拟分区ID，唯一标识
    uint32_t namespace_id;       // 所属租户ID，用于多租户隔离
    uint32_t stripe_width;       // 条带宽度，决定并行度
    uint32_t stripe_size;        // 条带大小，影响I/O粒度
    uint32_t essential_allocated;// 已分配的essential zones（保证资源）
    uint32_t spare_allocated;    // 已分配的spare zones（弹性资源）
    uint32_t congestion_window;  // 拥塞窗口（读调度用）
    
    // RBC字段 - 用于碎片管理
    std::vector<uint64_t> remapped_blocks;  // 重映射块列表
    uint64_t total_blocks;                   // 总块数
    float fragmentation_ratio;                // 碎片度 = remapped/total
    
    // 新增协同字段 - 两个系统交互
    uint64_t last_reorg_time;     // 上次重组时间，避免频繁重组
    uint32_t borrow_count;        // 借贷次数，反映弹性使用情况
    uint32_t reorg_count;         // 重组次数，反映碎片管理频率
};