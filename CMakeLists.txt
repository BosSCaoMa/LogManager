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

# 生成动态库 (共享库)
add_library(LogM SHARED ${LOGM_SOURCES})

# 仅构建期头文件搜索路径
 target_include_directories(LogM PUBLIC ${PROJECT_SOURCE_DIR}/src)

# 让 MSVC 编译时导出符号（生成 .lib 所需）
if(MSVC)
    target_compile_options(LogM PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(LogM PRIVATE LOGM_EXPORTS)
else()
    target_compile_options(LogM PRIVATE -Wall -Wextra -Wpedantic)
    # 如需隐藏未标记符号再加: -fvisibility=hidden 并在头文件导出接口
endif()

# 如不需要版本信息可删除下面两行
set_target_properties(LogM PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)