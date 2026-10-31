@echo off
rem Compilation du Studio Audio AI natif (C++ / Win32 / DirectX 11 / Dear ImGui) via VS 2022 BuildTools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

cd /d "%~dp0native"
if not exist build mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build . -j %NUMBER_OF_PROCESSORS% || exit /b 1
