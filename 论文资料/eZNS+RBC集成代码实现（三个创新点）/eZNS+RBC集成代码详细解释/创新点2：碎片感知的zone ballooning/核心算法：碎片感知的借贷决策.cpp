BorrowResult FragAwareBalloon::borrow_zones(const BorrowRequest& request) {
    // 1. 获取当前碎片度
    float current_frag = meta->fragmentation_ratio;
    uint32_t adjusted_width = request.requested_width;
    
    // 2. 碎片度检查（创新点核心）
    if (current_frag > frag_threshold_) {
        // 碎片度过高，减少借贷量（论文Section 4.4.2）
        adjusted_width = static_cast<uint32_t>(
            request.requested_width * borrow_reduction_factor_);
        adjusted_width = std::max(adjusted_width, MIN_STRIPE_WIDTH);
        result.threshold_exceeded = true;
        stats_.total_frag_reduced++;
        
        printf("碎片度过高 %.2f > %.2f，减少借贷 %u -> %u\n",
               current_frag, frag_threshold_, 
               request.requested_width, adjusted_width);
    }
    
    // 3. 计算可分配资源
    uint32_t essential_per_vzone = calculate_essential_per_vzone(...);
    uint32_t available_spare = total_spare_ - used_spare_;
    uint32_t requested_spare = adjusted_width - essential_per_vzone;
    uint32_t allocatable_spare = std::min(requested_spare, available_spare);
    
    // 4. 分配资源并记录
    if (allocatable_spare > 0) {
        // 更新租户分配状态
        auto& alloc = tenant_alloc_[request.namespace_id];
        alloc.spare += allocatable_spare;
        
        // 记录借贷事件（用于后续分析）
        metadata_mgr_->record_borrow(vzone_id, allocatable_spare);
    }
}