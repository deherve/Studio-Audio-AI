# Construit le livrable distribution\StudioAudioAI (sans modèles ni données personnelles) à partir du projet, puis le zippe
# en distribution\StudioAudioAI-<version>.zip. Le dossier cible est entièrement vidé avant chaque construction.
# Usage : powershell -ExecutionPolicy Bypass -File native\make_distribution.ps1 [-NoZip]
#
# MODÈLE DE RÉFÉRENCE (1.0.31) : une copie du livrable paramétrée à la main, gardée hors du dépôt.
# Tout réglage qu'il y fait devient une valeur par défaut du code (struct Settings de main.cpp) : on ne livre JAMAIS de
# fichier de réglages (donnees\ est créé au premier lancement chez chaque utilisateur). Ne pas modifier ce dossier de
# référence ; les essais se font ailleurs (Downloads\test_livrable_verif).
#
# Structure du livrable (1.0.42) :
#   Studio Audio AI.exe, onnxruntime*.dll, msvcp140*.dll, vcruntime140*.dll   (à côté de l'exe)
#   LISEZMOI.md
#   outils\moteur\     yue-synth.exe, yue-transcribe.exe, ggml*.dll (moteur CUDA facultatif : outils\moteur\cuda, zip à part)
#   outils\ffmpeg\     ffmpeg.exe, ffprobe.exe (build « essentials »)
#   outils\whisper\    whisper-cli.exe
#   outils\llama\      llama.cpp processeur (traduction des paroles, IA locale de secours)
#   outils\llama-vulkan\  llama.cpp pour la carte graphique : téléchargé depuis le Studio (onglet Composer avec l'IA)
#   ressources\aide\, ressources\langues\
#   outils\sd-vulkan\  stable-diffusion.cpp (pochettes)
#   modeles\           musique, lora, analyse, voix, traduction, ia, images (vides : téléchargés au premier lancement)
#   Audios\            morceaux de démonstration (native\distribution_fichiers\Audios, s'il existe) ; 1.0.43 : un dossier
#                      par groupe, puis par album, puis par morceau (« NN. Titre »), pochettes et vidéos de l'album à côté
#   LICENSES\
#   donnees\, config\ et cache\ ne sont pas livrés : le Studio les crée au premier lancement (1.0.41 : config\ =
#   réglages, styles, mots à éviter, clés d'API ; cache\ = brouillon, sante, temp ; donnees\ = pochettes).
param([switch]$NoZip)
$ErrorActionPreference = 'Stop'
# 1.0.45 : sources et zips dans music\_developpement ($dev), Studio installé à la racine de music ($root).
$dev     = Split-Path $PSScriptRoot -Parent
$root    = Split-Path $dev -Parent
$distDir = Join-Path $dev 'distribution'
$dist    = Join-Path $distDir 'StudioAudioAI'
$fixed   = Join-Path $PSScriptRoot 'distribution_fichiers'   # fichiers fixes du livrable (LISEZMOI, licences, démos…)
$rel     = Join-Path $dev 'yue2.cpp\build\Release'
$version = ([regex]::Match((Get-Content (Join-Path $PSScriptRoot 'src\version.hpp') -Raw), 'STUDIO_VERSION\s+"([^"]+)"')).Groups[1].Value
$vcrt    = 'msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll'

function Copy-Runtime($dir) { foreach ($f in $vcrt) { Copy-Item "$env:WINDIR\System32\$f" $dir -Force } }

# Archive zip standard (séparateurs « / ») avec le tar.exe de Windows ; Compress-Archive de PowerShell 5.1 écrit des « \ »
# que certains outils de décompression (hors Windows) lisent mal.
function New-Zip($parent, $folder, $zip) {
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
    & "$env:WINDIR\System32\tar.exe" -a -c -f $zip -C $parent $folder
    if ($LASTEXITCODE -ne 0) { throw "tar.exe n'a pas pu créer $zip" }
    "{0}  ({1:N0} Mo)" -f $zip, ((Get-Item $zip).Length / 1MB)
}

