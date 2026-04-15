#!/usr/bin/env python3
# ============================================================================
# 测试统一元数据管理的性能和内存占用
# ============================================================================

import subprocess
import time
import psutil
import matplotlib.pyplot as plt
import numpy as np
import json
import os
from datetime import datetime

class MetadataTest:
    def __init__(self, result_dir="./results"):
        self.result_dir = result_dir
        os.makedirs(result_dir, exist_ok=True)
        
    def measure_memory(self, process_name):
        """测量进程内存占用"""
        for proc in psutil.process_iter(['pid', 'name', 'memory_info']):
            if process_name in proc.info['name']:
                return proc.info['memory_info'].rss / (1024 * 1024)  # MB
        return 0
    
    def measure_lookup_latency(self, config, iterations=1000000):
        """测量元数据查询延迟"""
        cmd = f"./bin/metadata_benchmark --config={config} --iterations={iterations}"
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        # 解析输出，提取平均延迟
        for line in result.stdout.split('\n'):
            if "avg_latency_us" in line:
                return float(line.split(':')[1])
        return 0
    
    def run_metadata_test(self):
        """运行元数据测试"""
        print("=" * 50)
        print("测试1: 统一元数据管理")
        print("=" * 50)
        
        results = {
            "timestamp": datetime.now().isoformat(),
            "configs": []
        }
        
        # 测试三种配置
        configs = [
            {"name": "eZNS-only", "cmd": "./bin/ezns_only"},
            {"name": "RBC-only", "cmd": "./bin/rbc_only"},
            {"name": "Unified", "cmd": "./bin/unified_metadata"}
        ]
        
        for cfg in configs:
            print(f"\n测试配置: {cfg['name']}")
            
            # 启动进程
            proc = subprocess.Popen(cfg['cmd'].split(), stdout=subprocess.PIPE)
            time.sleep(2)  # 等待启动
            
            # 测量内存
            mem = self.measure_memory(cfg['name'].split('-')[0])
            
            # 测量查询延迟
            lat = self.measure_lookup_latency(cfg['name'])
            
            # 终止进程
            proc.terminate()
            
            results["configs"].append({
                "name": cfg['name'],
                "memory_mb": mem,
                "lookup_latency_us": lat
            })
            
            print(f"  内存: {mem:.2f} MB")
            print(f"  查询延迟: {lat:.2f} us")
        
        # 计算节省
        ezns_mem = results["configs"][0]["memory_mb"]
        rbc_mem = results["configs"][1]["memory_mb"]
        unified_mem = results["configs"][2]["memory_mb"]
        
        savings = (ezns_mem + rbc_mem - unified_mem) / (ezns_mem + rbc_mem) * 100
        print(f"\n内存节省: {savings:.1f}%")
        
        # 保存结果
        with open(f"{self.result_dir}/metadata_results.json", 'w') as f:
            json.dump(results, f, indent=2)
        
        # 生成图表
        self.plot_metadata_results(results)
        
        return results
    
    def plot_metadata_results(self, results):
        """生成内存和延迟对比图"""
        names = [cfg['name'] for cfg in results['configs']]
        memory = [cfg['memory_mb'] for cfg in results['configs']]
        latency = [cfg['lookup_latency_us'] for cfg in results['configs']]
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
        
        # 内存对比
        bars1 = ax1.bar(names, memory, color=['blue', 'green', 'red'])
        ax1.set_ylabel('Memory (MB)')
        ax1.set_title('Metadata Memory Footprint')
        ax1.grid(True, alpha=0.3)
        
        # 添加数值标签
        for bar, val in zip(bars1, memory):
            ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                    f'{val:.1f}MB', ha='center', va='bottom')
        
        # 延迟对比
        bars2 = ax2.bar(names, latency, color=['blue', 'green', 'red'])
        ax2.set_ylabel('Lookup Latency (us)')
        ax2.set_title('Metadata Lookup Latency')
        ax2.grid(True, alpha=0.3)
        
        for bar, val in zip(bars2, latency):
            ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.05,
                    f'{val:.2f}us', ha='center', va='bottom')
        
        plt.tight_layout()
        plt.savefig(f"{self.result_dir}/metadata_comparison.png", dpi=150)
        plt.show()

if __name__ == "__main__":
    test = MetadataTest()
    test.run_metadata_test()