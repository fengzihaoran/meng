#include "unified_metadata.h"

namespace ezns_rbc {

UnifiedMetadataManager::UnifiedMetadataManager() {
    LOG_INFO("UnifiedMetadataManager initialized");
}

UnifiedMetadataManager::~UnifiedMetadataManager() {
    auto stats = get_stats();
    LOG_INFO("UnifiedMetadataManager destroyed, stats: " 
             << "memory=" << stats.total_metadata_memory / 1024 << "KB"
             << ", vzones=" << stats.total_vzones
             << ", remapped=" << stats.total_remapped_blocks
             << ", avg_frag=" << stats.avg_fragmentation);
}

bool UnifiedMetadataManager::init(const std::string& device_path, uint32_t num_namespaces) {
    std::unique_lock lock(rw_mutex_);
    LOG_INFO("Initializing with " << num_namespaces << " namespaces");
    // 实际初始化代码会根据设备信息创建初始元数据
    return true;
}

UnifiedMetadata* UnifiedMetadataManager::get_metadata(uint32_t vzone_id) {
    auto start_time = get_current_us();
    
    {
        std::shared_lock lock(rw_mutex_);
        auto it = metadata_map_.find(vzone_id);
        if (it != metadata_map_.end()) {
            auto& meta = it->second;
            meta.last_access_time = get_current_us();
            meta.access_count++;
            
            auto end_time = get_current_us();
            total_lookups_++;
            total_lookup_time_us_ += (end_time - start_time);
            
            return &meta;
        }
    }
    
    auto end_time = get_current_us();
    total_lookups_++;
    total_lookup_time_us_ += (end_time - start_time);
    
    return nullptr;
}

UnifiedMetadata* UnifiedMetadataManager::create_metadata(uint32_t namespace_id, 
                                                          uint32_t stripe_width) {
    static uint32_t next_vzone_id = 1000;  // 简单递增
    
    std::unique_lock lock(rw_mutex_);
    
    uint32_t vzone_id = next_vzone_id++;
    UnifiedMetadata meta;
    meta.vzone_id = vzone_id;
    meta.namespace_id = namespace_id;
    meta.stripe_width = stripe_width;
    meta.stripe_size = 16 * 1024;  // 默认16KB
    meta.essential_allocated = 2;  // 默认最小2个
    meta.spare_allocated = 0;
    meta.congestion_window = stripe_width;  // 初始为stripe_width
    meta.total_blocks = 1024;  // 假设每zone 1024个block
    meta.last_access_time = get_current_us();
    
    metadata_map_[vzone_id] = meta;
    
    LOG_INFO("Created vzone " << vzone_id << " for namespace " << namespace_id);
    
    return &metadata_map_[vzone_id];
}

void UnifiedMetadataManager::update_access(uint32_t vzone_id, IOType type, uint64_t latency_us) {
    auto meta = get_metadata(vzone_id);
    if (!meta) return;
    
    if (type == IOType::READ) {
        meta->total_read_latency += latency_us;
        meta->read_io_count++;
        meta->last_read_latency = latency_us;
    }
    // 其他类型可类似处理
}

void UnifiedMetadataManager::record_borrow(uint32_t vzone_id, uint32_t amount) {
    auto meta = get_metadata(vzone_id);
    if (meta) {
        meta->borrow_count++;
        meta->spare_allocated += amount;
        LOG_DEBUG("vzone " << vzone_id << " borrowed " << amount 
                  << ", total spare now " << meta->spare_allocated);
    }
}

void UnifiedMetadataManager::record_reorg(uint32_t vzone_id) {
    auto meta = get_metadata(vzone_id);
    if (meta) {
        meta->reorg_count++;
        LOG_DEBUG("vzone " << vzone_id << " reorganized, count=" << meta->reorg_count);
    }
}

float UnifiedMetadataManager::get_fragmentation(uint32_t vzone_id) const {
    std::shared_lock lock(rw_mutex_);
    auto it = metadata_map_.find(vzone_id);
    if (it != metadata_map_.end()) {
        return it->second.fragmentation_ratio;
    }
    return 0.0f;
}

void UnifiedMetadataManager::add_remapped_block(uint32_t vzone_id, uint32_t block_id) {
    auto meta = get_metadata(vzone_id);
    if (meta) {
        meta->add_remapped_block(block_id);
    }
}

void UnifiedMetadataManager::remove_remapped_block(uint32_t vzone_id, uint32_t block_id) {
    auto meta = get_metadata(vzone_id);
    if (meta) {
        meta->remove_remapped_block(block_id);
    }
}

UnifiedMetadataManager::Stats UnifiedMetadataManager::get_stats() const {
    std::shared_lock lock(rw_mutex_);
    
    Stats stats;
    stats.total_vzones = metadata_map_.size();
    stats.total_remapped_blocks = 0;
    float total_frag = 0.0f;
    
    for (const auto& pair : metadata_map_) {
        stats.total_remapped_blocks += pair.second.remapped_blocks.size();
        total_frag += pair.second.fragmentation_ratio;
    }
    
    stats.avg_fragmentation = (stats.total_vzones > 0) ? 
                              total_frag / stats.total_vzones : 0.0f;
    stats.total_metadata_memory = estimate_memory_usage();
    stats.total_lookups = total_lookups_.load();
    stats.avg_lookup_latency_us = (stats.total_lookups > 0) ? 
                                  static_cast<double>(total_lookup_time_us_.load()) / 
                                  stats.total_lookups : 0.0;
    
    return stats;
}

uint64_t UnifiedMetadataManager::estimate_memory_usage() const {
    uint64_t total = 0;
    
    // 估算每个元数据的大小
    for (const auto& pair : metadata_map_) {
        total += sizeof(UnifiedMetadata);
        total += pair.second.remapped_blocks.capacity() * sizeof(uint32_t);
    }
    
    // 加上unordered_map的开销（估算）
    total += metadata_map_.bucket_count() * sizeof(void*);
    total += metadata_map_.size() * (sizeof(uint32_t) + sizeof(UnifiedMetadata*));
    
    return total;
}

void UnifiedMetadataManager::dump_stats(std::ostream& os) const {
    auto stats = get_stats();
    
    os << "=== UnifiedMetadataManager Stats ===" << std::endl;
    os << "Total vZones: " << stats.total_vzones << std::endl;
    os << "Total remapped blocks: " << stats.total_remapped_blocks << std::endl;
    os << "Average fragmentation: " << stats.avg_fragmentation << std::endl;
    os << "Memory usage: " << stats.total_metadata_memory / 1024 << " KB" << std::endl;
    os << "Total lookups: " << stats.total_lookups << std::endl;
    os << "Avg lookup latency: " << stats.avg_lookup_latency_us << " us" << std::endl;
    os << "=====================================" << std::endl;
}

} // namespace ezns_rbc