#include "unified_controller.h"
#include <thread>
#include <vector>
#include <random>
#include <chrono>

using namespace ezns_rbc;

// ============================================================================
// 模拟工作负载
// ============================================================================
void workload_thread(UnifiedController* ctrl, uint32_t namespace_id,
                     int num_ops, int read_ratio) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> lba_dist(0, 1000000);
    std::uniform_int_distribution<> lat_dist(50, 500);
    std::uniform_int_distribution<> dirty_dist(0, 99);
    
    // 分配v-zone
    uint32_t vzone_id = ctrl->allocate_vzone(namespace_id, 8);
    if (vzone_id == 0) {
        printf("Thread %u: Failed to allocate vzone\n", namespace_id);
        return;
    }
    
    printf("Thread %u: Allocated vzone %u\n", namespace_id, vzone_id);
    
    for (int i = 0; i < num_ops; i++) {
        uint64_t lba = lba_dist(gen);
        uint64_t latency = lat_dist(gen);
        bool is_read = (gen() % 100) < read_ratio;
        
        if (is_read) {
            ctrl->handle_read(vzone_id, lba, latency);
        } else {
            bool is_dirty = (dirty_dist(gen) < 30);  // 30%脏块
            ctrl->handle_write(vzone_id, lba, lba + 1000000, 4096, is_dirty);
        }
        
        // 模拟I/O间隔
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    ctrl->free_vzone(vzone_id);
    printf("Thread %u: Completed %d ops\n", namespace_id, num_ops);
}

int main() {
    printf("========================================\n");
    printf("eZNS+RBC Integration Test\n");
    printf("========================================\n");
    
    // 创建统一控制器
    UnifiedController ctrl;
    
    // 初始化
    if (!ctrl.init(1024, 256, 100ULL * 1024 * 1024 * 1024)) {
        printf("Failed to initialize controller\n");
        return 1;
    }
    
    // 注册租户
    ctrl.register_tenant(1, TenantType::WRITE_HEAVY, 5);   // 写重
    ctrl.register_tenant(2, TenantType::WRITE_HEAVY, 5);   // 写重
    ctrl.register_tenant(3, TenantType::READ_HEAVY, 8);    // 读重（高优先级）
    ctrl.register_tenant(4, TenantType::READ_HEAVY, 8);    // 读重
    
    printf("\nStarting workload threads...\n");
    
    // 启动工作线程
    std::vector<std::thread> threads;
    
    // 租户1-2：写重（30%读，70%写）
    threads.emplace_back(workload_thread, &ctrl, 1, 5000, 30);
    threads.emplace_back(workload_thread, &ctrl, 2, 5000, 30);
    
    // 租户3-4：读重（90%读，10%写）
    threads.emplace_back(workload_thread, &ctrl, 3, 5000, 90);
    threads.emplace_back(workload_thread, &ctrl, 4, 5000, 90);
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    // 输出统计
    ctrl.print_stats();
    
    printf("\nTest completed successfully!\n");
    return 0;
}