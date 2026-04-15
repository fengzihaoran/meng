#include "tenant_isolation.h"

namespace ezns_rbc {

TenantIsolation::TenantIsolation(UnifiedMetadataManager* metadata_mgr)
    : metadata_mgr_(metadata_mgr) {
    LOG_INFO("TenantIsolation initialized");
}

TenantIsolation::~TenantIsolation() {
    LOG_INFO("TenantIsolation destroyed, stats: "
             << "checks=" << stats_.total_permission_checks
             << ", allowed=" << stats_.total_checks_allowed
             << ", denied=" << stats_.total_checks_denied
             << ", throttle=" << stats_.total_throttle_events);
}

bool TenantIsolation::register_tenant(const TenantConfig& config) {
    std::unique_lock lock(config_mutex_);
    
    // 检查是否已存在
    if (tenant_configs_.find(config.namespace_id) != tenant_configs_.end()) {
        LOG_ERROR("Tenant " << config.namespace_id << " already registered");
        return false;
    }
    
    // 设置默认值
    TenantConfig cfg = config;
    if (cfg.max_fragmentation <= 0) cfg.max_fragmentation = FRAG_THRESHOLD;
    if (cfg.min_essential_zones == 0) cfg.min_essential_zones = 2;
    if (cfg.max_spare_zones == 0) cfg.max_spare_zones = 8;
    if (cfg.latency_sla_us == 0) cfg.latency_sla_us = 1000;  // 1ms默认
    
    tenant_configs_[cfg.namespace_id] = cfg;
    
    LOG_INFO("Registered tenant " << cfg.namespace_id 
              << " type=" << static_cast<int>(cfg.type)
              << " max_frag=" << cfg.max_fragmentation);
    
    return true;
}

bool TenantIsolation::update_tenant_config(uint32_t namespace_id, 
                                            const TenantConfig& config) {
    std::unique_lock lock(config_mutex_);
    
    auto it = tenant_configs_.find(namespace_id);
    if (it == tenant_configs_.end()) {
        LOG_ERROR("Tenant " << namespace_id << " not found");
        return false;
    }
    
    it->second = config;
    LOG_INFO("Updated tenant " << namespace_id << " config");
    
    return true;
}

TenantStatus TenantIsolation::get_tenant_status(uint32_t namespace_id) const {
    TenantStatus status;
    status.namespace_id = namespace_id;
    
    // 获取租户的元数据（假设每个租户有一个主vzone）
    uint32_t vzone_id = namespace_id * 100 + 1;
    auto meta = metadata_mgr_->get_metadata(vzone_id);
    
    if (meta) {
        status.current_fragmentation = meta->fragmentation_ratio;
        status.essential_allocated = meta->essential_allocated;
        status.spare_allocated = meta->spare_allocated;
        status.avg_read_latency_us = static_cast<uint64_t>(meta->get_avg_read_latency());
        status.p99_read_latency_us = status.avg_read_latency_us * 2;  // 简化
        status.active_vzones = 1;  // 简化
    } else {
        status.current_fragmentation = 0;
        status.essential_allocated = 0;
        status.spare_allocated = 0;
        status.avg_read_latency_us = 0;
        status.p99_read_latency_us = 0;
        status.active_vzones = 0;
    }
    
    // 检查是否被限流
    {
        std::shared_lock lock(throttle_mutex_);
        auto it = throttled_tenants_.find(namespace_id);
        status.is_throttled = (it != throttled_tenants_.end());
        status.throttling_reason = status.is_throttled ? it->second : "";
    }
    
    return status;
}

BorrowDecision TenantIsolation::check_borrow_permission(uint32_t lender_id,
                                                         uint32_t borrower_id,
                                                         uint32_t requested_width) {
    stats_.total_permission_checks++;
    
    BorrowDecision decision;
    decision.allowed = true;
    decision.max_borrow = requested_width;
    decision.reason = "OK";
    
    // 获取租户配置
    TenantConfig lender_cfg, borrower_cfg;
    {
        std::shared_lock lock(config_mutex_);
        
        auto lit = tenant_configs_.find(lender_id);
        if (lit == tenant_configs_.end()) {
            decision.allowed = false;
            decision.reason = "Lender not registered";
            stats_.total_checks_denied++;
            return decision;
        }
        lender_cfg = lit->second;
        
        auto bit = tenant_configs_.find(borrower_id);
        if (bit == tenant_configs_.end()) {
            decision.allowed = false;
            decision.reason = "Borrower not registered";
            stats_.total_checks_denied++;
            return decision;
        }
        borrower_cfg = bit->second;
    }
    
    // 1. 检查出借者是否被限流
    {
        std::shared_lock lock(throttle_mutex_);
        if (throttled_tenants_.find(lender_id) != throttled_tenants_.end()) {
            decision.allowed = false;
            decision.reason = "Lender is throttled";
            stats_.total_checks_denied++;
            return decision;
        }
    }
    
    // 2. 预测借贷后的碎片影响
    float frag_impact = predict_frag_impact(lender_id, borrower_id, requested_width);
    float new_frag = lender_cfg.max_fragmentation + frag_impact;
    
    if (new_frag > lender_cfg.max_fragmentation * 1.2) {  // 允许20%超出
        decision.allowed = false;
        decision.max_borrow = static_cast<uint32_t>(
            requested_width * (lender_cfg.max_fragmentation / new_frag));
        decision.reason = "Fragmentation impact too high";
        stats_.total_checks_denied++;
        LOG_DEBUG("Denied borrow from " << lender_id << " to " << borrower_id
                  << ": frag impact " << frag_impact);
        return decision;
    }
    
    // 3. 检查延迟SLA
    if (!check_latency_sla(lender_id)) {
        decision.allowed = false;
        decision.reason = "Lender latency SLA violation risk";
        stats_.total_checks_denied++;
        return decision;
    }
    
    stats_.total_checks_allowed++;
    return decision;
}

void TenantIsolation::throttle_tenant(uint32_t namespace_id, const std::string& reason) {
    std::unique_lock lock(throttle_mutex_);
    throttled_tenants_[namespace_id] = reason;
    stats_.total_throttle_events++;
    
    LOG_WARN("Tenant " << namespace_id << " throttled: " << reason);
}

void TenantIsolation::unthrottle_tenant(uint32_t namespace_id) {
    std::unique_lock lock(throttle_mutex_);
    throttled_tenants_.erase(namespace_id);
    LOG_INFO("Tenant " << namespace_id << " unthrottled");
}

std::vector<uint32_t> TenantIsolation::detect_noisy_neighbors(uint32_t victim_id) const {
    std::vector<uint32_t> noisy;
    stats_.total_noisy_detections++;
    
    auto victim_status = get_tenant_status(victim_id);
    
    // 获取所有租户状态
    auto all_status = get_all_tenant_status();
    
    for (const auto& status : all_status) {
        if (status.namespace_id == victim_id) continue;
        
        // 检测条件：
        // 1. 对方碎片度远高于受害者
        // 2. 对方带宽占用过高
        // 3. 受害者延迟超出SLA
        if (status.current_fragmentation > victim_status.current_fragmentation * 2 &&
            status.current_fragmentation > FRAG_THRESHOLD) {
            noisy.push_back(status.namespace_id);
            LOG_DEBUG("Detected noisy neighbor: " << status.namespace_id 
                      << " frag=" << status.current_fragmentation);
        }
    }
    
    return noisy;
}

double TenantIsolation::calculate_fairness_index() const {
    auto all_status = get_all_tenant_status();
    
    if (all_status.empty()) return 1.0;
    
    // 计算带宽公平性（简化：用1/碎片度作为公平性指标）
    std::vector<double> metrics;
    for (const auto& status : all_status) {
        // 公平性指标：碎片度越低越公平
        double metric = 1.0 / (status.current_fragmentation + 0.1);
        metrics.push_back(metric);
    }
    
    // Jain's fairness index
    double sum = 0, sum_sq = 0;
    for (double m : metrics) {
        sum += m;
        sum_sq += m * m;
    }
    
    double fairness = (sum * sum) / (metrics.size() * sum_sq);
    return fairness;
}

std::vector<TenantStatus> TenantIsolation::get_all_tenant_status() const {
    std::vector<TenantStatus> statuses;
    
    std::shared_lock lock(config_mutex_);
    for (const auto& pair : tenant_configs_) {
        statuses.push_back(get_tenant_status(pair.first));
    }
    
    return statuses;
}

float TenantIsolation::predict_frag_impact(uint32_t lender_id, uint32_t borrower_id,
                                            uint32_t borrow_amount) const {
    // 简化预测模型：每借1个zone，碎片度增加0.5%
    return borrow_amount * 0.005f;
}

bool TenantIsolation::check_latency_sla(uint32_t namespace_id) const {
    auto status = get_tenant_status(namespace_id);
    
    // 获取租户配置
    TenantConfig cfg;
    {
        std::shared_lock lock(config_mutex_);
        auto it = tenant_configs_.find(namespace_id);
        if (it == tenant_configs_.end()) return true;
        cfg = it->second;
    }
    
    // 检查平均延迟是否接近SLA
    return status.avg_read_latency_us < cfg.latency_sla_us * 0.8;  // 留20%余量
}

void TenantIsolation::update_tenant_status(uint32_t namespace_id) {
    // 定期更新租户状态，可由外部定时调用
    auto status = get_tenant_status(namespace_id);
    
    // 如果延迟超过SLA，自动限流
    if (status.avg_read_latency_us > 1000) {  // 1ms阈值
        throttle_tenant(namespace_id, "Latency SLA violation");
    }
}

} // namespace ezns_rbc