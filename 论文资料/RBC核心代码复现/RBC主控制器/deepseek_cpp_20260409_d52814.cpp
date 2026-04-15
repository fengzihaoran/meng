#include "rbc_controller.h"

namespace rbc {

RBCController::RBCController() 
    : reorg_manager_(&remap_table_), 
      window_detector_(1024),  // 窗口大小1024个KV对
      current_strategy_(CompactionStrategySelector::Strategy::BLOCK_LEVEL) {}

RBCController::~RBCController() {
    reorg_manager_.stop();
    print_stats();
}

bool RBCController::init(uint64_t total_data_size) {
    // 重映射表大小约0.4GB / TB（论文Section 3.2.1）
    uint64_t max_entries = total_data_size / (4 * 1024) * 0.004;  // 估算
    remap_table_.init(max_entries);
    
    reorg_manager_.start();
    
    return true;
}

bool RBCController::handle_read(uint64_t logical_addr, uint64_t& physical_addr,
                                 uint32_t& size, bool& is_remapped) {
    stats_.total_reads++;
    
    if (remap_table_.lookup(logical_addr, physical_addr, size)) {
        is_remapped = true;
        stats_.remapped_reads++;
        return true;
    }
    
    is_remapped = false;
    return false;
}

void RBCController::handle_write(uint64_t logical_addr, uint64_t physical_addr,
                                  uint32_t size, bool is_dirty) {
    stats_.total_writes++;
    
    if (is_dirty) {
        stats_.dirty_writes++;
    } else {
        // clean block，需要记录重映射（论文Section 3.2）
        remap_table_.add_remapping(logical_addr, physical_addr, size);
    }
    
    // 更新碎片度
    float frag = remap_table_.get_fragmentation_ratio();
    
    // 检查是否需要触发重组
    if (frag > FRAG_THRESHOLD) {
        reorg_manager_.update_bandwidth_usage(0.2f);  // 模拟低带宽
    }
}

void RBCController::before_compaction(float dirty_ratio) {
    stats_.compactions++;
    
    // 选择压缩策略（论文Section 3.3）
    current_strategy_ = CompactionStrategySelector::select_strategy(dirty_ratio);
    
    printf("[RBC] Compaction #%lu: dirty_ratio=%.2f, strategy=%s\n",
           stats_.compactions, dirty_ratio,
           CompactionStrategySelector::strategy_name(current_strategy_));
    
    if (current_strategy_ == CompactionStrategySelector::Strategy::HYBRID) {
        // 混合模式使用滑动窗口检测clean blocks
        printf("[RBC] Using sliding window detection\n");
    }
}

void RBCController::print_stats() const {
    auto remap_stats = remap_table_.get_stats();
    
    printf("\n=== RBC Statistics ===\n");
    printf("Total reads: %lu\n", stats_.total_reads);
    printf("  Remapped reads: %lu (%.2f%%)\n", 
           stats_.remapped_reads,
           100.0 * stats_.remapped_reads / stats_.total_reads);
    printf("Total writes: %lu\n", stats_.total_writes);
    printf("  Dirty writes: %lu\n", stats_.dirty_writes);
    printf("Compactions: %lu\n", stats_.compactions);
    printf("Remap table: %lu entries, %.2f MB\n",
           remap_stats.total_entries,
           remap_stats.memory_usage_bytes / (1024.0 * 1024.0));
    printf("Cache hit rate: %.2f%%\n",
           100.0 * remap_stats.cache_hits / remap_stats.total_lookups);
    printf("====================\n");
}

} // namespace rbc