@echo off
rem Compilation de whisper.cpp (whisper-cli.exe) avec le backend Vulkan (AMD) via VS 2022 BuildTools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set VULKAN_SDK=C:\VulkanSDK\1.4.363.0
set PATH=%VULKAN_SDK%\Bin;%PATH%

cd /d "%~dp0whisper.cpp"
if not exist build mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded || exit /b 1
cmake --build . --target whisper-cli -j %NUMBER_OF_PROCESSORS% || exit /b 1
