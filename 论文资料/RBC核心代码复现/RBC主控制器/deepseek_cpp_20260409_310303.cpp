#ifndef RBC_CONTROLLER_H
#define RBC_CONTROLLER_H

#include "rbc_remap.h"
#include "rbc_reorg.h"
#include "rbc_compaction.h"

namespace rbc {

// ============================================================================
// RBC主控制器（论文Section 3.1）
// ============================================================================
class RBCController {
public:
    RBCController();
    ~RBCController();
    
    // 初始化
    bool init(uint64_t total_data_size);
    
    // 处理读请求
    bool handle_read(uint64_t logical_addr, uint64_t& physical_addr, 
                     uint32_t& size, bool& is_remapped);
    
    // 处理写请求（块压缩时调用）
    void handle_write(uint64_t logical_addr, uint64_t physical_addr, 
                      uint32_t size, bool is_dirty);
    
    // 压缩前调用
    void before_compaction(float dirty_ratio);
    
    // 统计信息
    void print_stats() const;

private:
    RemapTable remap_table_;
    ReorganizationManager reorg_manager_;
    SlidingWindowDetector window_detector_;
    
    // 统计
    struct {
        uint64_t total_reads = 0;
        uint64_t remapped_reads = 0;
        uint64_t total_writes = 0;
        uint64_t dirty_writes = 0;
        uint64_t compactions = 0;
    } stats_;
    
    // 当前压缩策略
    CompactionStrategySelector::Strategy current_strategy_;
};

} // namespace rbc

#endif // RBC_CONTROLLER_H