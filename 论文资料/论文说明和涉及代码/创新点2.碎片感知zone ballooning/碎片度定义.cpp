// fragmentation_ratio.h
#pragma once

class FragmentationMonitor {
public:
    static constexpr float FRAG_THRESHOLD = 0.3f;

    static float calculateFragmentationRatio(uint64_t remapped_blocks, uint64_t total_blocks) {
        if (total_blocks == 0) return 0.0f;
        return static_cast<float>(remapped_blocks) / static_cast<float>(total_blocks);
    }
};

// zone_allocator.h
#pragma once
#include <cstdint>

class ZoneAllocator {
public:
    virtual int originalAlloc(uint32_t namespace_id, uint32_t requested_width) = 0;
    virtual ~ZoneAllocator() = default;
};

class ZoneArbiter : public ZoneAllocator {
private:
    ZoneAllocator* original_allocator;

public:
    explicit ZoneArbiter(ZoneAllocator* allocator) : original_allocator(allocator) {}

    int originalAlloc(uint32_t namespace_id, uint32_t requested_width) override {
        return original_allocator->originalAlloc(namespace_id, requested_width);
    }

    int zoneArbiterAllocWithFrag(uint32_t namespace_id, 
                                  uint32_t requested_width,
                                  float current_frag) {
        if (current_frag > FragmentationMonitor::FRAG_THRESHOLD) {
            // 碎片度过高，减少借贷量（减少30%）
            requested_width = static_cast<uint32_t>(requested_width * 0.7f);
        }
        return originalAlloc(namespace_id, requested_width);
    }
};