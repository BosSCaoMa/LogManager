@echo off
setlocal

set BUILD_DIR=build
set CONFIG=Release

if not exist "%BUILD_DIR%" (
    echo [INFO] Creating build directory "%BUILD_DIR%"...
)

echo [INFO] Configuring project with CMake...
cmake -S . -B "%BUILD_DIR%"
if errorlevel 1 (
    echo [ERROR] CMake configure step failed.
    exit /b 1
)

echo [INFO] Building target LogM (%CONFIG%)...
cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [INFO] Build finished. The dynamic library should be in "%BUILD_DIR%\%CONFIG%" (MSVC) or "%BUILD_DIR%" (MinGW/Make).
exit /b 0
