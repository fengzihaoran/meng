#include "frag_aware_balloon.h"

namespace ezns_rbc {

FragAwareBalloon::FragAwareBalloon(UnifiedMetadataManager* metadata_mgr)
    : metadata_mgr_(metadata_mgr),
      total_essential_(0), total_spare_(0),
      used_essential_(0), used_spare_(0) {
    LOG_INFO("FragAwareBalloon created");
}

FragAwareBalloon::~FragAwareBalloon() {
    LOG_INFO("FragAwareBalloon destroyed, stats: "
             << "borrow_req=" << stats_.total_borrow_requests
             << ", accepted=" << stats_.total_borrow_accepted
             << ", rejected=" << stats_.total_borrow_rejected
             << ", reorg=" << stats_.total_reorg_triggered);
}

void FragAwareBalloon::init(uint32_t total_essential, uint32_t total_spare) {
    std::unique_lock lock(rw_mutex_);
    total_essential_ = total_essential;
    total_spare_ = total_spare;
    used_essential_ = 0;
    used_spare_ = 0;
    
    LOG_INFO("Initialized: essential=" << total_essential 
             << ", spare=" << total_spare);
}

BorrowResult FragAwareBalloon::borrow_zones(const BorrowRequest& request) {
    stats_.total_borrow_requests++;
    
    std::unique_lock lock(rw_mutex_);
    
    BorrowResult result;
    result.allocated_width = request.requested_width;
    result.threshold_exceeded = false;
    
    LOG_DEBUG("Processing borrow request from NS" << request.namespace_id 
              << " for " << request.requested_width << " zones");
    
    // 1. 获取租户当前的元数据（假设每个租户有一个主vzone）
    // 简化：用namespace_id作为vzone_id
    uint32_t vzone_id = request.namespace_id * 100 + 1;  // 简单映射
    auto meta = metadata_mgr_->get_metadata(vzone_id);
    
    if (!meta) {
        // 如果没有元数据，创建一个
        meta = metadata_mgr_->create_metadata(request.namespace_id, 4);  // 默认宽度4
    }
    
    // 2. 检查当前碎片度
    float current_frag = meta->fragmentation_ratio;
    result.new_frag_ratio = current_frag;
    
    if (current_frag > frag_threshold_) {
        // 碎片度过高，减少借贷量
        uint32_t original_width = request.requested_width;
        uint32_t new_width = static_cast<uint32_t>(
            original_width * borrow_reduction_factor_);
        
        // 至少保证1个
        if (new_width < 1) new_width = 1;
        
        result.allocated_width = new_width;
        result.threshold_exceeded = true;
        
        LOG_DEBUG("Frag threshold exceeded (" << current_frag 
                  << " > " << frag_threshold_ 
                  << "), reducing borrow from " << original_width 
                  << " to " << new_width);
        
        // 触发重组（异步）
        if (should_reorganize(vzone_id)) {
            std::thread([this, vzone_id]() {
                this->trigger_reorganization(vzone_id);
            }).detach();
        }
    }
    
    // 3. 检查资源是否足够
    uint32_t available_spare = total_spare_ - used_spare_;
    if (result.allocated_width > available_spare) {
        result.allocated_width = available_spare;
        result.message = "Insufficient spare zones";
    }
    
    if (result.allocated_width > 0) {
        // 更新分配
        used_spare_ += result.allocated_width;
        auto& alloc = tenant_alloc_[request.namespace_id];
        alloc.spare += result.allocated_width;
        
        // 记录借贷
        metadata_mgr_->record_borrow(vzone_id, result.allocated_width);
        
        stats_.total_borrow_accepted++;
        result.message = "Success";
        
        LOG_DEBUG("Allocated " << result.allocated_width 
                  << " spare zones to NS" << request.namespace_id);
    } else {
        stats_.total_borrow_rejected++;
        result.message = "No available spare zones";
    }
    
    return result;
}

bool FragAwareBalloon::return_zones(uint32_t namespace_id, uint32_t width) {
    std::unique_lock lock(rw_mutex_);
    
    auto it = tenant_alloc_.find(namespace_id);
    if (it == tenant_alloc_.end() || it->second.spare < width) {
        LOG_ERROR("Cannot return " << width << " zones from NS" 
                  << namespace_id << ": insufficient");
        return false;
    }
    
    it->second.spare -= width;
    used_spare_ -= width;
    
    LOG_DEBUG("NS" << namespace_id << " returned " << width << " spare zones");
    
    return true;
}

void FragAwareBalloon::trigger_reorganization(uint32_t vzone_id) {
    stats_.total_reorg_triggered++;
    
    auto meta = metadata_mgr_->get_metadata(vzone_id);
    if (!meta) return;
    
    LOG_INFO("Triggering reorganization for vzone " << vzone_id);
    
    // 模拟重组过程
    // 1. 读取所有remapped blocks
    std::vector<uint32_t> blocks = meta->remapped_blocks;
    
    // 2. 顺序写入新位置（简化：清空remap列表）
    meta->remapped_blocks.clear();
    
    // 3. 更新碎片度
    meta->update_fragmentation();
    metadata_mgr_->record_reorg(vzone_id);
    
    LOG_INFO("Reorganization complete for vzone " << vzone_id 
             << ", reorganized " << blocks.size() << " blocks");
}

bool FragAwareBalloon::should_reorganize(uint32_t vzone_id) const {
    auto meta = metadata_mgr_->get_metadata(vzone_id);
    if (!meta) return false;
    
    // 条件1：碎片度过高
    if (meta->fragmentation_ratio > frag_threshold_) {
        return true;
    }
    
    // 条件2：近期有频繁借贷
    if (meta->borrow_count > 10) {  // 假设阈值
        return true;
    }
    
    return false;
}

void FragAwareBalloon::idle_time_reorganization() {
    if (!is_system_idle()) {
        return;
    }
    
    LOG_INFO("System idle, performing background reorganization");
    
    std::shared_lock lock(rw_mutex_);
    
    // 检查所有vzone，对需要重组的进行重组
    // 这里简化：直接调用metadata_mgr的迭代接口
    // 实际实现需要遍历所有vzone
    
    stats_.total_idle_reorg++;
}

bool FragAwareBalloon::is_system_idle() const {
    // 简化：检查当前I/O队列长度
    // 实际实现需要从I/O调度器获取信息
    return true;  // 假设总是空闲
}

void FragAwareBalloon::get_resource_status(uint32_t& essential_used, 
                                            uint32_t& spare_used,
                                            uint32_t& essential_total, 
                                            uint32_t& spare_total) const {
    std::shared_lock lock(rw_mutex_);
    essential_used = used_essential_;
    spare_used = used_spare_;
    essential_total = total_essential_;
    spare_total = total_spare_;
}

float FragAwareBalloon::calculate_frag_after_borrow(uint32_t ns_id, 
                                                      uint32_t additional_spare) const {
    // 简化计算：假设每借一个spare，碎片度增加1%
    uint32_t vzone_id = ns_id * 100 + 1;
    auto meta = metadata_mgr_->get_metadata(vzone_id);
    if (!meta) return 0.0f;
    
    return meta->fragmentation_ratio + 0.01f * additional_spare;
}

} // namespace ezns_rbc