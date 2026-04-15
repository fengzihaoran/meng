#ifndef EZNS_RBC_FRAG_AWARE_BALLOON_H
#define EZNS_RBC_FRAG_AWARE_BALLOON_H

#include "common.h"
#include "unified_metadata.h"
#include <functional>

namespace ezns_rbc {

// ============================================================================
// 碎片感知的zone ballooning（创新点2）
// 在eZNS的zone ballooning基础上加入碎片度检查
// ============================================================================

// 借贷请求
struct BorrowRequest {
    uint32_t namespace_id;      // 请求租户
    uint32_t requested_width;   // 请求的宽度（zone数）
    uint32_t priority;          // 优先级（1-10，越高越优先）
    uint64_t timestamp;         // 请求时间戳
};

// 借贷结果
struct BorrowResult {
    uint32_t allocated_width;   // 实际分配的宽度
    float new_frag_ratio;       // 分配后的碎片度
    bool threshold_exceeded;    // 是否超过阈值
    std::string message;        // 结果信息
};

// ============================================================================
// 碎片感知的zone ballooning类
// ============================================================================

class FragAwareBalloon {
public:
    explicit FragAwareBalloon(UnifiedMetadataManager* metadata_mgr);
    ~FragAwareBalloon();
    
    // 初始化
    void init(uint32_t total_essential, uint32_t total_spare);
    
    // 核心接口：碎片感知的借贷
    BorrowResult borrow_zones(const BorrowRequest& request);
    
    // 归还zone
    bool return_zones(uint32_t namespace_id, uint32_t width);
    
    // 触发重组（RBC机制）
    void trigger_reorganization(uint32_t vzone_id);
    
    // 检查是否应该重组
    bool should_reorganize(uint32_t vzone_id) const;
    
    // 空闲时重组
    void idle_time_reorganization();
    
    // 获取当前资源状态
    void get_resource_status(uint32_t& essential_used, uint32_t& spare_used,
                             uint32_t& essential_total, uint32_t& spare_total) const;
    
    // 设置碎片度阈值
    void set_frag_threshold(float threshold) { frag_threshold_ = threshold; }
    
    // 设置借贷减少因子
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
    float frag_threshold_ = FRAG_THRESHOLD;
    float borrow_reduction_factor_ = BORROW_REDUCTION_FACTOR;
    
    // 锁
    mutable std::shared_mutex rw_mutex_;
    
    // 统计信息
    struct {
        uint64_t total_borrow_requests = 0;
        uint64_t total_borrow_accepted = 0;
        uint64_t total_borrow_rejected = 0;
        uint64_t total_reorg_triggered = 0;
        uint64_t total_idle_reorg = 0;
    } stats_;
    
    // 辅助函数
    float calculate_frag_after_borrow(uint32_t ns_id, uint32_t additional_spare) const;
    bool is_system_idle() const;
};

} // namespace ezns_rbc

#endif // EZNS_RBC_FRAG_AWARE_BALLOON_H