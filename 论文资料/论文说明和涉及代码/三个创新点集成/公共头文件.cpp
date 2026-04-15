#ifndef EZNS_RBC_COMMON_H
#define EZNS_RBC_COMMON_H

#include <cstdint>
#include <vector>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>

// ============================================================================
// 常量定义
// ============================================================================

// 碎片度阈值（参考RBC论文30%）
constexpr float FRAG_THRESHOLD = 0.3f;

// 借贷减少因子（碎片度过高时减少30%）
constexpr float BORROW_REDUCTION_FACTOR = 0.7f;

// 重组触发阈值
constexpr uint32_t REORG_THRESHOLD = 1000;  // 1000次I/O

// 空闲带宽阈值（低于30%认为空闲）
constexpr float IDLE_BANDWIDTH_THRESHOLD = 0.3f;

// 默认zone大小（256MB）
constexpr uint64_t DEFAULT_ZONE_SIZE = 256 * 1024 * 1024;

// 默认最大active zones
constexpr uint32_t DEFAULT_MAX_ACTIVE_ZONES = 16;

// 延迟阈值（500us，参考eZNS论文）
constexpr uint64_t LATENCY_THRESHOLD_US = 500;

// ============================================================================
// 类型定义
// ============================================================================

// zone状态
enum class ZoneState : uint8_t {
    EMPTY,
    IMPLICIT_OPEN,
    EXPLICIT_OPEN,
    CLOSED,
    FULL,
    READ_ONLY,
    OFFLINE
};

// I/O类型
enum class IOType : uint8_t {
    READ,
    WRITE,
    RESET,
    FINISH
};

// 租户类型
enum class TenantType : uint8_t {
    WRITE_HEAVY,
    READ_HEAVY,
    MIXED,
    UNKNOWN
};

// ============================================================================
// 结构体定义
// ============================================================================

// 物理zone信息（来自eZNS）
struct PhysicalZone {
    uint32_t zone_id;           // zone ID
    uint64_t start_lba;         // 起始LBA
    uint64_t zone_size;         // zone大小
    uint32_t die_id;            // 所属die
    uint32_t channel_id;        // 所属channel
    uint64_t write_pointer;     // 写指针位置
    bool is_active;             // 是否active
    uint32_t read_count;        // 读计数
    uint32_t write_count;       // 写计数
    ZoneState state;            // 当前状态
};

// 时间戳工具
inline uint64_t get_current_us() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

// 日志工具
#define LOG_INFO(msg) \
    std::cout << "[INFO][" << get_current_us() << "] " << msg << std::endl

#define LOG_DEBUG(msg) \
    std::cout << "[DEBUG][" << get_current_us() << "] " << msg << std::endl

#define LOG_ERROR(msg) \
    std::cerr << "[ERROR][" << get_current_us() << "] " << msg << std::endl

#endif // EZNS_RBC_COMMON_H