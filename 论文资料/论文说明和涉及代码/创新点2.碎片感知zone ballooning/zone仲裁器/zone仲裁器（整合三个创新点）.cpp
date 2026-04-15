#ifndef EZNS_RBC_ZONE_ARBITER_H
#define EZNS_RBC_ZONE_ARBITER_H

#include "common.h"
#include "unified_metadata.h"
#include "frag_aware_balloon.h"
#include "tenant_isolation.h"

namespace ezns_rbc {

// ============================================================================
// zone仲裁器（整合三个创新点）
// ============================================================================

class ZoneArbiter {
public:
    ZoneArbiter();
    ~ZoneArbiter();
    
    // 初始化
    bool init(uint32_t total_zones, uint32_t max_active_zones);
    
    // 获取各组件
    UnifiedMetadataManager* get_metadata_manager() { return metadata_mgr_.get(); }
    FragAwareBalloon* get_balloon() { return balloon_.get(); }
    TenantIsolation* get_isolation() { return isolation_.get(); }
    
    // zone分配接口
    uint32_t allocate_vzone(uint32_t namespace_id, uint32_t requested_width);
    bool free_vzone(uint32_t vzone_id);
    
    // 处理I/O请求
    void handle_read(uint32_t vzone_id, uint64_t latency_us);
    void handle_write(uint32_t vzone_id, uint64_t latency_us);
    
    // 触发重组
    void trigger_reorganization(uint32_t vzone_id);
    
    // 获取统计信息
    void get_stats(uint64_t& total_io, uint64_t& total_borrow,
                   uint64_t& total_reorg, double& fairness) const;
    
    // 调试输出
    void dump_stats(std::ostream& os = std::cout) const;

private:
    std::unique_ptr<UnifiedMetadataManager> metadata_mgr_;
    std::unique_ptr<FragAwareBalloon> balloon_;
    std::unique_ptr<TenantIsolation> isolation_;
    
    // 全局配置
    uint32_t total_zones_;
    uint32_t max_active_zones_;
    uint32_t essential_per_namespace_;
    uint32_t spare_per_namespace_;
    
    // 统计
    struct {
        std::atomic<uint64_t> total_io_requests{0};
        std::atomic<uint64_t> total_reads{0};
        std::atomic<uint64_t> total_writes{0};
        std::atomic<uint64_t> total_borrow_events{0};
        std::atomic<uint64_t> total_reorg_events{0};
    } stats_;
};

} // namespace ezns_rbc

#endif // EZNS_RBC_ZONE_ARBITER_H