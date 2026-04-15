// 重组触发条件
int should_trigger_reorg(unified_zone_t* zone) {
    // 条件1：碎片度过高（RBC标准）
    if (zone->fragmentation_ratio > FRAG_THRESHOLD)
        return 1;
    
    // 条件2：最近有频繁借贷（eZNS标准）
    if (zone->borrow_count > BORROW_THRESHOLD)
        return 1;
    
    // 条件3：空闲带宽充足（RBC空闲重组）
    if (get_current_bandwidth_usage() < IDLE_BANDWIDTH_THRESHOLD)
        return 1;
    
    return 0;
}