# ---- 1. Dossier cible vidé puis recréé
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
$dirs = 'outils\moteur','outils\ffmpeg','outils\whisper','outils\llama','outils\llama-vulkan','ressources','LICENSES','Audios',
        'modeles\musique','modeles\lora','modeles\analyse','modeles\voix','modeles\traduction','modeles\ia','modeles\images','outils\sd-vulkan'
foreach ($d in $dirs) { New-Item -ItemType Directory -Force (Join-Path $dist $d) | Out-Null }

# ---- 2. Application : l'exécutable le plus récent (celui de la racine, ou celui qui vient d'être compilé si
#         l'installation à la racine a été bloquée parce que le Studio tournait)
$exe   = Get-Item "$root\Studio Audio AI.exe"
$built = Get-Item "$dev\native\build\Studio Audio AI.exe" -ErrorAction SilentlyContinue
if ($built -and $built.LastWriteTime -gt $exe.LastWriteTime) { $exe = $built }
Copy-Item $exe.FullName "$dist\Studio Audio AI.exe" -Force
foreach ($f in 'onnxruntime.dll','onnxruntime_providers_shared.dll') { Copy-Item "$dev\native\third_party\onnxruntime\lib\$f" $dist -Force }
Copy-Runtime $dist
Copy-Item "$fixed\LISEZMOI.md" $dist -Force

# ---- 3. Moteur de synthèse
foreach ($f in 'yue-synth.exe','yue-transcribe.exe','yue-plan.exe','ggml.dll','ggml-base.dll','ggml-cpu.dll','ggml-vulkan.dll') { Copy-Item "$rel\$f" "$dist\outils\moteur" -Force }
Copy-Runtime "$dist\outils\moteur"

# ---- 4. Outils : ffmpeg (build « essentials », bien plus légère que la « full »), whisper, llama.cpp processeur réduit
$ffSrc = Join-Path $dev 'native\third_party\ffmpeg_essentials'
if (-not (Test-Path "$ffSrc\ffmpeg.exe")) {   # à défaut, celui du PATH
    $ff = (Get-Command ffmpeg).Source
    $ffSrc = Split-Path (Resolve-Path $ff).Path -Parent
    if ((Get-Item $ff).Target) { $ffSrc = Split-Path ((Get-Item $ff).Target | Select-Object -First 1) -Parent }
}
foreach ($f in 'ffmpeg.exe','ffprobe.exe') { Copy-Item "$ffSrc\$f" "$dist\outils\ffmpeg" -Force }
Copy-Item "$root\outils\whisper\whisper-cli.exe" "$dist\outils\whisper" -Force
Copy-Runtime "$dist\outils\whisper"
# llama.cpp : seuls llama-server (IA locale), llama-completion / llama-cli (traduction) et leurs bibliothèques servent.
$llamaKeep = 'llama-server.exe','llama-server-impl.dll','llama-completion.exe','llama-completion-impl.dll','llama-cli.exe','llama-cli-impl.dll',
             'llama.dll','llama-common.dll','mtmd.dll','ggml.dll','ggml-base.dll','ggml-rpc.dll','libomp.dll'
Get-ChildItem "$root\outils\llama" -File | Where-Object { $llamaKeep -contains $_.Name -or $_.Name -like 'ggml-cpu-*.dll' } |
    ForEach-Object { Copy-Item $_.FullName "$dist\outils\llama" -Force }
Copy-Runtime "$dist\outils\llama"
# Pochettes (1.0.36) : stable-diffusion.cpp Vulkan (sd-cli + bibliothèques ; sans sd-server ni textes de licence, rangés dans LICENSES)
Get-ChildItem "$root\outils\sd-vulkan" -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { Copy-Item $_.FullName "$dist\outils\sd-vulkan" -Force }
Copy-Runtime "$dist\outils\sd-vulkan"
Set-Content -Encoding UTF8 "$dist\outils\llama-vulkan\TELECHARGE_PAR_LE_STUDIO.txt" @(
    "llama.cpp pour la carte graphique (Vulkan, ~37 Mo) : telecharge ici par Studio Audio AI, onglet Composer avec l'IA.",
    "llama.cpp for the graphics card (Vulkan): downloaded here by Studio Audio AI, Compose with AI tab.")

