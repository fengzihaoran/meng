// 租户碎片隔离策略
typedef struct {
    uint32_t ns_id;
    float max_frag;          // 最大允许碎片度
    uint32_t max_borrow;      // 最大可借数
    uint32_t current_borrow;
    float current_frag;
} tenant_isolation_t;

int can_borrow_from(uint32_t lender, uint32_t borrower) {
    tenant_isolation_t* l = get_tenant(lender);
    tenant_isolation_t* b = get_tenant(borrower);
    
    // 计算借贷后的碎片度
    float new_frag = calculate_frag_after_borrow(l, b);
    
    // 如果会超过阈值，拒绝
    if (new_frag > l->max_frag) {
        return 0;  // 拒绝借贷
    }
    
    return 1;  // 允许借贷
}