bool UnifiedMetadataManager::lookup_remapping(uint64_t logical_addr, 
                                               uint64_t& physical_addr,
                                               uint32_t& size, 
                                               uint32_t& source_vzone) {
    std::shared_lock lock(rw_mutex_);
    total_lookups_++;
    
    auto it = remap_table_.find(logical_addr);
    if (it != remap_table_.end()) {
        physical_addr = it->second.physical_addr;
        size = it->second.size;
        source_vzone = it->second.source_vzone_id;
        cache_hits_++;
        return true;
    }
    return false;
}