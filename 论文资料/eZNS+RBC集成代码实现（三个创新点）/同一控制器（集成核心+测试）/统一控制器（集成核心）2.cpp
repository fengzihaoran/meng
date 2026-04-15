#include "unified_controller.h"
#include <chrono>

namespace ezns_rbc {

UnifiedController::UnifiedController() 
    : balloon_(&metadata_mgr_), isolation_(&metadata_mgr_),
      total_zones_(0), max_active_zones_(0) {
    printf("========================================\n");
    printf("eZNS+RBC Unified Controller Created\n");
    printf("========================================\n");
}

UnifiedController::~UnifiedController() {
    running_ = false;
    if (reorg_thread_.joinable()) {
        reorg_thread_.join();
    }
    print_stats();
}

bool UnifiedController::init(uint32_t total_zones, uint32_t max_active_zones,
                              uint64_t total_data_size) {
    total_zones_ = total_zones;
    max_active_zones_ = max_active_zones;
    
    // 初始化元数据管理器
    if (!metadata_mgr_.init(total_data_size, 4)) {
        printf("Failed to initialize metadata manager\n");
        return false;
    }
    
    // 计算资源分配（论文Section 4.4.1）
    uint32_t essential_needed = 80;  // 达到最大写带宽需要80个zone
    uint32_t total_essential = std::min(essential_needed, max_active_zones / 2);
    uint32_t total_spare = max_active_zones - total_essential;
    
    // 初始化碎片感知的zone ballooning
    balloon_.init(total_essential, total_spare);
    
    // 启动后台重组线程
    running_ = true;
    reorg_thread_ = std::thread(&UnifiedController::reorg_worker, this);
    
    printf("UnifiedController initialized: total_zones=%u, max_active=%u, "
           "essential=%u, spare=%u\n",
           total_zones, max_active_zones, total_essential, total_spare);
    
    return true;
}

bool UnifiedController::register_tenant(uint32_t namespace_id, TenantType type,
                                         uint32_t priority) {
    TenantConfig config;
    config.namespace_id = namespace_id;
    config.type = type;
    config.priority = priority;
    
    // 根据租户类型设置参数
    switch (type) {
        case TenantType::WRITE_HEAVY:
            config.max_fragmentation = 0.35f;  // 写重租户容忍更高碎片
            config.latency_sla_us = 2000;
            config.max_spare_zones = 12;
            break;
        case TenantType::READ_HEAVY:
            config.max_fragmentation = 0.25f;  // 读重租户需要更低碎片
            config.latency_sla_us = 500;
            config.max_spare_zones = 6;
            break;
        case TenantType::MIXED:
            config.max_fragmentation = 0.3f;
            config.latency_sla_us = 1000;
            config.max_spare_zones = 8;
            break;
    }
    
    return isolation_.register_tenant(config);
}

uint32_t UnifiedController::allocate_vzone(uint32_t namespace_id, uint32_t requested_width) {
    stats_.total_allocations++;
    
    // 1. 检查租户隔离（创新点3）
    auto decision = isolation_.check_borrow_permission(namespace_id, namespace_id,
                                                        requested_width);
    if (!decision.allowed) {
        printf("Allocation denied for ns=%u: %s\n", namespace_id, decision.reason.c_str());
        return 0;
    }
    
    // 2. 创建元数据
    UnifiedVZoneMeta* meta = metadata_mgr_.create_vzone(namespace_id, requested_width);
    if (!meta) {
        printf("Failed to create vzone for ns=%u\n", namespace_id);
        return 0;
    }
    
    // 3. 发起借贷请求（创新点2）
    BorrowRequest req;
    req.namespace_id = namespace_id;
    req.requested_width = requested_width;
    req.current_active_vzones = 1;
    req.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    auto result = balloon_.borrow_zones(req);
    
    if (result.success) {
        meta->essential_allocated = result.allocated_essential;
        meta->spare_allocated = result.allocated_spare;
        meta->stripe_width = result.allocated_width;
        meta->stripe_size = result.new_stripe_size;
        
        printf("Allocated vzone %u for ns=%u: width=%u, stripe_size=%u, frag=%.2f\n",
               meta->vzone_id, namespace_id, result.allocated_width,
               result.new_stripe_size, result.new_frag_ratio);
        
        return meta->vzone_id;
    } else {
        printf("Failed to allocate vzone for ns=%u: %s\n",
               namespace_id, result.message.c_str());
        return 0;
    }
}

bool UnifiedController::free_vzone(uint32_t vzone_id) {
    UnifiedVZoneMeta* meta = metadata_mgr_.get_vzone(vzone_id);
    if (!meta) return false;
    
    // 归还spare zones
    if (meta->spare_allocated > 0) {
        balloon_.return_spare_zones(meta->namespace_id, meta->spare_allocated);
    }
    
    return metadata_mgr_.delete_vzone(vzone_id);
}

void UnifiedController::handle_read(uint32_t vzone_id, uint64_t lba, uint64_t latency_us) {
    stats_.total_reads++;
    
    // 更新元数据
    metadata_mgr_.update_read_latency(vzone_id, latency_us);
    
    // 检查是否要触发噪音邻居检测
    if (stats_.total_reads % 10000 == 0) {
        auto noisy = isolation_.detect_noisy_neighbors(vzone_id / 100);
        if (!noisy.empty()) {
            printf("Detected noisy neighbors for vzone %u\n", vzone_id);
        }
    }
}

void UnifiedController::handle_write(uint32_t vzone_id, uint64_t lba,
                                      uint64_t physical_addr, uint32_t size,
                                      bool is_dirty) {
    stats_.total_writes++;
    
    if (!is_dirty) {
        // clean block，记录重映射（创新点1）
        metadata_mgr_.add_remapping(lba, physical_addr, size, vzone_id);
        
        UnifiedVZoneMeta* meta = metadata_mgr_.get_vzone(vzone_id);
        if (meta) {
            meta->add_remapped_block(lba);
            
            // 如果碎片度过高，考虑触发重组
            if (meta->fragmentation_ratio > FRAG_THRESHOLD) {
                // 交给后台线程处理
            }
        }
    }
}

void UnifiedController::trigger_reorganization(uint32_t vzone_id) {
    stats_.total_reorgs++;
    
    UnifiedVZoneMeta* meta = metadata_mgr_.get_vzone(vzone_id);
    if (!meta) return;
    
    printf("Triggering reorganization for vzone %u (frag=%.2f)\n",
           vzone_id, meta->fragmentation_ratio);
    
    // 模拟重组过程
    meta->remapped_blocks.clear();
    meta->update_fragmentation();
    metadata_mgr_.record_reorg(vzone_id);
    
    printf("Reorganization completed for vzone %u\n", vzone_id);
}

void UnifiedController::reorg_worker() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 定期检查所有vzone的碎片度
        // 这里简化处理，实际需要遍历所有vzone
        for (uint32_t ns = 1; ns <= 4; ns++) {
            uint32_t vzone_id = ns * 100 + 1;
            UnifiedVZoneMeta* meta = metadata_mgr_.get_vzone(vzone_id);
            
            if (meta && meta->fragmentation_ratio > FRAG_THRESHOLD) {
                trigger_reorganization(vzone_id);
            }
        }
    }
}

