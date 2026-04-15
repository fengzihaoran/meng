void UnifiedController::handle_write(uint32_t vzone_id, uint64_t lba,
                                      uint64_t physical_addr, uint32_t size,
                                      bool is_dirty) {
    if (!is_dirty) {
        // clean block：记录重映射（创新点1）
        metadata_mgr_.add_remapping(lba, physical_addr, size, vzone_id);
        
        // 更新vzone碎片度（创新点2的输入）
        UnifiedVZoneMeta* meta = metadata_mgr_.get_vzone(vzone_id);
        meta->add_remapped_block(lba);
        
        // 碎片度过高时，触发重组决策
        if (meta->fragmentation_ratio > FRAG_THRESHOLD) {
            // 后台线程会处理
        }
    }
}