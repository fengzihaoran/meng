#!/usr/bin/env python3
# ============================================================================
# 实验结果分析脚本
# 生成论文所需的图表和表格
# ============================================================================

import json
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from pathlib import Path

class ResultAnalyzer:
    def __init__(self, result_dir="./results"):
        self.result_dir = Path(result_dir)
        self.output_dir = self.result_dir / "analysis"
        self.output_dir.mkdir(exist_ok=True)
        
    def load_results(self):
        """加载所有实验结果"""
        results = {}
        
        # 加载元数据结果
        meta_file = self.result_dir / "metadata_results.json"
        if meta_file.exists():
            with open(meta_file) as f:
                results['metadata'] = json.load(f)
        
        # 加载碎片感知结果
        frag_file = self.result_dir / "frag_aware_results.json"
        if frag_file.exists():
            with open(frag_file) as f:
                results['frag_aware'] = json.load(f)
        
        # 加载多租户结果
        multi_file = self.result_dir / "multi_tenant_results.json"
        if multi_file.exists():
            with open(multi_file) as f:
                results['multi_tenant'] = json.load(f)
        
        return results
    
    def generate_performance_table(self, results):
        """生成性能对比表格（论文用）"""
        if 'frag_aware' not in results:
            return
        
        data = []
        workloads = ['A', 'B', 'C']
        
        for wl in workloads:
            row = {'Workload': f'YCSB-{wl}'}
            for cfg in ['ezns', 'rbc', 'ezns_rbc']:
                if wl in results['frag_aware'] and cfg in results['frag_aware'][wl]:
                    res = results['frag_aware'][wl][cfg]
                    row[f'{cfg}_frag'] = res.get('avg_frag', 0)
                    row[f'{cfg}_lat'] = res.get('avg_lat', 0)
                    row[f'{cfg}_p99'] = res.get('p99_lat', 0)
            data.append(row)
        
        df = pd.DataFrame(data)
        df.to_csv(self.output_dir / 'performance_table.csv', index=False)
        
        # 生成LaTeX表格
        with open(self.output_dir / 'performance_table.tex', 'w') as f:
            f.write("\\begin{table}[t]\n")
            f.write("\\centering\n")
            f.write("\\caption{Performance Comparison across Workloads}\n")
            f.write("\\label{tab:performance}\n")
            f.write("\\begin{tabular}{|l|c|c|c|c|c|c|}\n")
            f.write("\\hline\n")
            f.write("Workload & \\multicolumn{2}{c|}{eZNS} & \\multicolumn{2}{c|}{RBC} & \\multicolumn{2}{c|}{eZNS+RBC} \\\\\n")
            f.write(" & Frag & Lat(us) & Frag & Lat(us) & Frag & Lat(us) \\\\\n")
            f.write("\\hline\n")
            
            for _, row in df.iterrows():
                f.write(f"{row['Workload']} & ")
                f.write(f"{row['ezns_frag']:.3f} & {row['ezns_lat']:.1f} & ")
                f.write(f"{row['rbc_frag']:.3f} & {row['rbc_lat']:.1f} & ")
                f.write(f"{row['ezns_rbc_frag']:.3f} & {row['ezns_rbc_lat']:.1f} \\\\\n")
                f.write("\\hline\n")
            
            f.write("\\end{tabular}\n")
            f.write("\\end{table}\n")
        
        print(f"性能表格已生成: {self.output_dir}/performance_table.tex")
    
    def generate_fairness_plot(self, results):
        """生成公平性图表"""
        if 'multi_tenant' not in results:
            return
        
        data = results['multi_tenant']
        tenants = data.get('tenants', [])
        res = data.get('results', {})
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
        
        # 带宽分布
        ns_ids = [t['ns_id'] for t in tenants]
        bws = [res[str(ns)]['bandwidth'] for ns in ns_ids]
        colors = ['red' if t['type'] == 'write_heavy' else 'blue' for t in tenants]
        
        bars = ax1.bar(ns_ids, bws, color=colors)
        ax1.set_xlabel('Namespace ID')
        ax1.set_ylabel('Bandwidth (MB/s)')
        ax1.set_title('Bandwidth Distribution Across Tenants')
        ax1.grid(True, alpha=0.3)
        
        # 添加公平性标注
        fairness = data.get('fairness', 0)
        ax1.text(0.5, 0.95, f'Fairness Index: {fairness:.4f}',
                transform=ax1.transAxes, ha='center',
                bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
        
        # 碎片度 vs 读延迟
        frags = [res[str(ns)]['frag_ratio'] for ns in ns_ids]
        lats = [res[str(ns)]['read_latency'] for ns in ns_ids]
        
        scatter = ax2.scatter(frags, lats, c=range(len(ns_ids)), 
                             cmap='coolwarm', s=100)
        ax2.set_xlabel('Fragmentation Ratio')
        ax2.set_ylabel('Read Latency (us)')
        ax2.set_title('Fragmentation vs Read Latency')
        ax2.grid(True, alpha=0.3)
        
        # 标注租户
        for i, ns in enumerate(ns_ids):
            ax2.annotate(f'NS{ns}', (frags[i], lats[i]), 
                        xytext=(5, 5), textcoords='offset points')
        
        plt.colorbar(scatter, ax=ax2, label='Tenant Index')
        plt.tight_layout()
        plt.savefig(self.output_dir / 'fairness_analysis.png', dpi=150)
        plt.show()
        
        print(f"公平性图表已生成: {self.output_dir}/fairness_analysis.png")
    
    def generate_summary_stats(self, results):
        """生成汇总统计"""
        stats = {}
        
        if 'metadata' in results:
            meta = results['metadata']
            ezns_mem = next((c['memory_mb'] for c in meta['configs'] if 'eZNS' in c['name']), 0)
            rbc_mem = next((c['memory_mb'] for c in meta['configs'] if 'RBC' in c['name']), 0)
            unified_mem = next((c['memory_mb'] for c in meta['configs'] if 'Unified' in c['name']), 0)
            
            stats['memory_saved'] = (ezns_mem + rbc_mem - unified_mem) / (ezns_mem + rbc_mem) * 100
        
        if 'frag_aware' in results:
            frag = results['frag_aware']
            # 计算平均改善
            lat_improvements = []
            for wl in ['A', 'B', 'C']:
                if wl in frag:
                    ezns_lat = frag[wl]['ezns']['avg_lat']
                    unified_lat = frag[wl]['ezns_rbc']['avg_lat']
                    imp = (ezns_lat - unified_lat) / ezns_lat * 100
                    lat_improvements.append(imp)
            
            stats['avg_lat_improvement'] = np.mean(lat_improvements) if lat_improvements else 0
        
        # 保存汇总
        with open(self.output_dir / 'summary_stats.json', 'w') as f:
            json.dump(stats, f, indent=2)
        
        print("\n汇总统计:")
        for k, v in stats.items():
            print(f"  {k}: {v:.2f}")
    
    def run_all(self):
        """运行所有分析"""
        print("=" * 50)
        print("开始分析实验结果")
        print("=" * 50)
        
        results = self.load_results()
        
        if not results:
            print("错误: 未找到实验结果文件")
            return
        
        self.generate_performance_table(results)
        self.generate_fairness_plot(results)
        self.generate_summary_stats(results)
        
        print(f"\n分析完成！结果保存在: {self.output_dir}")

if __name__ == "__main__":
    analyzer = ResultAnalyzer()
    analyzer.run_all()