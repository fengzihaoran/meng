// 租户碎片隔离策略
typedef struct {
    uint32_t namespace_id;
    float frag_threshold;        // 租户可接受的碎片阈值
    uint32_t max_borrow_spares;  // 最大可借spare数
    float current_frag;          // 当前碎片度
} tenant_frag_config_t;

// 借贷检查函数
int can_borrow_from_tenant(uint32_t lender_id, uint32_t borrower_id) {
    tenant_frag_config_t* lender = get_tenant_config(lender_id);
    
    // 如果借贷会推高碎片度超过阈值，则拒绝
    float new_frag = calculate_new_frag(lender, borrower);
    if (new_frag > lender->frag_threshold) {
        return 0;  // 拒绝借贷
    }
    return 1;
}