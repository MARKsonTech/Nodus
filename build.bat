@echo off

where cl >nul 2>nul
if %errorlevel% neq 0 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64
)

rc /fo nodus_engine.res resource.rc
if %errorlevel% neq 0 exit /b %errorlevel%

cl /EHsc /std:c++17 main.cpp nodus_engine.res /I "C:\SDL2\include" /I "C:\curl\include" /link /LIBPATH:"C:\SDL2\lib\x64" /LIBPATH:"C:\curl\bin" SDL2main.lib SDL2.lib SDL2_ttf.lib SDL2_image.lib libcurl.lib shell32.lib user32.lib /SUBSYSTEM:CONSOLE /OUT:nodus_engine.exe