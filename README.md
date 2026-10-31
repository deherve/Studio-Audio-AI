# Studio Audio AI

Application Windows native (C++20, Win32, DirectX 11, Dear ImGui) qui génère des chansons en local avec le modèle YuE2-3B, puis crée les albums, les pochettes et les clips vidéo. Tout tourne sur le PC, sans serveur.

- Notes du projet : [CLAUDE.md](CLAUDE.md)
- Détails techniques : [_developpement/README.md](_developpement/README.md)
- Guide de l'utilisateur : [LISEZMOI](_developpement/native/distribution_fichiers/LISEZMOI.md)
- Aide : [ressources/aide](ressources/aide)

## Ce dépôt ne contient que les sources

Restent hors du dépôt : modèles, morceaux, réglages, clés d'API, exécutables, ffmpeg et démos audio.

Pour compiler, il faut aussi :

- `_developpement/yue2.cpp` : <https://github.com/ServeurpersoCom/yue2.cpp>, avec les correctifs de `_developpement/patches`
- `_developpement/whisper.cpp` : <https://github.com/ggml-org/whisper.cpp>
- `_developpement/native/third_party/ffmpeg_essentials` : ffmpeg pour Windows, pour fabriquer le livrable

Puis lancer `_developpement/build_studio_natif.cmd`, et `_developpement/native/make_distribution.ps1` pour les zips.

## Licences

Modèles YuE2 : CC BY-NC 4.0. Modèles Essentia : CC BY-NC-SA 4.0. Ces deux licences limitent l'usage à du non commercial. Les licences des composants sont dans `_developpement/native/distribution_fichiers/LICENSES`.
