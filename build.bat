@echo off
echo ===================================================
echo  TDMA Radio Desktop Builder (MinGW GCC)
echo ===================================================

:: Configure build directory
if not exist build (
    echo [INFO] Configuring build directory using MinGW Makefiles...
    cmake -G "MinGW Makefiles" -S . -B build
) else (
    echo [INFO] Build directory already exists.
)

:: Compile target
echo [INFO] Compiling tdma_desktop...
cmake --build build --target tdma_desktop

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Build completed successfully.
echo Executive path: build\tdma_desktop.exe
