// 配置常量（基于论文经验值）
constexpr float FRAG_THRESHOLD = 0.3f;      // RBC论文的30%阈值
constexpr float BORROW_REDUCTION_FACTOR = 0.7f; // 减少30%
constexpr uint32_t MIN_STRIPE_WIDTH = 2;    // eZNS最小宽度
constexpr uint32_t MAX_STRIPE_WIDTH = 16;   // eZNS最大宽度