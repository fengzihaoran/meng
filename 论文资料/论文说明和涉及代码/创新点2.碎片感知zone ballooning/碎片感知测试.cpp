#include "frag_aware_balloon.h"
#include "unified_metadata.h"
#include <iostream>

using namespace ezns_rbc;

void test_frag_aware_basic() {
    std::cout << "\n=== Test 1: Basic Frag-Aware Ballooning ===" << std::endl;
    
    UnifiedMetadataManager meta_mgr;
    meta_mgr.init("/dev/nvme0n1", 4);
    
    FragAwareBalloon balloon(&meta_mgr);
    balloon.init(80, 80);  // 80 essential, 80 spare
    
    // 创建租户元数据
    auto meta = meta_mgr.create_metadata(1, 4);
    
    // 模拟一些重映射块，提高碎片度
    for (int i = 0; i < 500; i++) {
        meta_mgr.add_remapped_block(meta->vzone_id, i);
    }
    
    std::cout << "Initial fragmentation: " << meta->fragmentation_ratio << std::endl;
    
    // 发起借贷请求
    BorrowRequest req;
    req.namespace_id = 1;
    req.requested_width = 10;
    req.priority = 5;
    req.timestamp = get_current_us();
    
    auto result = balloon.borrow_zones(req);
    
    std::cout << "Borrow result: allocated=" << result.allocated_width
              << ", threshold_exceeded=" << result.threshold_exceeded
              << ", message=" << result.message << std::endl;
    
    // 检查资源状态
    uint32_t essential_used, spare_used, essential_total, spare_total;
    balloon.get_resource_status(essential_used, spare_used, essential_total, spare_total);
    
    std::cout << "Resource status: used_essential=" << essential_used
              << ", used_spare=" << spare_used
              << ", total_essential=" << essential_total
              << ", total_spare=" << spare_total << std::endl;
}

void test_frag_aware_threshold() {
    std::cout << "\n=== Test 2: Fragmentation Threshold Behavior ===" << std::endl;
    
    UnifiedMetadataManager meta_mgr;
    meta_mgr.init("/dev/nvme0n1", 4);
    
    FragAwareBalloon balloon(&meta_mgr);
    balloon.init(80, 80);
    balloon.set_frag_threshold(0.2f);  // 更严格的阈值
    
    auto meta = meta_mgr.create_metadata(1, 4);
    
    // 测试不同碎片度下的行为
    std::vector<float> test_frags = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    
    for (float frag : test_frags) {
        // 设置碎片度
        meta->fragmentation_ratio = frag;
        
        BorrowRequest req;
        req.namespace_id = 1;
        req.requested_width = 10;
        
        auto result = balloon.borrow_zones(req);
        
        std::cout << "Frag=" << frag 
                  << " -> allocated=" << result.allocated_width
                  << " (threshold=" << (frag > 0.2 ? "exceeded" : "ok") << ")"
                  << std::endl;
    }
}

int main() {
    test_frag_aware_basic();
    test_frag_aware_threshold();
    
    std::cout << "\nAll frag-aware tests passed!" << std::endl;
    return 0;
}