@echo off
setlocal
wsl.exe --cd "%~dp0." --exec sh -lc "if [ ! -f build/CMakeCache.txt ]; then cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug || exit; fi; cmake --build build -j4 && ./build/ark-ui-simulator"
if errorlevel 1 pause
