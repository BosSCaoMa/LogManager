cmake_minimum_required(VERSION 3.16)

project(LogM VERSION 0.1.0 LANGUAGES CXX)

# C++ 标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 标准“必须”满足
set(CMAKE_CXX_EXTENSIONS OFF) # 禁止编译器专有扩展，使用纯标准模式

# 默认构建类型（可用 -DCMAKE_BUILD_TYPE=Release 覆盖）
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT CMAKE_GENERATOR MATCHES "Xcode")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# 自动收集源文件（递归）。CONFIGURE_DEPENDS 让新增/删除源触发重新配置。
file(GLOB_RECURSE LOGM_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.c
)

# 添加 main.cpp（位于项目根目录）
add_executable(LogM ${PROJECT_SOURCE_DIR}/main.cpp ${LOGM_SOURCES})

# 头文件目录
# include_directories(${PROJECT_SOURCE_DIR}/src)  # 旧方式
# 使用目标私有包含目录
target_include_directories(LogM PRIVATE ${PROJECT_SOURCE_DIR}/src)

# 编译警告/严格性
if(MSVC)
    target_compile_options(LogM PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(LogM PRIVATE -Wall -Wextra -Wpedantic)
endif()