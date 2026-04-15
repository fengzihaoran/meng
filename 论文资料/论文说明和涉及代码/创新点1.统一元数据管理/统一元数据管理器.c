// 统一元数据管理器
typedef struct {
    // eZNS原有字段
    uint32_t vzone_id;
    uint32_t namespace_id;
    uint32_t stripe_width;
    uint32_t essential_allocated;
    uint32_t spare_allocated;
    
    // RBC原有字段
    uint32_t* remapped_blocks;     // 重映射块列表
    uint32_t remap_count;          // 重映射块数量
    uint32_t total_blocks;         // 总块数
    float fragmentation_ratio;      // 碎片度 = remap_count / total_blocks
    
    // 新增协同字段
    uint64_t last_reorg_time;       // 上次重组时间
    uint32_t reorg_count;           // 重组次数
    uint32_t borrow_count;          // 借贷次数
} unified_zone_t;