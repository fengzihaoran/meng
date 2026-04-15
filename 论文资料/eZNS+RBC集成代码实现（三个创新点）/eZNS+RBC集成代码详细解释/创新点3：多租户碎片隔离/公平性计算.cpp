double TenantIsolation::calculate_fairness_index() const {
    auto all_status = get_all_tenant_status();
    
    // 使用1/碎片度作为公平性指标（碎片度越低越公平）
    std::vector<double> metrics;
    for (const auto& status : all_status) {
        double metric = 1.0 / (status.current_fragmentation + 0.1);
        metrics.push_back(metric);
    }
    
    // Jain's fairness index
    double sum = std::accumulate(metrics.begin(), metrics.end(), 0.0);
    double sum_sq = std::inner_product(metrics.begin(), metrics.end(),
                                       metrics.begin(), 0.0);
    
    return (sum * sum) / (metrics.size() * sum_sq);
}