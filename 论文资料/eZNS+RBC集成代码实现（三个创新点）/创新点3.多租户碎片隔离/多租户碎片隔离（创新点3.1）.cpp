#ifndef TENANT_ISOLATION_H
#define TENANT_ISOLATION_H

#include "unified_metadata.h"
#include <vector>

namespace ezns_rbc {

// ============================================================================
// 多租户碎片隔离（创新点3）
// 保证一个租户的碎片不会影响其他租户的性能
// ============================================================================

// 租户类型
enum class TenantType {
    WRITE_HEAVY,
    READ_HEAVY,
    MIXED
};

// 租户配置
struct TenantConfig {
    uint32_t namespace_id;
    TenantType type;
    float max_fragmentation;        // 最大允许碎片度
    uint32_t min_essential_zones;    // 最小essential zones保证
    uint32_t max_spare_zones;        // 最大可借spare zones
    uint64_t latency_sla_us;         // 延迟SLA（us）
    uint32_t priority;               // 优先级（1-10）
    
    TenantConfig() : namespace_id(0), type(TenantType::MIXED),
                     max_fragmentation(FRAG_THRESHOLD),
                     min_essential_zones(2), max_spare_zones(8),
                     latency_sla_us(1000), priority(5) {}
};

// 租户状态
struct TenantStatus {
    uint32_t namespace_id;
    TenantType type;
    float current_fragmentation;
    uint32_t essential_allocated;
    uint32_t spare_allocated;
    uint64_t avg_read_latency_us;
    uint64_t p99_read_latency_us;
    uint32_t active_vzones;
    bool is_throttled;
    std::string throttling_reason;
};

// 借贷决策
struct BorrowDecision {
    bool allowed;
    uint32_t max_borrow;
    std::string reason;
};

// ============================================================================
// 多租户隔离管理器
// ============================================================================
class TenantIsolation {
public:
    explicit TenantIsolation(UnifiedMetadataManager* metadata_mgr);
    ~TenantIsolation();
    
    // 注册租户
    bool register_tenant(const TenantConfig& config);
    
    // 获取租户状态
    TenantStatus get_tenant_status(uint32_t namespace_id) const;
    
    // 检查借贷权限（核心隔离逻辑）
    BorrowDecision check_borrow_permission(uint32_t lender_id, uint32_t borrower_id,
                                           uint32_t requested_width);
    
    // 检测噪音邻居
    std::vector<uint32_t> detect_noisy_neighbors(uint32_t victim_id) const;
    
    // 计算公平性指数（Jain's fairness index）
    double calculate_fairness_index() const;
    
    // 租户限流
    void throttle_tenant(uint32_t namespace_id, const std::string& reason);
    void unthrottle_tenant(uint32_t namespace_id);
    
    // 获取所有租户状态
    std::vector<TenantStatus> get_all_tenant_status() const;

private:
    UnifiedMetadataManager* metadata_mgr_;
    
    // 租户配置
    std::unordered_map<uint32_t, TenantConfig> tenant_configs_;
    
    // 被限流的租户
    std::unordered_map<uint32_t, std::string> throttled_tenants_;
    
    // 锁
    mutable std::shared_mutex rw_mutex_;
    
    // 统计
    struct {
        uint64_t total_checks = 0;
        uint64_t total_allowed = 0;
        uint64_t total_denied = 0;
        uint64_t total_throttle_events = 0;
    } stats_;
    
    // 辅助函数
    float predict_frag_impact(uint32_t lender_id, uint32_t amount) const;
    bool check_latency_sla(uint32_t namespace_id) const;
};

} // namespace ezns_rbc

#endif // TENANT_ISOLATION_H