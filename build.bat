@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if %errorlevel% neq 0 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64
)
where cl >nul 2>nul
if errorlevel 1 (
    echo ERROR: Visual Studio C++ Build Tools were not found.
    exit /b 1
)

set "VCPKG_ROOT=%LOCALAPPDATA%\vcpkg-nodus"
set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
set "VCPKG_INSTALLED=%VCPKG_ROOT%\installed\x64-windows"
set "VCPKG_DOWNLOADS=%~dp0tools\vcpkg\downloads"

if not exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" (
    where git >nul 2>nul
    if errorlevel 1 (
        echo ERROR: Git is required to download vcpkg and FFmpeg.
        exit /b 1
    )
    if not exist "%~dp0tools" mkdir "%~dp0tools"
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
    if errorlevel 1 exit /b %errorlevel%
)

if not exist "%VCPKG_EXE%" (
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 exit /b %errorlevel%
)

echo Installing FFmpeg and its dependencies with vcpkg...
"%VCPKG_EXE%" install ffmpeg:x64-windows --disable-metrics
if errorlevel 1 exit /b %errorlevel%

rc /fo nodus_engine.res resource.rc
if %errorlevel% neq 0 exit /b %errorlevel%

cl /EHsc /std:c++17 main.cpp nodus_engine.res /I "C:\SDL2\include" /I "C:\curl\include" /I "%VCPKG_INSTALLED%\include" /link /LIBPATH:"C:\SDL2\lib\x64" /LIBPATH:"C:\curl\bin" /LIBPATH:"%VCPKG_INSTALLED%\lib" SDL2main.lib SDL2.lib SDL2_ttf.lib SDL2_image.lib libcurl.lib avformat.lib avcodec.lib avutil.lib swscale.lib swresample.lib shell32.lib user32.lib /SUBSYSTEM:CONSOLE /OUT:nodus_engine.exe
if errorlevel 1 exit /b %errorlevel%

if exist "%VCPKG_INSTALLED%\bin\avcodec-*.dll" copy /Y "%VCPKG_INSTALLED%\bin\*.dll" "%~dp0" >nul

pause