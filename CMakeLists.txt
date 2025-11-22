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

# 先引入安装目录变量
include(GNUInstallDirs)

# 头文件目录（区分构建与安装阶段，避免导出绝对源码路径）
target_include_directories(LogM
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/LogM>
)

# 编译警告/严格性
if(MSVC)
    target_compile_options(LogM PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(LogM PRIVATE LOGM_EXPORTS)
else()
    target_compile_options(LogM PRIVATE -Wall -Wextra -Wpedantic -fvisibility=hidden)
endif()

# 可选：设置版本属性（Linux/Unix 有效）
set_target_properties(LogM PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 安装规则
install(TARGETS LogM
    EXPORT LogMTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LogM
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LogM FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# 导出配置（find_package 支持）
install(EXPORT LogMTargets FILE LogMTargets.cmake NAMESPACE LogM:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LogM)