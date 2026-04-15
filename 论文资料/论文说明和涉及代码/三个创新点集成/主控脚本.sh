#!/bin/bash
# ============================================================================
# eZNS+RBC实验主控脚本
# 运行所有三个创新点的实验
# ============================================================================

set -e

RESULT_DIR="./results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_DIR="./logs/${TIMESTAMP}"

mkdir -p $LOG_DIR
mkdir -p $RESULT_DIR

echo "========================================="
echo "eZNS+RBC实验开始 - $TIMESTAMP"
echo "========================================="

# 1. 统一元数据测试
echo ""
echo "[1/3] 运行统一元数据测试..."
python3 test_metadata.py | tee $LOG_DIR/metadata.log

# 2. 碎片感知的zone ballooning测试
echo ""
echo "[2/3] 运行碎片感知测试..."
python3 test_frag_aware.py | tee $LOG_DIR/frag_aware.log

# 3. 多租户碎片隔离测试
echo ""
echo "[3/3] 运行多租户测试..."
python3 test_multi_tenant.py | tee $LOG_DIR/multi_tenant.log

# 4. 生成汇总报告
echo ""
echo "生成汇总报告..."

cat > $LOG_DIR/summary.txt << EOF
eZNS+RBC实验汇总报告
====================
时间: $(date)

1. 统一元数据管理
   - eZNS内存: $(grep "eZNS-only" $LOG_DIR/metadata.log | grep "内存" | awk '{print $3}')
   - RBC内存: $(grep "RBC-only" $LOG_DIR/metadata.log | grep "内存" | awk '{print $3}')
   - 统一后内存: $(grep "Unified" $LOG_DIR/metadata.log | grep "内存" | awk '{print $3}')
   - 节省: $(grep "内存节省" $LOG_DIR/metadata.log | awk '{print $3}')

2. 碎片感知的zone ballooning
   - eZNS平均碎片度: $(grep "ezns" -A5 $LOG_DIR/frag_aware.log | grep "平均碎片度" | head -1 | awk '{print $3}')
   - eZNS+RBC平均碎片度: $(grep "ezns_rbc" -A5 $LOG_DIR/frag_aware.log | grep "平均碎片度" | head -1 | awk '{print $3}')
   - 读延迟改善: $(grep "ezns" -A5 $LOG_DIR/frag_aware.log | grep "平均读延迟" | head -1 | awk '{print $3}')

3. 多租户碎片隔离
   - 公平性指数: $(grep "公平性指数" $LOG_DIR/multi_tenant.log | awk '{print $3}')
   - 写重租户平均碎片度: $(grep "write_heavy" -B1 $LOG_DIR/multi_tenant.log | grep "碎片度" | awk '{sum+=$3; count++} END {print sum/count}')
   - 读重租户平均读延迟: $(grep "read_heavy" -B1 $LOG_DIR/multi_tenant.log | grep "读延迟" | awk '{sum+=$4; count++} END {print sum/count}')
EOF

cat $LOG_DIR/summary.txt

echo ""
echo "========================================="
echo "实验完成！结果保存在:"
echo "  - 日志目录: $LOG_DIR"
echo "  - 结果图表: $RESULT_DIR/"
echo "========================================="