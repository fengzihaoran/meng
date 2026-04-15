uint32_t FragAwareBalloon::adjust_stripe_size(uint32_t width) const {
    // 条带大小与宽度成反比（eZNS论文Section 4.4.2）
    const uint32_t BASE_STRIPE_SIZE = 32 * 1024;  // 32KB基线
    const uint32_t MIN_STRIPE_SIZE = 4 * 1024;    // 4KB最小
    
    // 宽度增加时，条带减小，以保持I/O粒度合适
    uint32_t size = BASE_STRIPE_SIZE / (width / 2);
    return std::max(size, MIN_STRIPE_SIZE);
}