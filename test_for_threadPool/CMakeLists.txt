cmake_minimum_required(VERSION 3.10)
project(chat_pressure_test)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/server)
include_directories(${CMAKE_SOURCE_DIR}/include/server/db)
include_directories(${CMAKE_SOURCE_DIR}/include/server/model)
include_directories(${CMAKE_SOURCE_DIR}/include/server/redis)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty)

# 添加可执行文件
add_executable(pressure_test pressure_test.cpp)

# 链接需要的库
target_link_libraries(pressure_test pthread)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin) 