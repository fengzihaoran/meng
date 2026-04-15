struct TenantConfig {
    uint32_t namespace_id;
    TenantType type;                 // 租户类型（写重/读重/混合）
    float max_fragmentation;          // 最大允许碎片度
    uint32_t min_essential_zones;     // 最小essential保证
    uint32_t max_spare_zones;         // 最大可借spare数
    uint64_t latency_sla_us;          // 延迟SLA（服务质量目标）
    uint32_t priority;                // 优先级（1-10）
};

// 根据类型设置不同参数
switch (type) {
    case TenantType::WRITE_HEAVY:
        config.max_fragmentation = 0.35f;  // 写重租户容忍更高碎片
        config.latency_sla_us = 2000;      // 容忍更高延迟
        config.max_spare_zones = 12;       // 可借更多spare
        break;
    case TenantType::READ_HEAVY:
        config.max_fragmentation = 0.25f;  // 读重租户需要更低碎片
        config.latency_sla_us = 500;       // 严格的延迟要求
        config.max_spare_zones = 6;        // 限制借贷保护读性能
        break;
}