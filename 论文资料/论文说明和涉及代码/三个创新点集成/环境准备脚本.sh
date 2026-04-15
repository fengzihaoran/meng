#!/bin/bash
# ============================================================================
# eZNS+RBC实验环境准备脚本
# ============================================================================

set -e

echo "========================================="
echo "开始准备eZNS+RBC实验环境"
echo "========================================="

# 1. 创建目录
mkdir -p ~/ezns_rbc_experiments/{scripts,results,logs,configs}

# 2. 下载FEMU（如果不存在）
if [ ! -d ~/femu ]; then
    cd ~
    git clone https://github.com/ucare-uchicago/femu.git
    cd femu
    mkdir -p build-femu
    cd build-femu
    cp ../femu-scripts/femu-copy-scripts.sh .
    ./femu-copy-scripts.sh
    ./pkgdep.sh
    ../configure --enable-kvm --target-list=x86_64-softmmu --enable-debug
    make -j$(nproc)
fi

# 3. 下载RocksDB和ZenFS
cd ~
if [ ! -d rocksdb ]; then
    git clone https://github.com/facebook/rocksdb.git
    cd rocksdb
    git checkout v7.10.0
    git clone https://github.com/westerndigitalcorporation/zenfs.git plugin/zenfs
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DROCKSDB_PLUGINS=zenfs
    make -j$(nproc)
fi

# 4. 下载eZNS和RBC代码（假设已有）
cd ~/ezns_rbc_experiments
# 这里放置您的eZNS+RBC实现代码

echo "========================================="
echo "环境准备完成"
echo "========================================="