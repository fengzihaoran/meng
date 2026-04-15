#include "frag_aware_balloon.h"
#include <algorithm>
#include <cmath>

namespace ezns_rbc {

FragAwareBalloon::FragAwareBalloon(UnifiedMetadataManager* metadata_mgr)
    : metadata_mgr_(metadata_mgr),
      total_essential_(0), total_spare_(0),
      used_essential_(0), used_spare_(0),
      frag_threshold_(FRAG_THRESHOLD),
      borrow_reduction_factor_(BORROW_REDUCTION_FACTOR) {
    printf("[FragAwareBalloon] Created\n");
}

FragAwareBalloon::~FragAwareBalloon() {
    printf("[FragAwareBalloon] Destroyed: requests=%lu, accepted=%lu, rejected=%lu, frag_reduced=%lu\n",
           stats_.total_borrow_requests, stats_.total_borrow_accepted,
           stats_.total_borrow_rejected, stats_.total_frag_reduced);
}

void FragAwareBalloon::init(uint32_t total_essential, uint32_t total_spare) {
    std::unique_lock lock(rw_mutex_);
    total_essential_ = total_essential;
    total_spare_ = total_spare;
    used_essential_ = 0;
    used_spare_ = 0;
    
    printf("[FragAwareBalloon] Initialized: essential=%u, spare=%u\n",
           total_essential, total_spare);
}

uint32_t FragAwareBalloon::calculate_essential_per_vzone(uint32_t namespace_id) const {
    // 基于租户数量和active vzone数计算
    uint32_t num_active = 1;  // 简化，实际应从metadata获取
    uint32_t num_namespaces = 4;  // 固定4个租户
    
    uint32_t essential_per_ns = total_essential_ / num_namespaces;
    return std::max(1u, essential_per_ns / num_active);
}

uint32_t FragAwareBalloon::round_to_power_of_two(uint32_t width) const {
    if (width <= 2) return 2;
    uint32_t power = 1;
    while (power * 2 <= width) {
        power *= 2;
    }
    return std::min(power, MAX_STRIPE_WIDTH);
}

uint32_t FragAwareBalloon::adjust_stripe_size(uint32_t width) const {
    // 条带大小与宽度成反比（论文Section 4.4.2）
    const uint32_t BASE_STRIPE_SIZE = 32 * 1024;  // 32KB
    const uint32_t MIN_STRIPE_SIZE = 4 * 1024;    // 4KB
    
    uint32_t size = BASE_STRIPE_SIZE / (width / 2);
    return std::max(size, MIN_STRIPE_SIZE);
}

BorrowResult FragAwareBalloon::borrow_zones(const BorrowRequest& request) {
    stats_.total_borrow_requests++;
    
    BorrowResult result;
    result.success = false;
    result.threshold_exceeded = false;
    
    // 获取租户的v-zone元数据
    uint32_t vzone_id = request.namespace_id * 100 + 1;  // 简化映射
    UnifiedVZoneMeta* meta = metadata_mgr_->get_vzone(vzone_id);
    
    if (!meta) {
        result.message = "VZone not found";
        return result;
    }
    
    // 检查当前碎片度（创新点2的核心）
    float current_frag = meta->fragmentation_ratio;
    uint32_t adjusted_width = request.requested_width;
    
    if (current_frag > frag_threshold_) {
        // 碎片度过高，减少借贷量
        adjusted_width = static_cast<uint32_t>(
            request.requested_width * borrow_reduction_factor_);
        adjusted_width = std::max(adjusted_width, MIN_STRIPE_WIDTH);
        result.threshold_exceeded = true;
        stats_.total_frag_reduced++;
        
        printf("[FragAwareBalloon] Frag threshold exceeded: %.2f > %.2f, reducing width %u -> %u\n",
               current_frag, frag_threshold_, request.requested_width, adjusted_width);
    }
    
    // 计算可分配资源
    std::unique_lock lock(rw_mutex_);
    
    uint32_t essential_per_vzone = calculate_essential_per_vzone(request.namespace_id);
    uint32_t available_spare = total_spare_ - used_spare_;
    uint32_t requested_spare = adjusted_width - essential_per_vzone;
    uint32_t allocatable_spare = std::min(requested_spare, available_spare);
    
    if (allocatable_spare > 0) {
        // 分配资源
        used_spare_ += allocatable_spare;
        
        auto& alloc = tenant_alloc_[request.namespace_id];
        alloc.essential += essential_per_vzone;
        alloc.spare += allocatable_spare;
        alloc.active_vzones.push_back(vzone_id);
        
        result.allocated_width = essential_per_vzone + allocatable_spare;
        result.allocated_essential = essential_per_vzone;
        result.allocated_spare = allocatable_spare;
        result.new_stripe_size = adjust_stripe_size(result.allocated_width);
        result.new_frag_ratio = current_frag + 0.01f * allocatable_spare;  // 估算
        result.success = true;
        result.message = "Success";
        
        stats_.total_borrow_accepted++;
        
        // 记录借贷
        metadata_mgr_->record_borrow(vzone_id, allocatable_spare);
        
        printf("[FragAwareBalloon] Borrow success: ns=%u, width=%u (ess=%u, spare=%u), stripe_size=%u\n",
               request.namespace_id, result.allocated_width, essential_per_vzone,
               allocatable_spare, result.new_stripe_size);
    } else {
        result.message = "Insufficient spare zones";
        stats_.total_borrow_rejected++;
    }
    
    return result;
}

bool FragAwareBalloon::return_spare_zones(uint32_t namespace_id, uint32_t num_spares) {
    std::unique_lock lock(rw_mutex_);
    
    auto it = tenant_alloc_.find(namespace_id);
    if (it == tenant_alloc_.end() || it->second.spare < num_spares) {
        return false;
    }
    
    it->second.spare -= num_spares;
    used_spare_ -= num_spares;
    
    printf("[FragAwareBalloon] Returned %u spares from ns=%u\n", num_spares, namespace_id);
    return true;
}

void FragAwareBalloon::get_resource_status(uint32_t& essential_used, uint32_t& spare_used,
                                           uint32_t& essential_total, uint32_t& spare_total) const {
    std::shared_lock lock(rw_mutex_);
    essential_used = used_essential_;
    spare_used = used_spare_;
    essential_total = total_essential_;
    spare_total = total_spare_;
}

} // namespace ezns_rbc