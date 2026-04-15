#include "rbc_remap.h"

namespace rbc {

RemapTable::RemapTable() : max_entries_(0), total_remapped_blocks_(0) {}

RemapTable::~RemapTable() {
    auto stats = get_stats();
    printf("[RBC] RemapTable destroyed: entries=%lu, mem=%.2f MB\n",
           stats.total_entries, stats.memory_usage_bytes / (1024.0 * 1024.0));
}

bool RemapTable::init(uint64_t max_entries) {
    max_entries_ = max_entries;
    table_.reserve(max_entries);
    return true;
}

bool RemapTable::add_remapping(uint64_t logical_addr, uint64_t physical_addr, uint32_t size) {
    std::unique_lock lock(rw_mutex_);
    
    if (table_.size() >= max_entries_) {
        // LRU淘汰（简化版本）
        auto it = table_.begin();
        uint64_t oldest_time = it->second.last_access;
        auto oldest = it;
        
        for (; it != table_.end(); ++it) {
            if (it->second.last_access < oldest_time) {
                oldest_time = it->second.last_access;
                oldest = it;
            }
        }
        table_.erase(oldest);
    }
    
    RemapEntry entry;
    entry.logical_addr = logical_addr;
    entry.physical_addr = physical_addr;
    entry.size = size;
    entry.ref_count = 1;
    entry.last_access = get_current_us();
    
    table_[logical_addr] = entry;
    total_remapped_blocks_++;
    
    return true;
}

bool RemapTable::lookup(uint64_t logical_addr, uint64_t& physical_addr, uint32_t& size) {
    std::shared_lock lock(rw_mutex_);
    total_lookups_++;
    
    auto it = table_.find(logical_addr);
    if (it != table_.end()) {
        physical_addr = it->second.physical_addr;
        size = it->second.size;
        it->second.last_access = get_current_us();
        cache_hits_++;
        return true;
    }
    
    return false;
}

bool RemapTable::remove_remapping(uint64_t logical_addr) {
    std::unique_lock lock(rw_mutex_);
    
    auto it = table_.find(logical_addr);
    if (it != table_.end()) {
        table_.erase(it);
        total_remapped_blocks_--;
        return true;
    }
    
    return false;
}

float RemapTable::get_fragmentation_ratio() const {
    std::shared_lock lock(rw_mutex_);
    // 假设总块数已知
    uint64_t total_blocks = 1024 * 1024;  // 示例值
    return static_cast<float>(table_.size()) / total_blocks;
}

RemapTable::Stats RemapTable::get_stats() const {
    std::shared_lock lock(rw_mutex_);
    
    Stats stats;
    stats.total_entries = table_.size();
    stats.total_lookups = total_lookups_.load();
    stats.cache_hits = cache_hits_.load();
    
    // 估算内存占用
    stats.memory_usage_bytes = table_.size() * (sizeof(uint64_t) + sizeof(RemapEntry));
    stats.memory_usage_bytes += table_.bucket_count() * sizeof(void*);
    
    return stats;
}

} // namespace rbc