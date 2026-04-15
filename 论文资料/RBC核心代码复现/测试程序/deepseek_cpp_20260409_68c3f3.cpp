#include "rbc_controller.h"
#include <random>
#include <thread>

using namespace rbc;

void simulate_workload(RBCController* rbc, int num_ops) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> addr_dist(0, 1000000);
    std::uniform_int_distribution<> dirty_dist(0, 99);
    
    for (int i = 0; i < num_ops; i++) {
        uint64_t lba = addr_dist(gen);
        uint64_t pba;
        uint32_t size = 4096;  // 4KB
        bool is_remapped;
        
        // 70%读，30%写
        if (i % 10 < 7) {
            rbc->handle_read(lba, pba, size, is_remapped);
        } else {
            bool is_dirty = (dirty_dist(gen) < 30);  // 30%脏块
            rbc->handle_write(lba, lba + 1000000, size, is_dirty);
        }
        
        // 每1000次操作触发一次压缩
        if (i % 1000 == 0 && i > 0) {
            float dirty_ratio = 0.3f + (i % 5) * 0.1f;  // 30%-70%
            rbc->before_compaction(dirty_ratio);
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

int main() {
    printf("========================================\n");
    printf("RBC Test Program\n");
    printf("========================================\n");
    
    RBCController rbc;
    rbc.init(100ULL * 1024 * 1024 * 1024);  // 100GB
    
    // 启动多个工作线程
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back(simulate_workload, &rbc, 10000);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    rbc.print_stats();
    
    printf("\nTest completed!\n");
    return 0;
}