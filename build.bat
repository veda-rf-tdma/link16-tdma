@echo off
echo ===================================================
echo  TDMA Radio Desktop Builder
echo ===================================================

:: Ensure MSYS2 UCRT64 bin directory is in PATH if it exists and isn't already there
if exist C:\msys64\ucrt64\bin (
    echo %PATH% | findstr /I "C:\msys64\ucrt64\bin" >nul
    if errorlevel 1 (
        echo [INFO] Adding C:\msys64\ucrt64\bin to PATH...
        set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    )
)

:: Find the best available build generator
set GENERATOR=
set GEN_NAME=

:: Check for Ninja
where ninja >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set GENERATOR=Ninja
    set GEN_NAME=Ninja
    goto :found_generator
)

:: Check for mingw32-make
where mingw32-make >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set GENERATOR="MinGW Makefiles"
    set "GEN_NAME=MinGW Makefiles"
    goto :found_generator
)

echo [ERROR] Neither Ninja nor mingw32-make was found in PATH.
echo [ERROR] Please install a build tool. For MSYS2 UCRT64:
echo         pacman -S mingw-w64-ucrt-x86_64-ninja
echo         or pacman -S mingw-w64-ucrt-x86_64-make
exit /b 1

:found_generator
echo [INFO] Found build tool: %GENERATOR%

:: Check if build directory exists and if generator matches
if exist build (
    if exist build\CMakeCache.txt (
        findstr /C:"CMAKE_GENERATOR:INTERNAL=%GEN_NAME%" build\CMakeCache.txt >nul 2>&1
        if errorlevel 1 (
            echo [WARNING] Cached generator does not match %GENERATOR%. Cleaning build directory...
            rmdir /s /q build
        )
    ) else (
        :: If build exists but CMakeCache.txt doesn't, it might be a failed config. Clean it.
        echo [WARNING] Incomplete build directory detected. Cleaning...
        rmdir /s /q build
    )
)

:: Configure build directory
if not exist build (
    echo [INFO] Configuring build directory using %GENERATOR%...
    cmake -G %GENERATOR% -S . -B build
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] CMake configuration failed!
        exit /b %ERRORLEVEL%
    )
) else (
    echo [INFO] Build directory already configured.
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

