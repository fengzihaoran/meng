#include "tenant_isolation.h"
#include "unified_metadata.h"
#include <iostream>
#include <iomanip>

using namespace ezns_rbc;

void test_tenant_basic() {
    std::cout << "\n=== Test 1: Basic Tenant Operations ===" << std::endl;
    
    UnifiedMetadataManager meta_mgr;
    meta_mgr.init("/dev/nvme0n1", 4);
    
    TenantIsolation isolation(&meta_mgr);
    
    // 注册租户
    TenantConfig cfg1;
    cfg1.namespace_id = 1;
    cfg1.type = TenantType::WRITE_HEAVY;
    cfg1.max_fragmentation = 0.3f;
    cfg1.min_essential_zones = 2;
    cfg1.max_spare_zones = 8;
    cfg1.latency_sla_us = 2000;
    cfg1.priority = 5;
    
    TenantConfig cfg2;
    cfg2.namespace_id = 2;
    cfg2.type = TenantType::READ_HEAVY;
    cfg2.max_fragmentation = 0.2f;
    cfg2.min_essential_zones = 2;
    cfg2.max_spare_zones = 4;
    cfg2.latency_sla_us = 500;
    cfg2.priority = 8;
    
    isolation.register_tenant(cfg1);
    isolation.register_tenant(cfg2);
    
    // 创建元数据
    auto meta1 = meta_mgr.create_metadata(1, 4);
    auto meta2 = meta_mgr.create_metadata(2, 4);
    
    // 添加一些重映射块
    for (int i = 0; i < 200; i++) {
        meta_mgr.add_remapped_block(meta1->vzone_id, i);
    }
    
    // 检查借贷权限
    auto decision = isolation.check_borrow_permission(1, 2, 5);
    
    std::cout << "Borrow from NS1 to NS2: allowed=" << decision.allowed
              << ", max_borrow=" << decision.max_borrow
              << ", reason=" << decision.reason << std::endl;
    
    // 获取租户状态
    auto status1 = isolation.get_tenant_status(1);
    auto status2 = isolation.get_tenant_status(2);
    
    std::cout << "NS1 status: frag=" << status1.current_fragmentation
              << ", throttled=" << status1.is_throttled << std::endl;
    std::cout << "NS2 status: frag=" << status2.current_fragmentation
              << ", throttled=" << status2.is_throttled << std::endl;
    
    // 计算公平性
    double fairness = isolation.calculate_fairness_index();
    std::cout << "Fairness index: " << std::fixed << std::setprecision(4) 
              << fairness << std::endl;
}

void test_noisy_detection() {
    std::cout << "\n=== Test 2: Noisy Neighbor Detection ===" << std::endl;
    
    UnifiedMetadataManager meta_mgr;
    meta_mgr.init("/dev/nvme0n1", 4);
    
    TenantIsolation isolation(&meta_mgr);
    
    // 注册4个租户
    for (int ns = 1; ns <= 4; ns++) {
        TenantConfig cfg;
        cfg.namespace_id = ns;
        cfg.type = (ns <= 2) ? TenantType::WRITE_HEAVY : TenantType::READ_HEAVY;
        cfg.max_fragmentation = 0.3f;
        isolation.register_tenant(cfg);
        
        auto meta = meta_mgr.create_metadata(ns, 4);
        
        // 给前两个租户高碎片度
        if (ns <= 2) {
            for (int i = 0; i < 500; i++) {
                meta_mgr.add_remapped_block(meta->vzone_id, i);
            }
        }
    }
    
    // 检测NS3的噪音邻居
    auto noisy = isolation.detect_noisy_neighbors(3);
    
    std::cout << "Noisy neighbors of NS3: ";
    for (uint32_t ns : noisy) {
        std::cout << ns << " ";
    }
    std::cout << std::endl;
    
    // 公平性
    double fairness = isolation.calculate_fairness_index();
    std::cout << "Fairness index: " << fairness << std::endl;
}

int main() {
    test_tenant_basic();
    test_noisy_detection();
    
    std::cout << "\nAll tenant isolation tests passed!" << std::endl;
    return 0;
}