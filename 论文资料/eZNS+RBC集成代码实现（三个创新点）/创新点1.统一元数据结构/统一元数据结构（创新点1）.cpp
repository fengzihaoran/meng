#ifndef UNIFIED_METADATA_H
#define UNIFIED_METADATA_H

#include <cstdint>
#include <vector>
#include <atomic>
#include <shared_mutex>

namespace ezns_rbc {

// ============================================================================
// 统一元数据结构（创新点1）
// 融合eZNS的shadow view和RBC的remap table
// ============================================================================

// 物理分区信息（来自eZNS）
struct PhysicalZone {
    uint32_t zone_id;
    uint64_t start_lba;
    uint64_t zone_size;
    uint32_t die_id;
    uint32_t channel_id;
    uint64_t write_pointer;
    bool is_active;
    uint32_t read_count;
    uint32_t write_count;
    bool is_essential;
    bool is_spare;
};

// 重映射表项（来自RBC）
struct RemapEntry {
    uint64_t logical_addr;
    uint64_t physical_addr;
    uint32_t size;
    uint32_t ref_count;
    uint64_t last_access;
    uint32_t source_vzone_id;  // 源v-zone ID（新增）
    
    RemapEntry() : logical_addr(0), physical_addr(0), size(0),
                   ref_count(0), last_access(0), source_vzone_id(0) {}
};

// 统一v-zone元数据（融合两个系统）
struct UnifiedVZoneMeta {
    // eZNS字段
    uint32_t vzone_id;
    uint32_t namespace_id;
    uint32_t stripe_width;
    uint32_t stripe_size;
    uint32_t essential_allocated;
    uint32_t spare_allocated;
    uint32_t congestion_window;
    uint64_t last_read_latency;
    
    // RBC字段
    std::vector<uint64_t> remapped_blocks;  // 重映射块列表
    uint64_t total_blocks;                   // 总块数
    float fragmentation_ratio;                // 碎片度
    
    // 新增协同字段
    uint64_t last_reorg_time;                 // 上次重组时间
    uint32_t borrow_count;                    // 借贷次数
    uint32_t reorg_count;                      // 重组次数
    uint64_t total_read_latency;               // 累计读延迟
    uint32_t read_io_count;                    // 读I/O计数
    
    UnifiedVZoneMeta() 
        : vzone_id(0), namespace_id(0), stripe_width(0), stripe_size(0),
          essential_allocated(0), spare_allocated(0), congestion_window(0),
          last_read_latency(0), total_blocks(0), fragmentation_ratio(0.0f),
          last_reorg_time(0), borrow_count(0), reorg_count(0),
          total_read_latency(0), read_io_count(0) {}
    
    // 获取平均读延迟
    double get_avg_read_latency() const {
        if (read_io_count == 0) return 0.0;
        return static_cast<double>(total_read_latency) / read_io_count;
    }
    
    // 更新碎片度
    void update_fragmentation() {
        if (total_blocks > 0) {
            fragmentation_ratio = static_cast<float>(remapped_blocks.size()) / total_blocks;
        }
    }
    
    // 添加重映射块
    void add_remapped_block(uint64_t block_addr) {
        remapped_blocks.push_back(block_addr);
        update_fragmentation();
    }
    
    // 移除重映射块（重组后）
    void remove_remapped_block(uint64_t block_addr) {
        auto it = std::find(remapped_blocks.begin(), remapped_blocks.end(), block_addr);
        if (it != remapped_blocks.end()) {
            remapped_blocks.erase(it);
            update_fragmentation();
        }
    }
};

// ============================================================================
// 统一元数据管理器
// ============================================================================
class UnifiedMetadataManager {
public:
    UnifiedMetadataManager();
    ~UnifiedMetadataManager();
    
    // 初始化
    bool init(uint64_t total_data_size, uint32_t num_namespaces);
    
    // v-zone管理
    UnifiedVZoneMeta* create_vzone(uint32_t namespace_id, uint32_t stripe_width);
    UnifiedVZoneMeta* get_vzone(uint32_t vzone_id);
    bool delete_vzone(uint32_t vzone_id);
    
    // 重映射表操作
    bool add_remapping(uint64_t logical_addr, uint64_t physical_addr, 
                       uint32_t size, uint32_t source_vzone);
    bool lookup_remapping(uint64_t logical_addr, uint64_t& physical_addr,
                          uint32_t& size, uint32_t& source_vzone);
    bool remove_remapping(uint64_t logical_addr);
    
    // 访问统计更新
    void update_read_latency(uint32_t vzone_id, uint64_t latency_us);
    void record_borrow(uint32_t vzone_id, uint32_t amount);
    void record_reorg(uint32_t vzone_id);
    
    // 统计信息
    struct Stats {
        uint64_t total_vzones;
        uint64_t total_remapped_blocks;
        float avg_fragmentation;
        uint64_t memory_usage_bytes;
        uint64_t total_lookups;
        double avg_lookup_latency_us;
    };
    
    Stats get_stats() const;
    void dump_stats() const;

private:
    // v-zone元数据映射
    std::unordered_map<uint32_t, UnifiedVZoneMeta> vzones_;
    
    // 重映射表（逻辑地址 -> 物理地址）
    std::unordered_map<uint64_t, RemapEntry> remap_table_;
    
    // 读写锁
    mutable std::shared_mutex rw_mutex_;
    
    // 统计
    mutable std::atomic<uint64_t> total_lookups_{0};
    mutable std::atomic<uint64_t> total_lookup_time_us_{0};
    
    uint64_t get_current_us() const;
};

} // namespace ezns_rbc

#endif // UNIFIED_METADATA_H