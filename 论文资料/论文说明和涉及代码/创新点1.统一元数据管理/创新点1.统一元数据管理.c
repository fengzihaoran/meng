// 统一元数据结构
typedef struct {
    // eZNS原有字段
    uint32_t vzone_id;
    uint32_t namespace_id;
    uint32_t stripe_width;
    uint32_t congestion_window;
    
    // RBC原有字段
    uint32_t* remapped_blocks;     // 重映射块列表
    uint32_t remap_count;          // 重映射块数量
    float fragmentation_ratio;      // 碎片度
    
    // 统一管理字段
    uint64_t last_access_time;
    uint32_t access_count;
} unified_zone_metadata_t;

// 内存占用对比
// eZNS原shadow view: 2.5MB (4个namespace)
// RBC原remap table: 46-184MB (100-400GB数据)
// 统一后: 节省约20%元数据空间（因消除冗余索引）