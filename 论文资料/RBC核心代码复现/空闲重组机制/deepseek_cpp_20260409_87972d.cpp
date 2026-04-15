#ifndef RBC_REORG_H
#define RBC_REORG_H

#include "rbc_remap.h"
#include <thread>
#include <atomic>

namespace rbc {

// ============================================================================
// 重组触发条件（论文Section 3.2.2）
// ============================================================================
constexpr float FRAG_THRESHOLD = 0.3f;      // 30%碎片度阈值
constexpr float IDLE_BANDWIDTH_THRESHOLD = 0.3f;  // 30%带宽阈值

class ReorganizationManager {
public:
    explicit ReorganizationManager(RemapTable* remap);
    ~ReorganizationManager();
    
    // 启动后台重组线程
    void start();
    void stop();
    
    // 检查是否需要重组
    bool should_reorganize() const;
    
    // 执行重组
    void perform_reorganization();
    
    // 更新带宽使用率（由外部调用）
    void update_bandwidth_usage(float usage);

private:
    RemapTable* remap_table_;
    
    std::thread reorg_thread_;
    std::atomic<bool> running_{false};
    
    float current_bandwidth_usage_;
    std::atomic<uint64_t> total_reorg_count_{0};
    
    void reorg_worker();
    bool is_system_idle() const;
};

} // namespace rbc

#endif // RBC_REORG_H