# ---- 5. Ressources (aide sans les images d'anciennes fonctions, langues)
Copy-Item "$root\ressources\aide" "$dist\ressources\aide" -Recurse -Force
foreach ($old in 'import_ia.jpg') { Remove-Item "$dist\ressources\aide\img\$old" -Force -ErrorAction SilentlyContinue }
Copy-Item "$root\ressources\langues" "$dist\ressources\langues" -Recurse -Force

# ---- 6. Modèles (dossiers vides avec leurs notes), démos, licences
Copy-Item "$fixed\modeles\*" "$dist\modeles" -Recurse -Force
if (Test-Path "$fixed\Audios") { Copy-Item "$fixed\Audios\*" "$dist\Audios" -Recurse -Force }
Copy-Item "$fixed\LICENSES\*" "$dist\LICENSES" -Force
Copy-Item "$dev\yue2.cpp\LICENSE" "$dist\LICENSES\yue2.cpp-MIT.txt" -Force
Copy-Item "$root\LICENSE" "$dist\LICENSES\StudioAudioAI-MIT.txt" -Force  # 1.0.86 : code du Studio sous MIT
Copy-Item "$dev\native\third_party\imgui\LICENSE.txt" "$dist\LICENSES\DearImGui-MIT.txt" -Force
Copy-Item "$root\outils\whisper\LICENSE-whisper.cpp.txt" "$dist\LICENSES\whisper.cpp-MIT.txt" -Force
Copy-Item "$dev\native\third_party\onnxruntime\LICENSE" "$dist\LICENSES\ONNXRuntime-MIT.txt" -Force
if (Test-Path "$root\outils\llama\LICENSE-llama.cpp.txt") { Copy-Item "$root\outils\llama\LICENSE-llama.cpp.txt" "$dist\LICENSES\llama.cpp-MIT.txt" -Force }
Copy-Item "$root\outils\sd-vulkan\stable-diffusion.cpp.txt" "$dist\LICENSES\stable-diffusion.cpp-MIT.txt" -Force
Copy-Item "$root\outils\sd-vulkan\ggml.txt" "$dist\LICENSES\ggml-MIT.txt" -Force

# ---- 7. Contrôle : rien de personnel ni de lourd dans le livrable
$forbidden = Get-ChildItem $dist -Recurse -File | Where-Object {
    $_.Name -match '^(studio_reglages\.json|studio_brouillon\.json|studio_modeles\.txt|createur_reglages\.json|forge_reglages\.json|reglages\.json|styles\.json|mots_a_eviter\.json|cles_api\.json|dernier_album\.json|brouillon\.json)$' -or
    $_.Name -match '^studio_yue2_.*\.(txt|log)$' -or
    $_.Extension -in '.gguf','.safetensors','.onnx','.part','.yue2pack' -or
    $_.FullName -match '\\(donnees|config|cache|licences|createur|pochettes|sante|temp)\\' }
