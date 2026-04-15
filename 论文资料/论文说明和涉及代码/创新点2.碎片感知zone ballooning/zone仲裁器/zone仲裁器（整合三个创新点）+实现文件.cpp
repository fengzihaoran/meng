#include "zone_arbiter.h"

namespace ezns_rbc {

ZoneArbiter::ZoneArbiter() 
    : total_zones_(0), max_active_zones_(0),
      essential_per_namespace_(0), spare_per_namespace_(0) {
    
    metadata_mgr_ = std::make_unique<UnifiedMetadataManager>();
    balloon_ = std::make_unique<FragAwareBalloon>(metadata_mgr_.get());
    isolation_ = std::make_unique<TenantIsolation>(metadata_mgr_.get());
    
    LOG_INFO("ZoneArbiter created");
}

ZoneArbiter::~ZoneArbiter() {
    dump_stats();
    LOG_INFO("ZoneArbiter destroyed");
}

bool ZoneArbiter::init(uint32_t total_zones, uint32_t max_active_zones) {
    total_zones_ = total_zones;
    max_active_zones_ = max_active_zones;
    
    // 计算资源分配
    // essential: 能够达到最大写带宽的最小zone数（假设每zone 40MB/s，PCIe 3.2GB/s，需要80个）
    uint32_t essential_needed = 80;
    uint32_t total_essential = std::min(essential_needed, max_active_zones / 2);
    uint32_t total_spare = max_active_zones - total_essential;
    
    // 初始化各组件
    metadata_mgr_->init("/dev/nvme0n1", 4);  // 4个namespace
    balloon_->init(total_essential, total_spare);
    
    // 注册默认租户
    for (uint32_t ns = 1; ns <= 4; ns++) {
        TenantConfig cfg;
        cfg.namespace_id = ns;
        cfg.type = (ns <= 2) ? TenantType::WRITE_HEAVY : TenantType::READ_HEAVY;
        cfg.max_fragmentation = FRAG_THRESHOLD;
        cfg.min_essential_zones = total_essential / 4;
        cfg.max_spare_zones = total_spare / 2;
        cfg.latency_sla_us = (ns <= 2) ? 2000 : 500;  // 写重租户容忍更高延迟
        cfg.priority = (ns <= 2) ? 5 : 8;  // 读重租户更高优先级
        
        isolation_->register_tenant(cfg);
    }
    
    LOG_INFO("ZoneArbiter initialized: total_zones=" << total_zones
             << ", max_active=" << max_active_zones
             << ", essential=" << total_essential
             << ", spare=" << total_spare);
    
    return true;
}

uint32_t ZoneArbiter::allocate_vzone(uint32_t namespace_id, uint32_t requested_width) {
    LOG_DEBUG("Allocating vzone for NS" << namespace_id << " width=" << requested_width);
    
    // 1. 检查租户隔离
    auto decision = isolation_->check_borrow_permission(namespace_id, namespace_id, 
                                                         requested_width);
    if (!decision.allowed) {
        LOG_WARN("Allocation denied for NS" << namespace_id << ": " << decision.reason);
        return 0;
    }
    
    // 2. 创建元数据
    auto meta = metadata_mgr_->create_metadata(namespace_id, requested_width);
    if (!meta) {
        LOG_ERROR("Failed to create metadata for NS" << namespace_id);
        return 0;
    }
    
    uint32_t vzone_id = meta->vzone_id;
    
    // 3. 发起借贷请求
    BorrowRequest req;
    req.namespace_id = namespace_id;
    req.requested_width = requested_width;
    req.priority = 5;
    req.timestamp = get_current_us();
    
    auto result = balloon_->borrow_zones(req);
    
    if (result.allocated_width > 0) {
        meta->spare_allocated = result.allocated_width;
        meta->essential_allocated = 2;  // 默认
        stats_.total_borrow_events++;
        
        LOG_INFO("Allocated vzone " << vzone_id << " for NS" << namespace_id
                  << " with width=" << result.allocated_width
                  << " frag=" << result.new_frag_ratio);
    } else {
        LOG_WARN("Failed to allocate vzone for NS" << namespace_id 
                  << ": " << result.message);
    }
    
    return vzone_id;
}

bool ZoneArbiter::free_vzone(uint32_t vzone_id) {
    auto meta = metadata_mgr_->get_metadata(vzone_id);
    if (!meta) {
        LOG_ERROR("Cannot free vzone " << vzone_id << ": not found");
        return false;
    }
    
    // 归还spare zones
    if (meta->spare_allocated > 0) {
        balloon_->return_zones(meta->namespace_id, meta->spare_allocated);
    }
    
    LOG_INFO("Freed vzone " << vzone_id);
    return true;
}

void ZoneArbiter::handle_read(uint32_t vzone_id, uint64_t latency_us) {
    stats_.total_io_requests++;
    stats_.total_reads++;
    
    metadata_mgr_->update_access(vzone_id, IOType::READ, latency_us);
}

void ZoneArbiter::handle_write(uint32_t vzone_id, uint64_t latency_us) {
    stats_.total_io_requests++;
    stats_.total_writes++;
    
    metadata_mgr_->update_access(vzone_id, IOType::WRITE, latency_us);
}

void ZoneArbiter::trigger_reorganization(uint32_t vzone_id) {
    stats_.total_reorg_events++;
    balloon_->trigger_reorganization(vzone_id);
}

void ZoneArbiter::get_stats(uint64_t& total_io, uint64_t& total_borrow,
                             uint64_t& total_reorg, double& fairness) const {
    total_io = stats_.total_io_requests.load();
    total_borrow = stats_.total_borrow_events.load();
    total_reorg = stats_.total_reorg_events.load();
    fairness = isolation_->calculate_fairness_index();
}

void ZoneArbiter::dump_stats(std::ostream& os) const {
    os << "=== ZoneArbiter Global Stats ===" << std::endl;
    os << "Total I/O requests: " << stats_.total_io_requests.load() << std::endl;
    os << "  Reads: " << stats_.total_reads.load() << std::endl;
    os << "  Writes: " << stats_.total_writes.load() << std::endl;
    os << "Total borrow events: " << stats_.total_borrow_events.load() << std::endl;
    os << "Total reorg events: " << stats_.total_reorg_events.load() << std::endl;
    
    double fairness = isolation_->calculate_fairness_index();
    os << "Fairness index: " << fairness << std::endl;
    os << "================================" << std::endl;
    
    metadata_mgr_->dump_stats(os);
}

} // namespace ezns_rbc