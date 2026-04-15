#!/usr/bin/env python3
# ============================================================================
# 测试碎片感知的zone ballooning
# ============================================================================

import subprocess
import time
import matplotlib.pyplot as plt
import numpy as np
import json
import os
import pandas as pd
from datetime import datetime

class FragAwareTest:
    def __init__(self, result_dir="./results"):
        self.result_dir = result_dir
        os.makedirs(result_dir, exist_ok=True)
        
    def run_ycsb_workload(self, workload, config, duration=3600):
        """运行YCSB负载"""
        cmd = f"./bin/db_bench --fs_uri=zenfs://dev:nvme0n1 " \
              f"--benchmarks=ycsb,stats --ycsb_workload={workload} " \
              f"--duration={duration} --config={config}"
        
        print(f"运行: {cmd}")
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        
        # 实时监控输出
        frag_data = []
        lat_data = []
        
        while True:
            line = proc.stdout.readline()
            if not line and proc.poll() is not None:
                break
            if "frag_ratio" in line:
                frag = float(line.split(':')[1].strip())
                frag_data.append(frag)
            if "read_latency" in line:
                lat = float(line.split(':')[1].strip())
                lat_data.append(lat)
        
        return {
            "frag_history": frag_data,
            "lat_history": lat_data,
            "avg_frag": np.mean(frag_data) if frag_data else 0,
            "avg_lat": np.mean(lat_data) if lat_data else 0,
            "p99_lat": np.percentile(lat_data, 99) if lat_data else 0
        }
    
    def run_frag_aware_test(self):
        """运行碎片感知测试"""
        print("=" * 50)
        print("测试2: 碎片感知的zone ballooning")
        print("=" * 50)
        
        workloads = ["A", "B", "C"]  # YCSB workloads
        configs = ["ezns", "rbc", "ezns_rbc"]
        
        results = {}
        
        for wl in workloads:
            results[wl] = {}
            for cfg in configs:
                print(f"\n测试: YCSB-{wl}, 配置={cfg}")
                res = self.run_ycsb_workload(wl, cfg, duration=1800)  # 30分钟
                results[wl][cfg] = res
                
                print(f"  平均碎片度: {res['avg_frag']:.3f}")
                print(f"  平均读延迟: {res['avg_lat']:.2f} us")
                print(f"  p99读延迟: {res['p99_lat']:.2f} us")
        
        # 保存结果
        with open(f"{self.result_dir}/frag_aware_results.json", 'w') as f:
            json.dump(results, f, indent=2)
        
        # 生成图表
        self.plot_frag_results(results)
        
        return results
    
    def plot_frag_results(self, results):
        """生成碎片度和延迟对比图"""
        workloads = list(results.keys())
        configs = ["ezns", "rbc", "ezns_rbc"]
        
        fig, axes = plt.subplots(2, len(workloads), figsize=(15, 8))
        
        for i, wl in enumerate(workloads):
            # 碎片度对比
            frags = [results[wl][cfg]['avg_frag'] for cfg in configs]
            bars = axes[0, i].bar(configs, frags, color=['blue', 'green', 'red'])
            axes[0, i].set_title(f'YCSB-{wl} - Fragmentation')
            axes[0, i].set_ylabel('Fragmentation Ratio')
            axes[0, i].grid(True, alpha=0.3)
            
            # 读延迟对比
            lats = [results[wl][cfg]['avg_lat'] for cfg in configs]
            bars2 = axes[1, i].bar(configs, lats, color=['blue', 'green', 'red'])
            axes[1, i].set_title(f'YCSB-{wl} - Read Latency')
            axes[1, i].set_ylabel('Latency (us)')
            axes[1, i].grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(f"{self.result_dir}/frag_aware_comparison.png", dpi=150)
        plt.show()

if __name__ == "__main__":
    test = FragAwareTest()
    test.run_frag_aware_test()