// 初始化统一元数据
int init_unified_metadata(const char* device_path);

// 碎片感知的zone分配
int alloc_zone_frag_aware(uint32_t ns_id, uint32_t size, float* out_frag);

// 触发重组
int trigger_reorganization(uint32_t ns_id);

// 检查租户隔离
int check_tenant_isolation(uint32_t lender, uint32_t borrower);