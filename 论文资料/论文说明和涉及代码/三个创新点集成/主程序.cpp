#include "zone_arbiter.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

using namespace ezns_rbc;

// 模拟I/O工作负载
void simulate_workload(ZoneArbiter* arbiter, uint32_t namespace_id, 
                        int num_ops, int read_ratio) {
    
    uint32_t vzone_id = arbiter->allocate_vzone(namespace_id, 4);
    if (vzone_id == 0) {
        std::cerr << "Failed to allocate vzone for NS" << namespace_id << std::endl;
        return;
    }
    
    std::cout << "NS" << namespace_id << " allocated vzone " << vzone_id << std::endl;
    
    for (int i = 0; i < num_ops; i++) {
        bool is_read = (rand() % 100) < read_ratio;
        uint64_t latency = 50 + (rand() % 200);  // 50-250us
        
        if (is_read) {
            arbiter->handle_read(vzone_id, latency);
        } else {
            arbiter->handle_write(vzone_id, latency);
        }
        
        // 偶尔触发借贷
        if (i % 1000 == 0 && i > 0) {
            // 模拟高负载时请求更多资源
            arbiter->allocate_vzone(namespace_id, 2);
        }
        
        // 模拟I/O间隔
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    arbiter->free_vzone(vzone_id);
    std::cout << "NS" << namespace_id << " completed " << num_ops << " ops" << std::endl;
}

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "eZNS+RBC Integration Test" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // 初始化zone仲裁器
    ZoneArbiter arbiter;
    arbiter.init(1024, 256);  // 1024个zone，最多256 active
    
    // 启动多个租户线程
    std::vector<std::thread> threads;
    
    // 租户1：写重（30%读，70%写）
    threads.emplace_back(simulate_workload, &arbiter, 1, 5000, 30);
    
    // 租户2：写重
    threads.emplace_back(simulate_workload, &arbiter, 2, 5000, 30);
    
    // 租户3：读重（90%读，10%写）
    threads.emplace_back(simulate_workload, &arbiter, 3, 5000, 90);
    
    // 租户4：读重
    threads.emplace_back(simulate_workload, &arbiter, 4, 5000, 90);
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    // 输出统计
    arbiter.dump_stats();
    
    // 触发空闲重组
    std::cout << "\nTriggering idle-time reorganization..." << std::endl;
    arbiter.get_balloon()->idle_time_reorganization();
    
    std::cout << "\nAll tests completed successfully!" << std::endl;
    
    return 0;
}