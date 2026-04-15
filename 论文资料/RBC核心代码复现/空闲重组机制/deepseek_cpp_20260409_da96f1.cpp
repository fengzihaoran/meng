#include "rbc_reorg.h"
#include <chrono>
#include <thread>

namespace rbc {

ReorganizationManager::ReorganizationManager(RemapTable* remap)
    : remap_table_(remap), current_bandwidth_usage_(0.0f) {}

ReorganizationManager::~ReorganizationManager() {
    stop();
}

void ReorganizationManager::start() {
    running_ = true;
    reorg_thread_ = std::thread(&ReorganizationManager::reorg_worker, this);
}

void ReorganizationManager::stop() {
    running_ = false;
    if (reorg_thread_.joinable()) {
        reorg_thread_.join();
    }
}

void ReorganizationManager::update_bandwidth_usage(float usage) {
    current_bandwidth_usage_ = usage;
}

bool ReorganizationManager::should_reorganize() const {
    float frag = remap_table_->get_fragmentation_ratio();
    return (frag > FRAG_THRESHOLD && 
            current_bandwidth_usage_ < IDLE_BANDWIDTH_THRESHOLD);
}

bool ReorganizationManager::is_system_idle() const {
    return current_bandwidth_usage_ < IDLE_BANDWIDTH_THRESHOLD;
}

void ReorganizationManager::reorg_worker() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));  // 每5秒检查一次
        
        if (should_reorganize()) {
            perform_reorganization();
        }
    }
}

void ReorganizationManager::perform_reorganization() {
    printf("[RBC] Starting background reorganization...\n");
    total_reorg_count_++;
    
    // 1. 获取重映射表快照
    // 2. 顺序读取碎片块
    // 3. 写入新位置
    // 4. 更新元数据
    
    // 模拟重组过程
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    printf("[RBC] Reorganization completed (total=%lu)\n", 
           total_reorg_count_.load());
}

} // namespace rbc