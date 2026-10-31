@echo off
rem ============================================================================
rem  Compilation du moteur yue2.cpp en version CUDA (cartes Nvidia).
rem
rem  Prérequis (une seule fois) :
rem   - Visual Studio 2022 BuildTools (déjà installé pour le Studio) ;
rem   - NVIDIA CUDA Toolkit 12.x : https://developer.nvidia.com/cuda-downloads
rem     (Windows > x86_64 > 11 > exe (local)). Pas besoin de carte Nvidia pour compiler.
rem
rem  Résultat : dossier « moteur_cuda » à la racine du projet, avec yue-synth.exe,
rem  yue-transcribe.exe, les DLL ggml (dont ggml-cuda.dll) et les DLL CUDA (cudart,
rem  cublas). L'application l'utilise automatiquement sur une carte Nvidia.
rem  Dossier séparé : ses DLL ggml diffèrent de celles du moteur Vulkan.
rem
rem  Durée : longue (30 à 60 minutes), les noyaux CUDA sont compilés pour plusieurs
rem  générations de cartes (RTX 20 à RTX 50).
rem ============================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

if "%CUDA_PATH%"=="" (
    echo CUDA Toolkit introuvable : installez-le depuis https://developer.nvidia.com/cuda-downloads
    echo puis fermez et rouvrez cette fenetre.
    exit /b 1
)
echo CUDA Toolkit : %CUDA_PATH%

rem Compilation dans un dossier à part (build_cuda) pour ne pas toucher au moteur Vulkan (build).
cd /d "%~dp0yue2.cpp"
if not exist build_cuda mkdir build_cuda
cd build_cuda
cmake .. -G "Ninja Multi-Config" -DGGML_CUDA=ON -DGGML_VULKAN=OFF || exit /b 1
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS% || exit /b 1

rem Copie du moteur et de ses DLL dans moteur_cuda.
set OUT=%~dp0..\outils\moteur\cuda
if not exist "%OUT%" mkdir "%OUT%"
for %%f in (yue-synth.exe yue-transcribe.exe ggml.dll ggml-base.dll ggml-cpu.dll ggml-cuda.dll) do (
    copy /y "Release\%%f" "%OUT%\" >nul || (echo Fichier manquant : %%f & exit /b 1)
)
rem DLL d'exécution CUDA : dans bin (CUDA 12) ou bin\x64 (CUDA 13).
for %%d in ("%CUDA_PATH%\bin" "%CUDA_PATH%\bin\x64") do (
    if exist "%%~d\cudart64_*.dll" copy /y "%%~d\cudart64_*.dll" "%OUT%\" >nul
    if exist "%%~d\cublas64_*.dll" copy /y "%%~d\cublas64_*.dll" "%OUT%\" >nul
    if exist "%%~d\cublasLt64_*.dll" copy /y "%%~d\cublasLt64_*.dll" "%OUT%\" >nul
)
rem Licence des DLL CUDA redistribuées.
if exist "%CUDA_PATH%\EULA.txt" copy /y "%CUDA_PATH%\EULA.txt" "%OUT%\CUDA-EULA.txt" >nul

echo.
echo Moteur CUDA pret dans : %OUT%
dir /b "%OUT%"
endlocal
