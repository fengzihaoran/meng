// 统一元数据结构
typedef struct {
    // eZNS字段
    uint32_t vzone_id;
    uint32_t namespace_id;
    uint32_t stripe_width;
    uint32_t essential_alloc;
    uint32_t spare_alloc;
    uint32_t congestion_window;
    
    // RBC字段
    uint32_t* remapped_blocks;
    uint32_t remap_count;
    uint32_t total_blocks;
    float frag_ratio;
    
    // 新增字段
    uint64_t last_access;
    uint32_t access_count;
    uint32_t borrow_count;
} unified_metadata_t;

// 内存节省计算公式
memory_saved = (shadow_view_size + remap_table_size) - unified_size