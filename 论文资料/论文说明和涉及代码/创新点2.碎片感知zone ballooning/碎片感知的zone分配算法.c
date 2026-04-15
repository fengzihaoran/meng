// 改进后的zone分配算法
int zone_arbiter_alloc_with_frag(unified_zone_t* zone, 
                                   uint32_t requested_width) {
    // 1. 检查当前碎片度
    if (zone->fragmentation_ratio > FRAG_THRESHOLD) {
        // 碎片度过高，触发重组（RBC机制）
        trigger_reorganization(zone);
        
        // 减少借贷量（eZNS机制）
        requested_width = requested_width * 0.7;
    }
    
    // 2. 原有分配逻辑
    return original_alloc(zone, requested_width);
}