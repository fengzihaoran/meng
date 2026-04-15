#ifndef RBC_COMPACTION_H
#define RBC_COMPACTION_H

#include <cstdint>
#include <vector>

namespace rbc {

// ============================================================================
// 脏块阈值（论文Section 3.3）
// ============================================================================
constexpr float DIRTY_BLOCK_THRESHOLD = 0.8f;    // p = 0.8
constexpr float LOWER_THRESHOLD = 0.4f;          // p/2 = 0.4

// KV对元数据
struct KVMetadata {
    uint64_t key;
    uint32_t block_id;      // 所属块ID
    uint64_t offset;        // 块内偏移
    uint64_t length;        // 长度
};

// ============================================================================
// 滑动窗口检测器（论文Figure 16）
// ============================================================================
class SlidingWindowDetector {
public:
    SlidingWindowDetector(uint32_t window_size);
    
    // 检测当前窗口是否为clean block
    bool is_clean_block(const std::vector<KVMetadata>& kv_pairs, 
                        size_t start_pos);
    
    // 获取下一个clean block的位置
    size_t find_next_clean_block(const std::vector<KVMetadata>& kv_pairs,
                                 size_t start_pos);
    
private:
    uint32_t window_size_;  // 窗口大小（块大小）
    
    bool check_block_integrity(const std::vector<KVMetadata>& kv_pairs,
                               size_t start_pos);
    bool check_boundary_alignment(const std::vector<KVMetadata>& kv_pairs,
                                  size_t start_pos);
};

// ============================================================================
// 压缩策略选择器
// ============================================================================
class CompactionStrategySelector {
public:
    // 根据脏块比例选择压缩粒度
    enum class Strategy {
        BLOCK_LEVEL,    // 块级压缩（<40%脏块）
        HYBRID,         // 混合模式（40-80%脏块，使用滑动窗口）
        FILE_LEVEL      // 文件级压缩（>80%脏块）
    };
    
    static Strategy select_strategy(float dirty_ratio);
    static const char* strategy_name(Strategy s);
};

} // namespace rbc

#endif // RBC_COMPACTION_H