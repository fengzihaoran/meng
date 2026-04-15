#ifndef UNIFIED_CONTROLLER_H
#define UNIFIED_CONTROLLER_H

#include "unified_metadata.h"
#include "frag_aware_balloon.h"
#include "tenant_isolation.h"
#include <thread>
#include <atomic>

namespace ezns_rbc {

// ============================================================================
// eZNS+RBC统一控制器
// 集成三个创新点
// ============================================================================
class UnifiedController {
public:
    UnifiedController();
    ~UnifiedController();
    
    // 初始化
    bool init(uint32_t total_zones, uint32_t max_active_zones, uint64_t total_data_size);
    
    // 租户管理
    bool register_tenant(uint32_t namespace_id, TenantType type, uint32_t priority);
    
    // 分配v-zone（整合三个创新点）
    uint32_t allocate_vzone(uint32_t namespace_id, uint32_t requested_width);
    bool free_vzone(uint32_t vzone_id);
    
    // 处理I/O（更新元数据）
    void handle_read(uint32_t vzone_id, uint64_t lba, uint64_t latency_us);
    void handle_write(uint32_t vzone_id, uint64_t lba, uint64_t physical_addr,
                      uint32_t size, bool is_dirty);
    
    // 触发碎片重组
    void trigger_reorganization(uint32_t vzone_id);
    
    // 获取统计信息
    void print_stats() const;

private:
    UnifiedMetadataManager metadata_mgr_;
    FragAwareBalloon balloon_;
    TenantIsolation isolation_;
    
    // 配置
    uint32_t total_zones_;
    uint32_t max_active_zones_;
    
    // 统计
    struct {
        std::atomic<uint64_t> total_reads{0};
        std::atomic<uint64_t> total_writes{0};
        std::atomic<uint64_t> total_allocations{0};
        std::atomic<uint64_t> total_reorgs{0};
    } stats_;
    
    // 后台重组线程
    std::thread reorg_thread_;
    std::atomic<bool> running_{false};
    
    void reorg_worker();
};

} // namespace ezns_rbc

#endif // UNIFIED_CONTROLLER_H