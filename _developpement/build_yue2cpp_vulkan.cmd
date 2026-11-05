@echo off
rem Compilation de yue2.cpp avec le backend Vulkan (AMD) via VS 2022 BuildTools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set VULKAN_SDK=C:\VulkanSDK\1.4.363.0
set PATH=%VULKAN_SDK%\Bin;%PATH%

cd /d "%~dp0yue2.cpp"
if not exist build mkdir build
cd build
cmake .. -G "Ninja Multi-Config" -DGGML_VULKAN=ON || exit /b 1
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS% || exit /b 1
rem 1.0.45 : moteur copié dans le Studio installé (music\outils\moteur), qui ne le cherche plus dans yue2.cpp\build.
set OUT=%~dp0..\outils\moteur
if not exist "%OUT%" mkdir "%OUT%"
for %%f in (yue-synth.exe yue-transcribe.exe yue-plan.exe ggml.dll ggml-base.dll ggml-cpu.dll ggml-vulkan.dll) do copy /y "Release\%%f" "%OUT%\" >nul || exit /b 1
echo Moteur copie dans %OUT%
