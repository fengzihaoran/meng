#include "rbc_compaction.h"
#include <algorithm>

namespace rbc {

SlidingWindowDetector::SlidingWindowDetector(uint32_t window_size)
    : window_size_(window_size) {}

bool SlidingWindowDetector::check_block_integrity(
    const std::vector<KVMetadata>& kv_pairs, size_t start_pos) {
    
    if (start_pos + window_size_ > kv_pairs.size()) {
        return false;
    }
    
    // 检查窗口内所有KV对是否属于同一个原始块
    uint32_t expected_block_id = kv_pairs[start_pos].block_id;
    for (size_t i = start_pos + 1; i < start_pos + window_size_; i++) {
        if (kv_pairs[i].block_id != expected_block_id) {
            return false;
        }
    }
    return true;
}

bool SlidingWindowDetector::check_boundary_alignment(
    const std::vector<KVMetadata>& kv_pairs, size_t start_pos) {
    
    // Condition 2: 第一个KV对的偏移必须匹配原始块的起始地址
    if (kv_pairs[start_pos].offset != 0) {
        return false;
    }
    
    // Condition 3: 最后一个KV对的结束偏移必须匹配原始块的终止地址
    size_t last_pos = start_pos + window_size_ - 1;
    uint64_t expected_end = kv_pairs[last_pos].offset + kv_pairs[last_pos].length;
    
    // 假设块大小固定为4KB
    const uint64_t BLOCK_SIZE = 4 * 1024;
    if (expected_end != BLOCK_SIZE) {
        return false;
    }
    
    return true;
}

bool SlidingWindowDetector::is_clean_block(
    const std::vector<KVMetadata>& kv_pairs, size_t start_pos) {
    
    return check_block_integrity(kv_pairs, start_pos) && 
           check_boundary_alignment(kv_pairs, start_pos);
}

size_t SlidingWindowDetector::find_next_clean_block(
    const std::vector<KVMetadata>& kv_pairs, size_t start_pos) {
    
    for (size_t i = start_pos; i + window_size_ <= kv_pairs.size(); i++) {
        if (is_clean_block(kv_pairs, i)) {
            return i;
        }
    }
    return kv_pairs.size();  // 未找到
}

// ============================================================================
// 压缩策略选择器
// ============================================================================

CompactionStrategySelector::Strategy 
CompactionStrategySelector::select_strategy(float dirty_ratio) {
    if (dirty_ratio > DIRTY_BLOCK_THRESHOLD) {
        return Strategy::FILE_LEVEL;
    } else if (dirty_ratio < LOWER_THRESHOLD) {
        return Strategy::BLOCK_LEVEL;
    } else {
        return Strategy::HYBRID;
    }
}

const char* CompactionStrategySelector::strategy_name(Strategy s) {
    switch (s) {
        case Strategy::BLOCK_LEVEL: return "Block-level";
        case Strategy::HYBRID:       return "Hybrid (sliding window)";
        case Strategy::FILE_LEVEL:   return "File-level";
        default: return "Unknown";
    }
}

} // namespace rbc