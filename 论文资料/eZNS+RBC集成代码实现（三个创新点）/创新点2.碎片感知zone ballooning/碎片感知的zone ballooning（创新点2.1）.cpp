#ifndef FRAG_AWARE_BALLOON_H
#define FRAG_AWARE_BALLOON_H

#include "unified_metadata.h"
#include <functional>

namespace ezns_rbc {

// ============================================================================
// 碎片感知的zone ballooning（创新点2）
// 在eZNS的zone ballooning基础上加入碎片度检查
// ============================================================================

// 配置常量
constexpr float FRAG_THRESHOLD = 0.3f;          // 30%碎片度阈值（来自RBC）
constexpr float BORROW_REDUCTION_FACTOR = 0.7f; // 碎片度高时减少30%借贷
constexpr uint32_t MIN_STRIPE_WIDTH = 2;         // 最小条带宽度
constexpr uint32_t MAX_STRIPE_WIDTH = 16;        // 最大条带宽度

// 借贷请求
struct BorrowRequest {
    uint32_t namespace_id;
    uint32_t requested_width;
    uint32_t current_active_vzones;
    uint64_t timestamp;
};

// 借贷结果
struct BorrowResult {
    uint32_t allocated_width;
    uint32_t allocated_essential;
    uint32_t allocated_spare;
    uint32_t new_stripe_size;
    float new_frag_ratio;
    bool threshold_exceeded;
    bool success;
    std::string message;
};

// ============================================================================
// 碎片感知的zone ballooning类
// ============================================================================
class FragAwareBalloon {
public:
    explicit FragAwareBalloon(UnifiedMetadataManager* metadata_mgr);
    ~FragAwareBalloon();
    
    // 初始化资源池
    void init(uint32_t total_essential, uint32_t total_spare);
    
    // 核心接口：碎片感知的借贷
    BorrowResult borrow_zones(const BorrowRequest& request);
    
    // 归还spare zones
    bool return_spare_zones(uint32_t namespace_id, uint32_t num_spares);
    
    // 获取当前资源状态
    void get_resource_status(uint32_t& essential_used, uint32_t& spare_used,
                             uint32_t& essential_total, uint32_t& spare_total) const;
    
    // 设置参数
    void set_frag_threshold(float threshold) { frag_threshold_ = threshold; }
    void set_borrow_reduction(float factor) { borrow_reduction_factor_ = factor; }

private:
    UnifiedMetadataManager* metadata_mgr_;
    
    // 资源池
    uint32_t total_essential_;
    uint32_t total_spare_;
    uint32_t used_essential_;
    uint32_t used_spare_;
    
    // 租户当前分配
    struct TenantAlloc {
        uint32_t essential;
        uint32_t spare;
        std::vector<uint32_t> active_vzones;
    };
    std::unordered_map<uint32_t, TenantAlloc> tenant_alloc_;
    
    // 配置参数
    float frag_threshold_;
    float borrow_reduction_factor_;
    
    // 锁
    mutable std::shared_mutex rw_mutex_;
    
    // 统计
    struct {
        uint64_t total_borrow_requests = 0;
        uint64_t total_borrow_accepted = 0;
        uint64_t total_borrow_rejected = 0;
        uint64_t total_frag_reduced = 0;
    } stats_;
    
    // 辅助函数
    uint32_t calculate_essential_per_vzone(uint32_t namespace_id) const;
    uint32_t round_to_power_of_two(uint32_t width) const;
    uint32_t adjust_stripe_size(uint32_t width) const;
};

} // namespace ezns_rbc

#endif // FRAG_AWARE_BALLOON_H