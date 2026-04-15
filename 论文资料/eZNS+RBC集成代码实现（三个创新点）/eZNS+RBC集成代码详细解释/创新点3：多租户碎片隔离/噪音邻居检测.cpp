std::vector<uint32_t> TenantIsolation::detect_noisy_neighbors(uint32_t victim_id) const {
    std::vector<uint32_t> noisy;
    
    auto victim_status = get_tenant_status(victim_id);
    auto all_status = get_all_tenant_status();
    
    for (const auto& status : all_status) {
        if (status.namespace_id == victim_id) continue;
        
        // 检测条件：碎片度远高于受害者，且超过阈值
        if (status.current_fragmentation > victim_status.current_fragmentation * 1.5 &&
            status.current_fragmentation > FRAG_THRESHOLD) {
            noisy.push_back(status.namespace_id);
        }
    }
    
    return noisy;
}