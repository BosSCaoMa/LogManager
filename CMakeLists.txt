cmake_minimum_required(VERSION 3.10)

project(LogM LANGUAGES CXX)

# 基础 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 构建共享库（手动列出源文件，避免 GLOB 复杂性）
add_library(LogM SHARED
    src/LogM.cpp
)

# 公开头文件目录（仅构建阶段和使用阶段都用源码目录即可）
target_include_directories(LogM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 安装生成的库和头文件（极简，不使用 GNUInstallDirs）
install(TARGETS LogM
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES src/LogM.h DESTINATION include/LogM)