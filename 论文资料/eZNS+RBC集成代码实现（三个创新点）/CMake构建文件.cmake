cmake_minimum_required(VERSION 3.10)
project(ezns_rbc_integrated VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O2 -DNDEBUG)
endif()

# 包含目录
include_directories(include)

# 源文件
set(SOURCES
    src/unified_metadata.cpp
    src/frag_aware_balloon.cpp
    src/tenant_isolation.cpp
    src/unified_controller.cpp
    # eZNS原有组件（如果需要）
    # src/zone_arbiter.cpp
    # src/io_scheduler.cpp
    # RBC原有组件
    # src/rbc_remap.cpp
    # src/rbc_reorg.cpp
    # src/rbc_compaction.cpp
)

# 测试程序
add_executable(test_integrated tests/test_integrated.cpp ${SOURCES})

# 链接库
target_link_libraries(test_integrated pthread)

# 安装
install(TARGETS test_integrated DESTINATION bin)