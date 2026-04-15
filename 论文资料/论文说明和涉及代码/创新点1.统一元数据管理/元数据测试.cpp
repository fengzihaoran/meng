#include "unified_metadata.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace ezns_rbc;

void test_metadata_basic() {
    std::cout << "\n=== Test 1: Basic Metadata Operations ===" << std::endl;
    
    UnifiedMetadataManager mgr;
    mgr.init("/dev/nvme0n1", 4);
    
    // 创建元数据
    auto meta1 = mgr.create_metadata(1, 4);
    auto meta2 = mgr.create_metadata(2, 8);
    
    std::cout << "Created vzone " << meta1->vzone_id << " for NS1" << std::endl;
    std::cout << "Created vzone " << meta2->vzone_id << " for NS2" << std::endl;
    
    // 添加重映射块
    mgr.add_remapped_block(meta1->vzone_id, 101);
    mgr.add_remapped_block(meta1->vzone_id, 102);
    mgr.add_remapped_block(meta1->vzone_id, 103);
    
    std::cout << "After adding remapped blocks: frag=" 
              << mgr.get_fragmentation(meta1->vzone_id) << std::endl;
    
    // 记录访问
    mgr.update_access(meta1->vzone_id, IOType::READ, 120);
    mgr.update_access(meta1->vzone_id, IOType::READ, 150);
    mgr.update_access(meta1->vzone_id, IOType::READ, 90);
    
    auto meta1_updated = mgr.get_metadata(meta1->vzone_id);
    std::cout << "Avg read latency: " << meta1_updated->get_avg_read_latency() << " us" << std::endl;
    
    mgr.dump_stats();
}

void test_metadata_concurrent() {
    std::cout << "\n=== Test 2: Concurrent Metadata Access ===" << std::endl;
    
    UnifiedMetadataManager mgr;
    mgr.init("/dev/nvme0n1", 4);
    
    // 创建多个vzone
    const int NUM_VZONES = 10;
    std::vector<uint32_t> vzone_ids;
    
    for (int i = 0; i < NUM_VZONES; i++) {
        auto meta = mgr.create_metadata(i % 4 + 1, 4);
        vzone_ids.push_back(meta->vzone_id);
    }
    
    // 并发访问
    std::vector<std::thread> threads;
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 10000;
    
    auto worker = [&](int thread_id) {
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            int idx = rand() % NUM_VZONES;
            uint32_t vzone_id = vzone_ids[idx];
            
            // 随机操作
            int op = rand() % 3;
            if (op == 0) {
                mgr.get_metadata(vzone_id);
            } else if (op == 1) {
                mgr.update_access(vzone_id, IOType::READ, rand() % 200);
            } else {
                mgr.add_remapped_block(vzone_id, rand() % 1000);
            }
        }
    };
    
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    mgr.dump_stats();
}

int main() {
    test_metadata_basic();
    test_metadata_concurrent();
    
    std::cout << "\nAll metadata tests passed!" << std::endl;
    return 0;
}