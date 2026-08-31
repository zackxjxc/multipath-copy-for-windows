@echo off
setlocal

rem Generated with AI assistance. Build the x64 Release executable with CMake.
for %%I in ("%~dp0.") do set "PROJECT_DIR=%%~fI"
set "BUILD_DIR=%PROJECT_DIR%\.build"
set "OUTPUT_DIR=%PROJECT_DIR%\output"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -A x64
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

cmake --install "%BUILD_DIR%" --config Release --prefix "%OUTPUT_DIR%"
if errorlevel 1 exit /b 1

echo Built: %OUTPUT_DIR%\MultiPathCopy.exe
endlocal
