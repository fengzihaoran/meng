BorrowDecision TenantIsolation::check_borrow_permission(
    uint32_t lender_id, uint32_t borrower_id, uint32_t requested_width) {
    
    // 1. 检查出借者是否被限流
    if (throttled_tenants_.find(lender_id) != throttled_tenants_.end()) {
        return {false, 0, "Lender is throttled"};
    }
    
    // 2. 预测借贷后的碎片影响
    float frag_impact = predict_frag_impact(lender_id, requested_width);
    float new_frag = lender_meta->fragmentation_ratio + frag_impact;
    
    // 3. 检查是否超过租户的最大允许碎片度
    if (new_frag > lender_config.max_fragmentation) {
        // 计算允许的最大借贷量
        float allowed_increase = lender_config.max_fragmentation - 
                                  lender_meta->fragmentation_ratio;
        uint32_t allowed_borrow = static_cast<uint32_t>(allowed_increase * 100);
        
        return {false, allowed_borrow, "Would exceed max fragmentation"};
    }
    
    // 4. 检查延迟SLA
    if (!check_latency_sla(lender_id)) {
        return {false, 0, "Latency SLA violation risk"};
    }
    
    return {true, requested_width, "OK"};
}