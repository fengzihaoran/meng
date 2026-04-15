cmake_minimum_required(VERSION 3.10)
project(ezns_rbc VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O3 -DNDEBUG)
endif()

# 包含目录
include_directories(include)

# 源文件
set(SOURCES
    src/unified_metadata.cc
    src/frag_aware_balloon.cc
    src/tenant_isolation.cc
    src/zone_arbiter.cc
)

# 主程序
add_executable(ezns_rbc_main src/main.cc ${SOURCES})

# 测试程序
add_executable(test_metadata tests/test_metadata.cc ${SOURCES})
add_executable(test_frag_aware tests/test_frag_aware.cc ${SOURCES})
add_executable(test_tenant tests/test_tenant.cc ${SOURCES})

# 链接库
target_link_libraries(ezns_rbc_main pthread)
target_link_libraries(test_metadata pthread)
target_link_libraries(test_frag_aware pthread)
target_link_libraries(test_tenant pthread)

# 安装（可选）
install(TARGETS ezns_rbc_main DESTINATION bin)