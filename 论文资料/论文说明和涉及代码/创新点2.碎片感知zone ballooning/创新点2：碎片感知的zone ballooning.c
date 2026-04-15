// 碎片度阈值（参考RBC的30%）
#define FRAG_THRESHOLD 0.3
#define BORROW_REDUCTION_FACTOR 0.7

int frag_aware_ballooning(uint32_t ns_id, uint32_t req_width) {
    unified_metadata_t* meta = get_metadata(ns_id);
    
    // 碎片度检查
    if (meta->frag_ratio > FRAG_THRESHOLD) {
        // 触发空闲重组（RBC机制）
        if (is_idle()) {
            trigger_reorganization(ns_id);
        }
        
        // 减少借贷量
        req_width = (uint32_t)(req_width * BORROW_REDUCTION_FACTOR);
        log_event("Frag threshold exceeded, reducing borrow by 30%%");
    }
    
    return original_ballooning(ns_id, req_width);
}