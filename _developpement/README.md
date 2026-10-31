# Studio Audio AI — documentation du projet

Générateur de chansons local (YuE2-3B) pour PC Windows, carte AMD ou Nvidia (Vulkan, CUDA facultatif). Version : `native\src\version.hpp` (+0.0.1 à chaque modification). Guide utilisateur : `distribution\StudioAudioAI-portable\LISEZMOI.md`.

## Organisation
| Chemin | Contenu |
|---|---|
| `native\` | application native C++20 (Win32 + DirectX 11 + Dear ImGui) — **version à jour** |
| `native\src\main.cpp` | interface (formulaire, bibliothèque, albums, vidéos, réglages) |
| `native\src\studio.cpp/.hpp` | file d'attente, lancement de `yue-synth`, métadonnées, santé GPU, `Paths` |
| `native\src\media.cpp/.hpp` | exports d'album (pochette intégrée), vidéos par titre (ffmpeg) |
| `native\src\suno.cpp/.hpp` | convertisseur Suno → YuE2, 42 profils d'influence, langues |
| `native\src\setup.cpp/.hpp` | premier lancement : téléchargement (WinHTTP, reprise) ou choix du dossier des modèles |
| `native\src\sysinfo.cpp/.hpp` | mémoire vive, processeur, type du disque des modèles (SSD/HDD, `IOCTL_STORAGE_QUERY_PROPERTY`) pour les recommandations ; mémoire vidéo par programme (compteurs PDH « GPU Process Memory ») pour la vérification avant génération |
| `native\createur\createur_styles.cpp` | Créateur de styles (outil de l'éditeur, hors distribution) : analyse une ou plusieurs chansons d'inspiration (même code que le Studio : `analysis.cpp`, sans paroles), fusionne les balises (cochées si présentes dans au moins la moitié des chansons), propose la structure la plus fréquente, et range le style dans un pack `.yue2pack` (dossier `packs\` par défaut) → `createur\Createur de styles.exe` (avec les DLL d'ONNX Runtime ; icône : `native\createur\createur.ico`, fabriquée par `native\tools\make_creator_icon.py`) ; réglages dans `createur\createur_reglages.json` |
| `native\src\translate.cpp/.hpp` | traduction des paroles en gardant la structure : Google Traduction (point d'accès gratuit « gtx », via `net.cpp`) ou, hors ligne, llama.cpp (`traduction\llama-completion.exe`, processeur) + Qwen2.5-3B-Instruct (`models\traduction`) avec lignes numérotées |
| `native\src\gpumem.cpp/.hpp` | mémoire vidéo utilisée et débordement (compteurs PDH « GPU Adapter Memory »), utilisation CPU (« Processor Information ») et GPU (« GPU Engine », moteur le plus chargé de la carte) : quatre jauges sous l'en-tête |
| `native\src\analysis.cpp/.hpp` | analyse audio d'un fichier de reprise : spectrogramme mel, Discogs-EffNet et classificateurs Essentia (ONNX Runtime, `native\third_party\onnxruntime`), partition SheetSage2 (tempo, tonalité, structure), suggestions de balises YuE2 ; modèles dans `modeles\analyse\` (CC BY-NC-SA 4.0) |
| `whisper\whisper-cli.exe`, `whisper.cpp\`, `build_whisper_vulkan.cmd` | transcription des paroles : whisper.cpp (Vulkan, exécutable séparé car ses DLL GGML diffèrent de celles de yue2.cpp) ; modèle `modeles\voix\ggml-medium-q5_0.bin` téléchargé depuis les Réglages |
| `native\src\download.cpp/.hpp` | téléchargements HTTPS avec reprise (premier lancement, LoRA) |
| `native\src\lora_catalog.hpp` | catalogue de 40 LoRA (URLs figées, tailles, mots déclencheurs), généré depuis `adapter-catalog.json` de YuE2-Studio |
| `IDEES_YuE2-Studio.md` | idées tirées du projet YuE2-Studio, à intégrer plus tard |
| `native\src\player.cpp` | lecture (MCI) |
| `yue2.cpp\` | moteur de synthèse (dépôt ServeurpersoCom/yue2.cpp, patches locaux : attention par blocs dans `src\nar.h` et `src\qwen3-lm.h`, solveurs AB2, AB3 et Euler (`src\flow-solver.h`, champ `solver` de la requête, option `--solver`) ; diff complet dans `yue2cpp_patches\local_patches.diff`) |
| `native\src\gpuinfo.cpp/.hpp` | carte graphique (marque, nom, mémoire via DXGI) et profil de calcul : variables `GGML_VK_*` (`apply_gpu_env()`, posées avant chaque lancement par `run_process`), options du moteur (`engine_flags()`), choix Vulkan/CUDA |
| fichiers `*.yue2pack` | styles (préréglages) et structures exportés ou packs de styles : JSON `{"format":"yue2pack","version":1,"pack":{name,author,version,description},"styles":[…],"structures":[{name,sections}]}` ; import/export dans Réglages › Génération (`read_pack`/`write_pack` dans main.cpp), glisser-déposer sur la fenêtre |
| `native\src\version.hpp` | numéro de version (barre de titre, en-tête) et `UPDATE_MANIFEST_URL` (adresse de recherche de mise à jour, vide pour l'instant ; ou clé `update_url` de `studio_reglages.json`) |
| `native\src\update_check.cpp/.hpp`, `native\src\net.cpp/.hpp` | recherche de mise à jour (publication GitHub `releases/latest` ou JSON `{version,url,notes}`) ; requêtes HTTPS courtes partagées (`https_get`) |
| `native\src\lyrics_web.cpp/.hpp` | reprises : identité du morceau (ffprobe, sinon nom et dossiers), paroles sur lrclib.net (WinHTTP, délais courts), vérification par les mots de Whisper, structure reconstruite (`web_lyrics_to_yue2`), filtre des inventions de Whisper (`is_whisper_hallucination`, appelé par `parse_whisper`) ; Whisper lancé avec `-mc 0` |
| `patches\yue2cpp_negative_style.patch` | modification du moteur yue2.cpp pour les balises négatives (clé JSON `negative_style`, guidage CFG 1,5 par défaut) ; à réappliquer après une mise à jour de yue2.cpp (sauvegarde d'origine : `_sauvegardes\yue2cpp_src_2026-10-21_avant_negatif\`) |
| `moteur_cuda\`, `build_yue2cpp_cuda.cmd` | moteur CUDA facultatif (cartes Nvidia) : compilé dans `yue2.cpp\build_cuda`, copié avec ses DLL CUDA ; dossier séparé (DLL ggml différentes) |
| `_a_supprimer\` | fichiers de test et ancienne version web (FastAPI) mis de côté le 21/10/2026, à supprimer |
| `bibliotheque\` | morceaux : `meta.json`, `request.json`, `audio.json`, `audio.wav/flac/mp3`, `generation.log` |
| `models\` | `gguf\` (modèles), `adapters\` (LoRA) et `analysis\` (modèle d'analyse audio) |
| `prompt_YuE2.md` | prompt pour une IA qui écrit paroles/styles au format YuE2 |
| `distribution\` | version portable (`StudioAudioAI-portable\` et son `.zip`) |

## Aide intégrée (règle de maintenance)
`aide\aide.html` (français, référence) et `aide\aide.en.html` (anglais : même structure, à mettre à jour en même temps ; autres langues : `aide.<code>.html`, repli sur l'anglais) (+ `aide\img\`) est la page d'aide de l'application (bouton « Aide », F1 ; `open_help()` dans `main.cpp`). **Toute modification, ajout de fonctionnalité ou de réglage doit être répercutée dans cette page** (section concernée + journal des évolutions + date), ainsi que dans `LISEZMOI.md`. `make_distribution.ps1` copie le dossier `aide` dans la version portable.

## Traductions (règle de maintenance)
L'interface est traduite en 10 langues par des **fichiers JSON** (`langues\<code>.json`, un par langue, plus `_modele.json`). Les textes du code sont en français, entre `tr("...")` / `trf("...")` (format printf) / `wtr("...")` (UTF-16, menus et boîtes Windows) / `N_("...")` (tableaux statiques, traduits à l'affichage avec `tr()`) ; le texte français sert de clé. Module `native\src\i18n.cpp` (chargement, repli anglais puis français, polices par écriture dans `build_fonts()` de `main.cpp`). **Tout texte nouveau ou modifié doit être traduit** : lancer `python native\tools\i18n_sync.py` (ajoute les clés manquantes aux fichiers, retire les obsolètes, vérifie les marques de format), puis remplir les valeurs vides des 9 langues. Voir `langues\LISEZMOI.md` (règles pour les traducteurs). `make_distribution.ps1` copie `langues\`.

## Compiler
Prérequis : Visual Studio 2022 Build Tools (C++), CMake, Ninja, Vulkan SDK (moteur).
- Moteur : `build_yue2cpp_vulkan.cmd` → `yue2.cpp\build\Release\yue-synth.exe`.
- Moteur CUDA (facultatif, CUDA Toolkit 12.x) : `build_yue2cpp_cuda.cmd` → `moteur_cuda\` ; `make_distribution.ps1` en fait un zip à part (`StudioAudioAI-moteur-CUDA.zip`).
- Application : `build_studio_natif.cmd` → `Studio Audio AI.exe` à la racine (échoue si l'application tourne).
- Distribution : `powershell -ExecutionPolicy Bypass -File native\make_distribution.ps1` (reconstruit le dossier portable puis le zip).

## Résolution des chemins au démarrage
1. Moteur : `yue-synth.exe` cherché dans le dossier de l'exécutable puis ses parents (`yue2.cpp\build\Release` ou directement à côté). Moteur CUDA : `moteur_cuda\` (ou `yue2.cpp\build_cuda\Release`), utilisé si la carte est Nvidia (ou si « Moteur de calcul » = CUDA).
1 sexies. Atelier (1.0.45) : sources et fabrication dans `music\_developpement\` (native, yue2.cpp, whisper.cpp, patches, build_*.cmd, distribution, _a_supprimer, ce README) ; la racine de music = le Studio installé (exe, onnxruntime*.dll, Admin_tools, Audios, cache, config, donnees, modeles, outils, ressources, _sauvegardes, CLAUDE.md). CMake copie l'exe et les outils vers `APP_ROOT` (= native/../..) ; make_distribution sépare `$dev` et `$root` ; i18n_sync écrit dans `../ressources/langues` ; build_yue2cpp_vulkan.cmd copie le moteur dans `music\outils\moteur` (le Studio de développement ne le cherche plus dans yue2.cpp\build, le repli reste pour les sources seules).
1 quinquies. Outils de l'éditeur (1.0.44) : `Admin_tools\Createur_styles` (exe + DLL ONNX + createur_reglages.json ; `Paths::locate(…, shared_root = true)` remonte jusqu'au Studio : yue2.cpp\build en développement, `outils\moteur` installé), `Admin_tools\Forge` (rangée par le fil de la Forge). CMake copie les exe dans Admin_tools. make_distribution : second zip `StudioAudioAI-Admin-<version>.zip` (Admin_tools seul, à décompresser dans le dossier du Studio), contrôle : jamais de réglage ni de projet.
1 quater. Morceaux (1.0.43) : `Audios\<Groupe>\<Album>\<NN. Titre>\` (sans album : `Audios\<Groupe>\<Titre>\` ; « Sans groupe »), pochettes `pochette*.ext` et `videos\{YouTube,TikTok,IG}` dans le dossier de l'album ; dossier d'un morceau = `song_folder(id)` (table identifiant → dossier, remplie par `rescan` à partir des meta.json), déplacé par `place_song` à l'enregistrement (morceaux terminés, en erreur, annulés ou en pause). Migration `migrate_library` (studio.cpp) et `migrate_covers` (main.cpp).
1 ter. Dossiers (1.0.42) : `modeles\{musique, lora, analyse, voix, traduction, ia, images, entrainement}`, `outils\{moteur (+ cuda), ffmpeg, whisper, llama, llama-vulkan, sd-vulkan}`, `ressources\{aide, langues}` ; migration automatique `migrate_layout` (studio.cpp, journal `cache\migration.log`) ; plus aucune recherche dans les anciens dossiers. Dans le dossier de développement, l'aide et les langues sont donc dans `ressources\` (i18n_sync.py et make_distribution.ps1 y lisent).
1 bis. Réglages (1.0.41) : `config\` = `reglages.json`, `styles.json` (presets, structures, packs), `mots_a_eviter.json` (ai_avoid, ai_auto_avoid, ai_recent), `cles_api.json` (ai_keys, DPAPI), `dernier_album.json` ; `cache\` = `brouillon.json`, `sante\`, `temp\`. Migration automatique depuis `donnees\` (`migrate_settings` dans main.cpp, `migrate_cache` dans studio.cpp).
2. Modèles : toujours `<exe>\modeles` (1.0.32 : chaque copie n'utilise que ses propres dossiers `models`, `donnees`, `bibliotheque` ; un ancien `donnees\studio_modeles.txt` est renommé `_ancien_studio_modeles.txt` et ignoré). Absents → proposition de téléchargement dans ce dossier, ou ouverture du dossier pour y copier les fichiers. Toute suppression de fichier est refusée hors du dossier du programme (`inside_app_dir`). Un dossier valide contient `YuE2-Vae-F32.gguf` et au moins un `YuE2-3B-*.gguf` (directement ou dans `gguf\`). Modèle par défaut : Q6_K, à défaut Q8_0, Q5_K_M ou le premier trouvé ; le choix de l'utilisateur est dans `config\reglages.json` (`model` ; `donnees\studio_reglages.json` avant la 1.0.41) et chaque morceau mémorise son modèle (`model` dans `meta.json`).
3. Données (bibliothèque, réglages, vidéos) : dans le dossier du moteur/de l'application.

## Points techniques à retenir
- Options du moteur : Vulkan `--no-fa --clamp-fp16` (obligatoires sur RDNA2) ; CUDA `--clamp-fp16`.
- Variables d'environnement en mode stable (AMD, Intel, ou forcé) : `GGML_VK_DISABLE_F16=1`, `GGML_VK_DISABLE_INTEGER_DOT_PRODUCT=1`, `GGML_VK_DISABLE_MMVQ=1` — les noyaux FP16/entiers de Vulkan sont instables sur RX 6700 XT (cause des grésillements). Retirées en mode rapide (Nvidia). Réglages `gpu_compat` et `gpu_backend` dans `studio_reglages.json`.
- Requêtes : `peak_clip=0`, sortie WAV 24 bits ; FLAC/MP3 avec −1 dB de marge.
- Re-rendre rejoue `audio.json` (mêmes `semantic_tokens`) ; recomposer génère une nouvelle composition.
- Vidéos : une par titre et par réseau, écrites en `.part`, contrôlées (décodage + image non noire) puis renommées ; rangées dans `videos\YT` et `videos\TT`.
- Solveur de la phase NAR : réglage = méthode + pas (`SolverSpec` dans `studio.hpp`/`studio.cpp`, clés `ab3_16`, `ab3_20`, `ab3_24`, `euler_32`, `ab2_32` (défaut), `ab3_32`, `mp32`, classées du plus rapide au plus lent ; anciennes valeurs `ab2`/`midpoint` comprises). Le moteur reçoit `solver` + `steps`. Mesuré sur 180 s : 297 s (MP 32) contre 153 s (AB2 32) de NAR ; AB3 24 ≈ 115 s à précision MP 32. Choix par défaut dans Réglages, choix rapide dans la barre du haut, mémorisé par morceau.
- Boucle de rendu événementielle (≈ 0 % CPU au repos) ; icône de zone de notification avec pastilles.