if ($forbidden) { throw ("Fichiers interdits dans le livrable : " + (($forbidden | ForEach-Object { $_.FullName.Substring($dist.Length + 1) }) -join ', ')) }
"Livrable : {0}  ({1} fichiers, {2:N0} Mo)" -f $dist, (Get-ChildItem $dist -Recurse -File).Count, ((Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)

# ---- 8. Moteur CUDA facultatif (cartes Nvidia), produit par build_yue2cpp_cuda.cmd : zip à part (~600 Mo de DLL CUDA)
#         à décompresser dans le dossier de l'application (il crée outils\moteur\cuda).
$cuda     = Join-Path $root 'outils\moteur\cuda'
$cudaDist = Join-Path $distDir 'StudioAudioAI-moteur-CUDA'
if (Test-Path "$cuda\yue-synth.exe") {
    if (Test-Path $cudaDist) { Remove-Item $cudaDist -Recurse -Force }
    New-Item -ItemType Directory "$cudaDist\outils\moteur\cuda" | Out-Null
    Copy-Item "$cuda\*" "$cudaDist\outils\moteur\cuda" -Recurse -Force
    Copy-Runtime "$cudaDist\outils\moteur\cuda"
    if (-not $NoZip) {
        New-Zip $cudaDist 'outils' (Join-Path $distDir "StudioAudioAI-$version-moteur-CUDA.zip")
    }
} else {
    "Moteur CUDA absent (build_yue2cpp_cuda.cmd non lancé) : pas de zip CUDA."
}

# ---- 8 bis. Pack Admin (1.0.44) : Admin_tools\{Createur_styles, Forge}, à décompresser dans le dossier
#         du Studio. Seulement les programmes et leurs bibliothèques
#         (ni aucun réglage ni projet).
$admin    = Join-Path $root 'Admin_tools'
$adminDir = Join-Path $distDir 'StudioAudioAI-Admin'
if (Test-Path $adminDir) { Remove-Item $adminDir -Recurse -Force }
$adminOk = $false
if (Test-Path "$admin\Createur_styles\Createur de styles.exe") {
    New-Item -ItemType Directory "$adminDir\Admin_tools\Createur_styles" -Force | Out-Null
    foreach ($f in 'Createur de styles.exe','onnxruntime.dll','onnxruntime_providers_shared.dll') { Copy-Item "$admin\Createur_styles\$f" "$adminDir\Admin_tools\Createur_styles" -Force }
    Copy-Runtime "$adminDir\Admin_tools\Createur_styles"
    $adminOk = $true
}
if (Test-Path "$admin\Forge\YuELoraForge.exe") {   # la Forge se range elle-même dans Admin_tools\Forge (fil de la Forge)
    New-Item -ItemType Directory "$adminDir\Admin_tools\Forge" -Force | Out-Null
    Copy-Item "$admin\Forge\YuELoraForge.exe" "$adminDir\Admin_tools\Forge" -Force
    foreach ($f in 'LISEZMOI.md') { if (Test-Path "$admin\Forge\$f") { Copy-Item "$admin\Forge\$f" "$adminDir\Admin_tools\Forge" -Force } }
    # Code d'entraînement (la Forge lance <exe>\python\forge_train.py) : seulement les sources .py et leurs textes de
    # licence, jamais __pycache__, *.pyc ni environnement Python.
    Get-ChildItem "$admin\Forge\python" -Recurse -File | Where-Object {
        $_.FullName -notmatch '\\(__pycache__|venv|\.venv|env|site-packages|Lib|Scripts)\\' -and $_.Extension -in '.py','.txt','.md' -or
        ($_.Name -eq 'LICENSE' -and $_.FullName -notmatch '__pycache__') } | ForEach-Object {
        $dst = Join-Path "$adminDir\Admin_tools\Forge" $_.FullName.Substring("$admin\Forge\".Length)
        New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
        Copy-Item $_.FullName $dst -Force
    }
    New-Item -ItemType Directory "$adminDir\Admin_tools\LICENSES" -Force | Out-Null
    Copy-Item "$admin\Forge\python\trainer_core\LICENSE_ComfyUI-YuE2-Trainer.txt" "$adminDir\Admin_tools\LICENSES\ComfyUI-YuE2-Trainer-MIT.txt" -Force
} else {
    "Forge absente d'Admin_tools\Forge : pas dans le pack Admin."
}
$forbiddenAdmin = Get-ChildItem $adminDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -match '^(.*_reglages\.json|forge_mesures\.json|reglages\.json|styles\.json|mots_a_eviter\.json|cles_api\.json)$' -or
    $_.Extension -in '.gguf','.safetensors','.onnx','.csv','.bin','.yue2pack','.pyc','.pt','.pth' -or
    $_.FullName -match '\\(projets|sources|__pycache__|config|cache|donnees|Audios|licences)\\' }
if ($forbiddenAdmin) { throw ("Fichiers interdits dans le pack Admin : " + (($forbiddenAdmin | ForEach-Object { $_.FullName.Substring($adminDir.Length + 1) }) -join ', ')) }
if ($adminOk -and -not $NoZip) { New-Zip $adminDir 'Admin_tools' (Join-Path $distDir "StudioAudioAI-Admin-$version.zip") }

# ---- 9. Archive
if (-not $NoZip) {
    New-Zip $distDir 'StudioAudioAI' (Join-Path $distDir "StudioAudioAI-$version.zip")
}
