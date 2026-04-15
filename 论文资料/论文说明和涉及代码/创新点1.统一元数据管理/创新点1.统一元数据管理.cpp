// 统一元数据结构
struct UnifiedZoneMetadata {
    // eZNS原有字段
    uint32_t vzone_id;
    uint32_t namespace_id;
    uint32_t stripe_width;
    uint32_t congestion_window;
    
    // RBC原有字段
    std::vector<uint32_t> remapped_blocks;  // 重映射块列表（使用vector自动管理大小）
    float fragmentation_ratio;               // 碎片度
    
    // 统一管理字段
    uint64_t last_access_time;
    uint32_t access_count;

    // 构造函数
    UnifiedZoneMetadata()
        : vzone_id(0), namespace_id(0), stripe_width(0), congestion_window(0),
          fragmentation_ratio(0.0f), last_access_time(0), access_count(0) {}

    // 带参数的构造函数
    UnifiedZoneMetadata(uint32_t vzone, uint32_t ns_id, uint32_t stripe, uint32_t cong_win)
        : vzone_id(vzone), namespace_id(ns_id), stripe_width(stripe), congestion_window(cong_win),
          fragmentation_ratio(0.0f), last_access_time(0), access_count(0) {}

    // 获取重映射块数量（替代原来的remap_count字段）
    uint32_t getRemapCount() const {
        return static_cast<uint32_t>(remapped_blocks.size());
    }

    // 添加重映射块
    void addRemappedBlock(uint32_t block) {
        remapped_blocks.push_back(block);
    }

    // 移除重映射块
    void removeRemappedBlock(uint32_t block) {
        auto it = std::find(remapped_blocks.begin(), remapped_blocks.end(), block);
        if (it != remapped_blocks.end()) {
            remapped_blocks.erase(it);
        }
    }
};

// 内存占用对比
// eZNS原shadow view: 2.5MB (4个namespace)
// RBC原remap table: 46-184MB (100-400GB数据)
// 统一后: 节省约20%元数据空间（因消除冗余索引）