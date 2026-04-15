cmake_minimum_required(VERSION 3.10)
project(rbc_demo VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O2 -Wall -Wextra)

include_directories(include)

set(SOURCES
    src/rbc_remap.cpp
    src/rbc_reorg.cpp
    src/rbc_compaction.cpp
    src/rbc_controller.cpp
)

add_executable(rbc_demo src/main.cpp ${SOURCES})
target_link_libraries(rbc_demo pthread)