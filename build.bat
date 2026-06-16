@echo off
echo ===================================================
echo  TDMA Radio Desktop Builder
echo ===================================================

:: Ensure MSYS2 UCRT64 bin directory is in PATH if it exists and isn't already there
if not exist C:\msys64\ucrt64\bin goto :skip_path
echo "%PATH%" | findstr /I "C:\msys64\ucrt64\bin" >nul
if %ERRORLEVEL% EQU 0 goto :skip_path
echo [INFO] Adding C:\msys64\ucrt64\bin to PATH...
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
:skip_path

:: Find the best available build generator
set GENERATOR=
set GEN_NAME=

:: Check for Ninja
where ninja >nul 2>nul
if %ERRORLEVEL% NEQ 0 goto :check_mingw
set GENERATOR=Ninja
set GEN_NAME=Ninja
goto :found_generator

:check_mingw
:: Check for mingw32-make
where mingw32-make >nul 2>nul
if %ERRORLEVEL% NEQ 0 goto :no_tools
set GENERATOR="MinGW Makefiles"
set "GEN_NAME=MinGW Makefiles"
goto :found_generator

:no_tools
echo [ERROR] Neither Ninja nor mingw32-make was found in PATH.
echo [ERROR] Please install a build tool. For MSYS2 UCRT64:
echo         pacman -S mingw-w64-ucrt-x86_64-ninja
echo         or pacman -S mingw-w64-ucrt-x86_64-make
exit /b 1

:found_generator
echo [INFO] Found build tool: %GENERATOR%

:: Check if build directory exists and if generator matches
if not exist build goto :configure
if not exist build\CMakeCache.txt goto :clean_incomplete

findstr /C:"CMAKE_GENERATOR:INTERNAL=%GEN_NAME%" build\CMakeCache.txt >nul 2>&1
if %ERRORLEVEL% EQU 0 goto :configure

echo [WARNING] Cached generator does not match %GENERATOR%. Cleaning build directory...
rmdir /s /q build
goto :configure

:clean_incomplete
echo [WARNING] Incomplete build directory detected. Cleaning...
rmdir /s /q build

:configure
if exist build goto :build_target
echo [INFO] Configuring build directory using %GENERATOR%...
cmake -G %GENERATOR% -S . -B build
if %ERRORLEVEL% NEQ 0 goto :config_failed
goto :build_target

:config_failed
echo [ERROR] CMake configuration failed!
exit /b %ERRORLEVEL%

:build_target
:: Compile target
echo [INFO] Compiling tdma_desktop...
cmake --build build --target tdma_desktop

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Build completed successfully.
echo Executive path: build\tdma_desktop.exe

