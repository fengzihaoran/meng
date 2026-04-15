# 创建构建目录
mkdir build && cd build

# 配置
cmake ..

# 编译
make -j$(nproc)

# 运行测试
./test_metadata
./test_frag_aware
./test_tenant

# 运行主程序
./ezns_rbc_main