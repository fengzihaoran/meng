void UnifiedController::reorg_worker() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 检查所有vzone的碎片度
        for (uint32_t ns = 1; ns <= 4; ns++) {
            uint32_t vzone_id = ns * 100 + 1;
            UnifiedVZoneMeta* meta = metadata_mgr_.get_vzone(vzone_id);
            
            // 如果碎片度过高且不是被限流的租户，触发重组
            if (meta && meta->fragmentation_ratio > FRAG_THRESHOLD) {
                auto status = isolation_.get_tenant_status(ns);
                if (!status.is_throttled) {
                    trigger_reorganization(vzone_id);
                }
            }
        }
    }
}