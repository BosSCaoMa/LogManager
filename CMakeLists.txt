cmake_minimum_required(VERSION 3.16)

project(LogM VERSION 0.1.0 LANGUAGES CXX)

# =========================
# 基础配置
# =========================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LOGM_SRC_DIR ${PROJECT_SOURCE_DIR}/src)

# 默认构建类型（可用 -DCMAKE_BUILD_TYPE=Release 覆盖）
if(
    NOT CMAKE_BUILD_TYPE
    AND NOT CMAKE_GENERATOR MATCHES "Visual Studio"
    AND NOT CMAKE_GENERATOR MATCHES "Xcode"
)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# =========================
# 源码
# =========================
file(GLOB_RECURSE LOGM_SOURCES CONFIGURE_DEPENDS
    ${LOGM_SRC_DIR}/*.cpp
    ${LOGM_SRC_DIR}/*.c
)

# =========================
# 目标：动态库
# =========================
add_library(LogM SHARED ${LOGM_SOURCES})
target_include_directories(LogM PUBLIC ${LOGM_SRC_DIR})
target_compile_definitions(LogM PRIVATE LOGM_PROJECT_DIR="${PROJECT_SOURCE_DIR}")

# =========================
# 目标：示例程序
# =========================
add_executable(logm_demo ${PROJECT_SOURCE_DIR}/main.cpp)
target_link_libraries(logm_demo PRIVATE LogM)
target_compile_definitions(logm_demo PRIVATE LOGM_PROJECT_DIR="${PROJECT_SOURCE_DIR}")

# =========================
# 目标：压测程序（可选）
# =========================
if(EXISTS ${PROJECT_SOURCE_DIR}/perf_bench.cpp)
    add_executable(logm_perf_bench ${PROJECT_SOURCE_DIR}/perf_bench.cpp)
    target_link_libraries(logm_perf_bench PRIVATE LogM)
    target_compile_definitions(logm_perf_bench PRIVATE LOGM_PROJECT_DIR="${PROJECT_SOURCE_DIR}")
endif()

# =========================
# 编译选项
# =========================
if(MSVC)
    target_compile_options(LogM PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(LogM PRIVATE LOGM_EXPORTS)
else()
    target_compile_options(LogM PRIVATE -Wall -Wextra -Wpedantic)
endif()

# =========================
# 目标属性
# =========================
set_target_properties(LogM PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 可执行文件在构建目录下直接加载同目录共享库
set(LOGM_EXECUTABLES_WITH_RPATH logm_demo)

if(TARGET logm_perf_bench)
    list(APPEND LOGM_EXECUTABLES_WITH_RPATH logm_perf_bench)
endif()

foreach(target_name IN LISTS LOGM_EXECUTABLES_WITH_RPATH)
    set_target_properties(${target_name} PROPERTIES
        BUILD_RPATH "$ORIGIN"
    )
endforeach()