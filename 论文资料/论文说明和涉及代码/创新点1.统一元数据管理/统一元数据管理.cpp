#ifndef EZNS_RBC_UNIFIED_METADATA_H
#define EZNS_RBC_UNIFIED_METADATA_H

#include "common.h"
#include <unordered_map>
#include <shared_mutex>

namespace ezns_rbc {

// ============================================================================
// 统一元数据结构（创新点1）
// 融合eZNS的shadow view和RBC的remap table
// ============================================================================

struct UnifiedMetadata {
    // eZNS字段
    uint32_t vzone_id;              // 虚拟zone ID
    uint32_t namespace_id;          // 所属namespace
    uint32_t stripe_width;          // stripe宽度
    uint32_t stripe_size;           // stripe大小（字节）
    uint32_t essential_allocated;   // 已分配的essential zones
    uint32_t spare_allocated;        // 已分配的spare zones
    uint32_t congestion_window;      // 拥塞窗口
    uint64_t last_read_latency;      // 最后一次读延迟
    
    // RBC字段
    std::vector<uint32_t> remapped_blocks;  // 重映射块列表
    uint32_t total_blocks;                   // 总块数
    float fragmentation_ratio;                // 碎片度
    
    // 新增协同字段
    uint64_t last_access_time;        // 最后一次访问时间
    uint32_t access_count;            // 总访问次数
    uint32_t borrow_count;             // 借贷次数
    uint32_t reorg_count;              // 重组次数
    uint64_t total_read_latency;       // 累计读延迟
    uint32_t read_io_count;            // 读I/O计数
    
    // 构造函数
    UnifiedMetadata() 
        : vzone_id(0), namespace_id(0), stripe_width(0), stripe_size(0),
          essential_allocated(0), spare_allocated(0), congestion_window(0),
          last_read_latency(0), total_blocks(0), fragmentation_ratio(0.0f),
          last_access_time(0), access_count(0), borrow_count(0), reorg_count(0),
          total_read_latency(0), read_io_count(0) {}
    
    // 计算平均读延迟
    float get_avg_read_latency() const {
        if (read_io_count == 0) return 0.0f;
        return static_cast<float>(total_read_latency) / read_io_count;
    }
    
    // 更新碎片度
    void update_fragmentation() {
        if (total_blocks > 0) {
            fragmentation_ratio = static_cast<float>(remapped_blocks.size()) / total_blocks;
        } else {
            fragmentation_ratio = 0.0f;
        }
    }
    
    // 添加重映射块
    void add_remapped_block(uint32_t block_id) {
        remapped_blocks.push_back(block_id);
        update_fragmentation();
    }
    
    // 移除重映射块（重组后）
    void remove_remapped_block(uint32_t block_id) {
        auto it = std::find(remapped_blocks.begin(), remapped_blocks.end(), block_id);
        if (it != remapped_blocks.end()) {
            remapped_blocks.erase(it);
            update_fragmentation();
        }
    }
};

// ============================================================================
// 统一元数据管理器类
// ============================================================================

class UnifiedMetadataManager {
public:
    UnifiedMetadataManager();
    ~UnifiedMetadataManager();
    
    // 初始化
    bool init(const std::string& device_path, uint32_t num_namespaces);
    
    // 获取/创建元数据
    UnifiedMetadata* get_metadata(uint32_t vzone_id);
    UnifiedMetadata* create_metadata(uint32_t namespace_id, uint32_t stripe_width);
    
    // 更新操作
    void update_access(uint32_t vzone_id, IOType type, uint64_t latency_us);
    void record_borrow(uint32_t vzone_id, uint32_t amount);
    void record_reorg(uint32_t vzone_id);
    
    // 碎片相关
    float get_fragmentation(uint32_t vzone_id) const;
    void add_remapped_block(uint32_t vzone_id, uint32_t block_id);
    void remove_remapped_block(uint32_t vzone_id, uint32_t block_id);
    
    // 统计信息
    struct Stats {
        uint64_t total_metadata_memory;  // 总内存占用（字节）
        uint32_t total_vzones;            // 总vzone数
        uint32_t total_remapped_blocks;   // 总重映射块数
        float avg_fragmentation;           // 平均碎片度
        uint64_t total_lookups;            // 总查询次数
        double avg_lookup_latency_us;      // 平均查询延迟
    };
    
    Stats get_stats() const;
    
    // 调试输出
    void dump_stats(std::ostream& os = std::cout) const;

private:
    // 存储所有元数据
    std::unordered_map<uint32_t, UnifiedMetadata> metadata_map_;
    
    // 读写锁（读多写少）
    mutable std::shared_mutex rw_mutex_;
    
    // 统计信息
    mutable std::atomic<uint64_t> total_lookups_{0};
    mutable std::atomic<uint64_t> total_lookup_time_us_{0};
    
    // 辅助函数
    uint64_t estimate_memory_usage() const;
};

} // namespace ezns_rbc

#endif // EZNS_RBC_UNIFIED_METADATA_H