#include "tenant_isolation.h"
#include <cmath>
#include <numeric>

namespace ezns_rbc {

TenantIsolation::TenantIsolation(UnifiedMetadataManager* metadata_mgr)
    : metadata_mgr_(metadata_mgr) {
    printf("[TenantIsolation] Created\n");
}

TenantIsolation::~TenantIsolation() {
    printf("[TenantIsolation] Destroyed: checks=%lu, allowed=%lu, denied=%lu, throttle=%lu\n",
           stats_.total_checks, stats_.total_allowed,
           stats_.total_denied, stats_.total_throttle_events);
}

bool TenantIsolation::register_tenant(const TenantConfig& config) {
    std::unique_lock lock(rw_mutex_);
    
    if (tenant_configs_.find(config.namespace_id) != tenant_configs_.end()) {
        return false;
    }
    
    tenant_configs_[config.namespace_id] = config;
    
    printf("[TenantIsolation] Registered tenant %u (type=%d, max_frag=%.2f, priority=%u)\n",
           config.namespace_id, static_cast<int>(config.type),
           config.max_fragmentation, config.priority);
    
    return true;
}

TenantStatus TenantIsolation::get_tenant_status(uint32_t namespace_id) const {
    TenantStatus status;
    status.namespace_id = namespace_id;
    
    std::shared_lock lock(rw_mutex_);
    
    auto config_it = tenant_configs_.find(namespace_id);
    if (config_it != tenant_configs_.end()) {
        status.type = config_it->second.type;
    }
    
    // 从元数据管理器获取v-zone信息
    uint32_t vzone_id = namespace_id * 100 + 1;
    UnifiedVZoneMeta* meta = metadata_mgr_->get_vzone(vzone_id);
    
    if (meta) {
        status.current_fragmentation = meta->fragmentation_ratio;
        status.essential_allocated = meta->essential_allocated;
        status.spare_allocated = meta->spare_allocated;
        status.avg_read_latency_us = static_cast<uint64_t>(meta->get_avg_read_latency());
        status.p99_read_latency_us = status.avg_read_latency_us * 1.5;  // 估算
        status.active_vzones = 1;
    }
    
    // 检查是否被限流
    auto throttle_it = throttled_tenants_.find(namespace_id);
    status.is_throttled = (throttle_it != throttled_tenants_.end());
    status.throttling_reason = status.is_throttled ? throttle_it->second : "";
    
    return status;
}

float TenantIsolation::predict_frag_impact(uint32_t lender_id, uint32_t amount) const {
    // 简化预测：每借1个zone，碎片度增加1%
    return amount * 0.01f;
}

bool TenantIsolation::check_latency_sla(uint32_t namespace_id) const {
    auto config_it = tenant_configs_.find(namespace_id);
    if (config_it == tenant_configs_.end()) return true;
    
    auto status = get_tenant_status(namespace_id);
    return status.avg_read_latency_us < config_it->second.latency_sla_us;
}

BorrowDecision TenantIsolation::check_borrow_permission(uint32_t lender_id,
                                                         uint32_t borrower_id,
                                                         uint32_t requested_width) {
    stats_.total_checks++;
    
    BorrowDecision decision;
    decision.allowed = true;
    decision.max_borrow = requested_width;
    decision.reason = "OK";
    
    std::shared_lock lock(rw_mutex_);
    
    // 检查出借者是否被限流
    if (throttled_tenants_.find(lender_id) != throttled_tenants_.end()) {
        decision.allowed = false;
        decision.reason = "Lender is throttled";
        stats_.total_denied++;
        return decision;
    }
    
    // 获取租户配置
    auto lender_it = tenant_configs_.find(lender_id);
    auto borrower_it = tenant_configs_.find(borrower_id);
    
    if (lender_it == tenant_configs_.end() || borrower_it == tenant_configs_.end()) {
        decision.allowed = false;
        decision.reason = "Tenant not registered";
        stats_.total_denied++;
        return decision;
    }
    
    // 预测借贷后的碎片影响
    float frag_impact = predict_frag_impact(lender_id, requested_width);
    
    // 获取当前碎片度
    uint32_t lender_vzone = lender_id * 100 + 1;
    UnifiedVZoneMeta* lender_meta = metadata_mgr_->get_vzone(lender_vzone);
    
    if (lender_meta) {
        float new_frag = lender_meta->fragmentation_ratio + frag_impact;
        
        // 检查是否超过租户的最大允许碎片度
        if (new_frag > lender_it->second.max_fragmentation) {
            // 计算允许的最大借贷量
            float allowed_increase = lender_it->second.max_fragmentation - 
                                      lender_meta->fragmentation_ratio;
            uint32_t allowed_borrow = static_cast<uint32_t>(allowed_increase * 100);
            
            decision.allowed = false;
            decision.max_borrow = std::min(requested_width, allowed_borrow);
            decision.reason = "Would exceed max fragmentation";
            stats_.total_denied++;
            
            printf("[TenantIsolation] Denied borrow: lender=%u frag would exceed %.2f\n",
                   lender_id, lender_it->second.max_fragmentation);
            return decision;
        }
    }
    
    // 检查延迟SLA
    if (!check_latency_sla(lender_id)) {
        decision.allowed = false;
        decision.reason = "Latency SLA violation risk";
        stats_.total_denied++;
        return decision;
    }
    
    stats_.total_allowed++;
    return decision;
}

std::vector<uint32_t> TenantIsolation::detect_noisy_neighbors(uint32_t victim_id) const {
    std::vector<uint32_t> noisy;
    
    auto victim_status = get_tenant_status(victim_id);
    auto all_status = get_all_tenant_status();
    
    for (const auto& status : all_status) {
        if (status.namespace_id == victim_id) continue;
        
        // 检测条件：碎片度远高于受害者，且超过阈值
        if (status.current_fragmentation > victim_status.current_fragmentation * 1.5 &&
            status.current_fragmentation > FRAG_THRESHOLD) {
            noisy.push_back(status.namespace_id);
        }
    }
    
    if (!noisy.empty()) {
        printf("[TenantIsolation] Detected noisy neighbors for victim %u: ", victim_id);
        for (auto n : noisy) printf("%u ", n);
        printf("\n");
    }
    
    return noisy;
}

double TenantIsolation::calculate_fairness_index() const {
    auto all_status = get_all_tenant_status();
    
    if (all_status.empty()) return 1.0;
    
    // 使用带宽（1/碎片度）作为公平性指标
    std::vector<double> metrics;
    for (const auto& status : all_status) {
        double metric = 1.0 / (status.current_fragmentation + 0.1);
        metrics.push_back(metric);
    }
    
    // Jain's fairness index
    double sum = std::accumulate(metrics.begin(), metrics.end(), 0.0);
    double sum_sq = std::inner_product(metrics.begin(), metrics.end(),
                                       metrics.begin(), 0.0);
    
    double fairness = (sum * sum) / (metrics.size() * sum_sq);
    return fairness;
}

void TenantIsolation::throttle_tenant(uint32_t namespace_id, const std::string& reason) {
    std::unique_lock lock(rw_mutex_);
    throttled_tenants_[namespace_id] = reason;
    stats_.total_throttle_events++;
    printf("[TenantIsolation] Throttled tenant %u: %s\n", namespace_id, reason.c_str());
}

void TenantIsolation::unthrottle_tenant(uint32_t namespace_id) {
    std::unique_lock lock(rw_mutex_);
    throttled_tenants_.erase(namespace_id);
    printf("[TenantIsolation] Unthrottled tenant %u\n", namespace_id);
}

std::vector<TenantStatus> TenantIsolation::get_all_tenant_status() const {
    std::vector<TenantStatus> statuses;
    
    std::shared_lock lock(rw_mutex_);
    for (const auto& pair : tenant_configs_) {
        statuses.push_back(get_tenant_status(pair.first));
    }
    
    return statuses;
}

} // namespace ezns_rbc