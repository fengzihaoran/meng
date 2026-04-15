// 碎片度计算公式
fragmentation_ratio = remapped_blocks / total_blocks;

// 阈值触发（参考RBC的30%阈值）
#define FRAG_THRESHOLD 0.3

// 修改后的zone分配逻辑
int zone_arbiter_alloc_with_frag(uint32_t namespace_id, 
                                  uint32_t requested_width,
                                  float current_frag) {
    if (current_frag > FRAG_THRESHOLD) {
        // 碎片度过高，减少借贷量
        requested_width = requested_width * 0.7;  // 减少30%
    }
    return original_alloc(namespace_id, requested_width);
}