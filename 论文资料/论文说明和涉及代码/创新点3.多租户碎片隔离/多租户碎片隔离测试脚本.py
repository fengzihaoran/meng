#!/usr/bin/env python3
# ============================================================================
# 测试多租户碎片隔离
# ============================================================================

import subprocess
import time
import matplotlib.pyplot as plt
import numpy as np
import json
import os
import threading
from datetime import datetime

class MultiTenantTest:
    def __init__(self, result_dir="./results"):
        self.result_dir = result_dir
        os.makedirs(result_dir, exist_ok=True)
        
    def run_tenant(self, ns_id, workload, duration, results_dict):
        """运行单个租户的负载"""
        cmd = f"./bin/db_bench --fs_uri=zenfs://dev:nvme0n1 " \
              f"--benchmarks={workload},stats " \
              f"--duration={duration} --namespace={ns_id} " \
              f"--config=ezns_rbc"
        
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        
        # 收集结果
        frags = []
        lats = []
        bws = []
        
        while True:
            line = proc.stdout.readline()
            if not line and proc.poll() is not None:
                break
            if "frag_ratio" in line:
                frags.append(float(line.split(':')[1]))
            if "read_latency" in line:
                lats.append(float(line.split(':')[1]))
            if "bandwidth" in line:
                bws.append(float(line.split(':')[1]))
        
        results_dict[ns_id] = {
            "frag_ratio": np.mean(frags) if frags else 0,
            "read_latency": np.mean(lats) if lats else 0,
            "bandwidth": np.mean(bws) if bws else 0,
            "frag_history": frags,
            "lat_history": lats,
            "bw_history": bws
        }
    
    def calculate_jain_index(self, values):
        """计算Jain's fairness index"""
        if not values:
            return 0
        sum_vals = sum(values)
        sum_squares = sum([v**2 for v in values])
        n = len(values)
        return (sum_vals**2) / (n * sum_squares) if sum_squares > 0 else 0
    
    def run_multi_tenant_test(self):
        """运行多租户测试"""
        print("=" * 50)
        print("测试3: 多租户碎片隔离")
        print("=" * 50)
        
        # 配置4个租户
        tenants = [
            {"ns_id": 1, "workload": "fillrandom", "type": "write_heavy"},
            {"ns_id": 2, "workload": "fillrandom", "type": "write_heavy"},
            {"ns_id": 3, "workload": "readrandom", "type": "read_heavy"},
            {"ns_id": 4, "workload": "readrandom", "type": "read_heavy"}
        ]
        
        duration = 3600  # 1小时
        
        # 并行运行租户
        threads = []
        results = {}
        
        start_time = time.time()
        
        for t in tenants:
            thread = threading.Thread(
                target=self.run_tenant,
                args=(t["ns_id"], t["workload"], duration, results)
            )
            threads.append(thread)
            thread.start()
        
        # 等待所有租户完成
        for thread in threads:
            thread.join()
        
        elapsed = time.time() - start_time
        
        # 计算公平性
        bandwidths = [results[t["ns_id"]]["bandwidth"] for t in tenants]
        fairness = self.calculate_jain_index(bandwidths)
        
        # 输出结果
        print(f"\n测试完成，耗时: {elapsed:.2f}秒")
        print(f"公平性指数: {fairness:.4f}")
        print("\n各租户结果:")
        print("-" * 60)
        print(f"{'NS':<5} {'类型':<12} {'碎片度':<10} {'读延迟(us)':<12} {'带宽(MB/s)':<10}")
        print("-" * 60)
        
        for t in tenants:
            res = results[t["ns_id"]]
            print(f"{t['ns_id']:<5} {t['type']:<12} "
                  f"{res['frag_ratio']:<10.3f} {res['read_latency']:<12.2f} "
                  f"{res['bandwidth']:<10.2f}")
        
        # 保存结果
        output = {
            "timestamp": datetime.now().isoformat(),
            "duration": elapsed,
            "fairness": fairness,
            "tenants": tenants,
            "results": results
        }
        
        with open(f"{self.result_dir}/multi_tenant_results.json", 'w') as f:
            json.dump(output, f, indent=2)
        
        # 生成图表
        self.plot_multi_tenant_results(results, tenants, fairness)
        
        return results
    
    def plot_multi_tenant_results(self, results, tenants, fairness):
        """生成多租户结果图表"""
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        
        ns_ids = [t["ns_id"] for t in tenants]
        types = [t["type"] for t in tenants]
        
        # 碎片度对比
        frags = [results[ns]["frag_ratio"] for ns in ns_ids]
        colors = ['red' if 'write' in types[i] else 'blue' for i in range(len(ns_ids))]
        bars = axes[0, 0].bar(ns_ids, frags, color=colors)
        axes[0, 0].set_title('Fragmentation Ratio by Tenant')
        axes[0, 0].set_xlabel('Namespace ID')
        axes[0, 0].set_ylabel('Fragmentation Ratio')
        axes[0, 0].grid(True, alpha=0.3)
        
        # 读延迟对比
        lats = [results[ns]["read_latency"] for ns in ns_ids]
        bars2 = axes[0, 1].bar(ns_ids, lats, color=colors)
        axes[0, 1].set_title('Read Latency by Tenant')
        axes[0, 1].set_xlabel('Namespace ID')
        axes[0, 1].set_ylabel('Latency (us)')
        axes[0, 1].grid(True, alpha=0.3)
        
        # 带宽对比
        bws = [results[ns]["bandwidth"] for ns in ns_ids]
        bars3 = axes[1, 0].bar(ns_ids, bws, color=colors)
        axes[1, 0].set_title('Bandwidth by Tenant')
        axes[1, 0].set_xlabel('Namespace ID')
        axes[1, 0].set_ylabel('Bandwidth (MB/s)')
        axes[1, 0].grid(True, alpha=0.3)
        
        # 碎片度历史
        for ns in ns_ids:
            hist = results[ns]["frag_history"]
            if hist:
                axes[1, 1].plot(hist, label=f'NS{ns}')
        axes[1, 1].set_title('Fragmentation History')
        axes[1, 1].set_xlabel('Time')
        axes[1, 1].set_ylabel('Fragmentation Ratio')
        axes[1, 1].legend()
        axes[1, 1].grid(True, alpha=0.3)
        
        # 添加公平性信息
        fig.suptitle(f'Multi-Tenant Test Results (Fairness Index: {fairness:.4f})',
                    fontsize=14, fontweight='bold')
        
        plt.tight_layout()
        plt.savefig(f"{self.result_dir}/multi_tenant_results.png", dpi=150)
        plt.show()

if __name__ == "__main__":
    test = MultiTenantTest()
    test.run_multi_tenant_test()