#ifndef RBC_REMAP_H
#define RBC_REMAP_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

namespace rbc {

// ============================================================================
// 重映射表项（论文Section 3.2.1）
// ============================================================================
struct RemapEntry {
    uint64_t logical_addr;      // 逻辑地址
    uint64_t physical_addr;     // 物理地址
    uint32_t size;              // 块大小
    uint32_t ref_count;         // 引用计数
    uint64_t last_access;        // 最后访问时间
    
    RemapEntry() : logical_addr(0), physical_addr(0), 
                   size(0), ref_count(0), last_access(0) {}
};

// ============================================================================
// 重映射表管理器
// ============================================================================
class RemapTable {
public:
    RemapTable();
    ~RemapTable();
    
    // 初始化
    bool init(uint64_t max_entries);
    
    // 添加重映射
    bool add_remapping(uint64_t logical_addr, uint64_t physical_addr, uint32_t size);
    
    // 查询重映射
    bool lookup(uint64_t logical_addr, uint64_t& physical_addr, uint32_t& size);
    
    // 删除重映射
    bool remove_remapping(uint64_t logical_addr);
    
    // 获取碎片度
    float get_fragmentation_ratio() const;
    
    // 统计信息
    struct Stats {
        uint64_t total_entries;
        uint64_t total_lookups;
        uint64_t cache_hits;
        uint64_t memory_usage_bytes;
    };
    
    Stats get_stats() const;

private:
    std::unordered_map<uint64_t, RemapEntry> table_;
    mutable std::shared_mutex rw_mutex_;
    
    uint64_t max_entries_;
    uint64_t total_remapped_blocks_;
    
    // 统计
    mutable std::atomic<uint64_t> total_lookups_{0};
    mutable std::atomic<uint64_t> cache_hits_{0};
};

} // namespace rbc

#endif // RBC_REMAP_H