void UnifiedController::print_stats() const {
    printf("\n=== eZNS+RBC Unified Controller Statistics ===\n");
    printf("Total reads: %lu\n", stats_.total_reads.load());
    printf("Total writes: %lu\n", stats_.total_writes.load());
    printf("Total allocations: %lu\n", stats_.total_allocations.load());
    printf("Total reorganizations: %lu\n", stats_.total_reorgs.load());
    
    // 元数据统计
    auto meta_stats = metadata_mgr_.get_stats();
    printf("\nMetadata:\n");
    printf("  VZones: %lu\n", meta_stats.total_vzones);
    printf("  Remapped blocks: %lu\n", meta_stats.total_remapped_blocks);
    printf("  Avg fragmentation: %.2f\n", meta_stats.avg_fragmentation);
    printf("  Memory: %.2f MB\n", meta_stats.memory_usage_bytes / (1024.0 * 1024.0));
    printf("  Lookup latency: %.2f us\n", meta_stats.avg_lookup_latency_us);
    
    // 租户隔离统计
    double fairness = isolation_.calculate_fairness_index();
    printf("\nTenant Isolation:\n");
    printf("  Fairness index: %.4f\n", fairness);
    
    auto all_status = isolation_.get_all_tenant_status();
    for (const auto& status : all_status) {
        printf("  Tenant %u: frag=%.2f, lat=%lu us, throttled=%d\n",
               status.namespace_id, status.current_fragmentation,
               status.avg_read_latency_us, status.is_throttled);
    }
    
    printf("================================================\n");
}

} // namespace ezns_rbc