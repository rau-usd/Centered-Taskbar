@echo off
setlocal
cd /d "%~dp0"
REM Requires MinGW-w64 (e.g. w64devkit: https://github.com/skeeto/w64devkit) in PATH.
if not exist build mkdir build
if not exist dist mkdir dist

windres res/Centered-Taskbar.rc -O coff -o build/Centered-Taskbar.res
if errorlevel 1 goto fail

g++ -Os -s -municode -mwindows -static -ffunction-sections -fdata-sections -Wl,--gc-sections ^
  -o dist/Centered-Taskbar.exe src/Centered-Taskbar.cpp build/Centered-Taskbar.res ^
  -lole32 -loleaut32 -luuid -lshell32 -lgdi32 -luser32 -ladvapi32
if errorlevel 1 goto fail

echo.
echo Built: dist\Centered-Taskbar.exe
goto end

:fail
echo.
echo BUILD FAILED

:end
pause
