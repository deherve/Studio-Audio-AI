# Studio Audio AI (1.0.74)

Générateur de chansons local basé sur **YuE2-3B** (moteur `yue2.cpp`, Vulkan). Application Windows native : pas de navigateur, pas de serveur, tout reste sur votre PC.

## Configuration requise
- Windows 10 ou 11 (64 bits).
- Carte graphique compatible **Vulkan** (AMD, Nvidia ou Intel) avec beaucoup de mémoire vidéo (testé uniquement sur AMD RX 6700 XT 12 Go ; le modèle par défaut Q6_K pèse 2,9 Go).
- Cartes **Nvidia** : prises en charge mais non testées. Les réglages s'adaptent seuls ; pour le moteur **CUDA** (souvent plus rapide), décompressez le zip du moteur CUDA dans le dossier de l'application (il crée `outils\moteur\cuda`).
- 16 Go de RAM recommandés, 6 Go d'espace disque libre pour les modèles (davantage si vous gardez plusieurs quantifications).

## Premier lancement
1. Copiez le dossier où vous voulez (il est autonome : bibliothèque, réglages et modèles restent dedans) et lancez **Studio Audio AI.exe**.
2. Si le modèle est absent, une fenêtre propose :
   - **Oui** : télécharger les modèles depuis Hugging Face (≈ 4,6 Go, YuE2-3B en Q6_K) dans le sous-dossier `modeles\` (reprise automatique en cas de coupure) ;
   - **Non** : ouvrir le dossier `modeles\musique\` du programme pour y copier vous-même un modèle `YuE2-3B-*.gguf` (Q6_K, Q8_0, Q5_K_M…) et `YuE2-Vae-F32.gguf` déjà présents sur votre PC, puis répondre de nouveau ;
   - **Annuler** : quitter.
3. Modèle déjà téléchargé ou PC sans internet : déposez les fichiers `.gguf` dans `modeles\musique\` (et la LoRA dans `modeles\lora\`) avant de lancer l'application. Le détail des fichiers attendus est dans `modeles\LISEZMOI.txt`.

**Où sont vos fichiers ?** Toujours dans le dossier du programme, et nulle part ailleurs : modèles dans `modeles\musique\`, LoRA dans `modeles\lora\`, réglages, styles, mots à éviter, et clés d'API dans `config\`, morceaux dans `Audios\` (un dossier par groupe, puis par album, puis par morceau). Deux copies du Studio ne partagent jamais leurs fichiers : supprimer un modèle ou une LoRA dans l'une ne touche pas l'autre.

**Choix de la quantification** : le modèle principal existe en Q8_0 (3,8 Go, qualité maximale), Q6_K (2,9 Go, par défaut), Q5_K_M (2,6 Go) et BF16 (7,2 Go, trop lourd pour 12 Go de mémoire vidéo). Déposez les fichiers voulus dans `modeles\musique\` (https://huggingface.co/Serveurperso/YuE2-GGUF). Le modèle par défaut se règle dans **Réglages**, et une liste « Modèle » dans la barre du haut permet de changer pour les prochains morceaux ; chaque morceau retient le modèle qui l'a produit. Sur la RX 6700 XT, les trois quantifications testées ont donné la même vitesse et une qualité jugée équivalente ; Q6_K et Q5_K_M économisent seulement 0,3 à 0,4 Go de mémoire vidéo au pic.

Modèles utilisés : `YuE2-3B-Q6_K.gguf` (par défaut), `YuE2-Vae-F32.gguf`, `SheetSage2-Q8_0.gguf` + `MERT-v2-FullSong-Q8_0.gguf` (covers/reprises) et la LoRA instrumentale `adapters\ar_lora_inst_v3abc.bf16.safetensors`.

## Styles, structures et packs
Vos styles (préréglages) et vos structures (« Enregistrer la structure actuelle… » dans la liste des structures) peuvent être exportés et importés dans **Réglages › Génération** sous forme de fichier **.yue2pack** (sauvegarde, partage ou pack de styles). Un fichier .yue2pack peut aussi être glissé sur la fenêtre.

## Composer avec l'IA
L'onglet **Composer avec l'IA** (à côté de « Nouveau morceau ») fait écrire un morceau complet (titre, style, langue, paroles) par une IA, d'après la langue, le groupe ou chanteur d'inspiration et le thème choisis. La réponse est nettoyée au format YuE2 puis envoyée dans « Nouveau morceau ». Le moteur se règle dans **Réglages › IA (paroles)** : IA locale intégrée (modèle à télécharger depuis Hugging Face dans `modeles\ia`, et llama.cpp pour la carte graphique dans `outils\llama-vulkan`, 37 Mo, à télécharger une fois), serveur local (Ollama, LM Studio…) ou service en ligne avec clé d'API. L'IA locale intégrée ne travaille qu'au repos (aucune génération en cours). Choisissez **Un album** pour faire écrire un album entier d'un coup (1 à 50 morceaux, titre de l'album et nom du groupe facultatifs, chansons, instrumentaux ou mélangé) : chaque morceau est ajouté à la file en pause, avec les LoRA et réglages de « Nouveau morceau ». Pour des morceaux qui ne se ressemblent pas, **Réglages › IA › Mots à éviter** liste les mots que l'IA ne doit pas employer, et les mots qui reviennent souvent sont évités automatiquement (nom du groupe et titre de l'album compris, sauf ceux que vous tapez). L'onglet peut aussi créer les pochettes de l'album (case « Créer les pochettes » ; service local ou en ligne choisi dans Réglages › IA › Pochettes ; boutons « Générer » dans la fenêtre « Albums… »), et un clic sur un morceau de l'album affiche ses paroles. L'onglet a aussi le bloc LoRA (les mêmes LoRA cochées que dans « Nouveau morceau »), un nombre maximal d'instrumentaux par album « Mélangé » (Réglages › IA, 2 par défaut), son champ « Styles à éviter », et un bouton « Enregistrer comme style personnalisé » range le style écrit par l'IA dans vos styles.

## Contenu du dossier
Tout ce dont le Studio a besoin est dans ce dossier : il ne lit et n'écrit rien ailleurs (hormis les fichiers que vous choisissez vous-même). Vous pouvez le copier sur un autre disque ou un autre PC.

| Élément | Rôle |
|---|---|
| `Studio Audio AI.exe` | l'application |
| `onnxruntime*.dll` | ONNX Runtime (MIT) : exécute le modèle d'analyse audio |
| `msvcp140*.dll`, `vcruntime140*.dll` | bibliothèques Visual C++ (installation locale) |
| `outils\moteur\` | moteur de synthèse yue2.cpp (Vulkan) : `yue-synth.exe`, `yue-transcribe.exe`, `ggml*.dll` ; moteur CUDA facultatif dans `outils\moteur\cuda\` |
| `outils\ffmpeg\` | `ffmpeg.exe`, `ffprobe.exe` : conversions FLAC/MP3, exports, vidéos et clips animés, analyse |
| `outils\whisper\` | moteur de transcription des paroles (whisper.cpp, MIT) ; son modèle (environ 540 Mo) se télécharge dans Réglages › Analyse et paroles |
| `outils\llama\` | llama.cpp pour le processeur (traduction des paroles hors ligne) |
| `outils\llama-vulkan\` | llama.cpp pour la carte graphique (37 Mo), téléchargé depuis l'onglet Composer avec l'IA |
| `outils\sd-vulkan\` | stable-diffusion.cpp pour la carte graphique : pochettes d'album créées en local |
| `ressources\aide\` | **page d'aide complète** (bouton « Aide » de l'application ou touche F1) |
| `ressources\langues\` | **traductions de l'interface** : un fichier JSON par langue (français, anglais, espagnol, italien, allemand, portugais, japonais, coréen, mandarin, cantonais) ; une langue ajoutée dans ce dossier apparaît dans Réglages › Langue de l'application (voir `ressources\langues\LISEZMOI.md`) |
| `modeles\` | **dossier des modèles** : `musique\` (modèles de musique), `lora\` (LoRA), `analyse\` (analyse audio), `voix\` (transcription), `traduction\` (traduction hors ligne), `ia\` (Composer avec l'IA), `images\` (pochettes : Z-Image Turbo, 6,7 Go, téléchargé au clic dans Réglages › IA › Pochettes) ; vides tant que rien n'est téléchargé |
| `Audios\` | vos morceaux : `Audios\<Groupe>\<Album>\01. Titre\` (audio WAV/FLAC/MP3 et fiche), avec dans chaque album ses pochettes et ses vidéos (`videos\YouTube`, `TikTok`, `IG`) ; deux morceaux de démonstration y sont livrés dans `Studio Audio AI (démo)` : « Lumières du soir » (chanson) et « Horizon » (instrumental) |
| `config\` | créé au premier lancement, **à sauvegarder** : `reglages.json`, `styles.json` (styles, structures, packs), `mots_a_eviter.json`, `cles_api.json` (clés chiffrées par Windows, valables sur ce PC seulement), `dernier_album.json` |
| `cache\` | créé au premier lancement : brouillon du formulaire, test de santé de la carte, fichiers temporaires (jetables) |
| `donnees\` | pochettes des albums qui n'ont plus de morceau (les autres sont dans le dossier de leur album) |
| `LICENSES\` | licences des composants |

Les versions précédentes rangeaient ces éléments à la racine du dossier : ils restent reconnus, et sont rangés automatiquement au premier lancement (1.0.41 : réglages éclatés dans `config\`, l'ancien fichier gardé dans `donnees\_ancien_studio_reglages.json`).

## Utilisation
**Créer un morceau** : titre, style (balises en anglais séparées par des virgules, boutons par catégories : Genres, Ambiance, Voix, Instruments, Son ; environ 335 balises tirées du vocabulaire de la famille YuE, et le champ accepte aussi du texte libre ; langues : anglais, français, espagnol, italien, allemand, portugais, japonais, coréen, mandarin, cantonais), paroles avec sections `[Verse]`, `[Chorus]`… Le bouton de génération reste grisé tant que titre et style sont vides. Mode **instrumental** (sans paroles, LoRA dédiée), langue (anglais, français, espagnol, italien), durée maximale (180 s par défaut), 1 à 3 versions, graine aléatoire ou fixe.

**Adapter un prompt Suno** : collez titre, style et paroles dans le formulaire, puis « Adapter au format YuE2 » convertit un prompt de style Suno (titre, groupe, album, style en français, sections) en style et paroles YuE2. 42 profils d'influence (chanteur + musiciens) peuvent être ajoutés au style. Boutons « Vider » pour chaque champ.

**Blocs repliables et morceaux neufs** :
- Les blocs Morceau, Style, Paroles/Structure, LoRA et Génération se replient d'un clic sur leur titre (flèche ▾ / ▸) ; replié, un bloc affiche un résumé de son contenu. L'état est mémorisé. Dans le bloc Génération, les boutons « Générer… » et « Ajouter en pause » restent visibles sur une même ligne, même replié.
- Un morceau fraîchement généré et pas encore écouté s'affiche en rose (carte teintée, bordure rose, mention « Nouveau — à écouter et vérifier » ou pastille « À écouter ») dans les trois affichages de cartes. Il revient à la normale dès qu'on le lance en lecture ou qu'on le valide ou le signale. Le filtre « Nouveaux (à écouter) » les liste. Les morceaux déjà présents dans la bibliothèque sont considérés comme écoutés.

**Fin des morceaux** : un morceau qui atteint le plafond de durée sans conclure (c'est le cas des instrumentaux, que la LoRA instrumentale ne termine jamais, et de tout morceau à durée fixe) reçoit un fondu de sortie. Sa longueur se règle dans Réglages > Fin des morceaux (5 s par défaut, de 1 à 30 s, courbe linéaire). La case **« Fin en fondu sur la structure »** du bloc Génération remplace ce réglage pour le morceau : le fondu dure comme la dernière section de la structure (outro : 20 s en instrumental, 15 s en chanson) avec une courbe douce en demi-sinus. Aucun lien avec le solveur : AB2 et Midpoint donnent exactement la même fin.

**Thèmes et présentations** (Réglages > Thème graphique, et sélecteurs dans l'interface) :
- Quatre thèmes : *Minuit violet* (par défaut, suit le mode clair ou sombre de Windows), *Studio gris* (sombre, neutre, ambre, plus dense), *Papier encre* (clair chaud, bleu encre) et *Menthe clair* (clair, turquoise). Le changement est immédiat et mémorisé.
- Cartes de la bibliothèque (liste « Cartes » au-dessus de la liste) : *Détaillées* (par défaut), *Compactes* (une ligne par morceau terminé ; un clic sur le titre déplie la carte détaillée, un nouveau clic sur le titre la replie) ou *Avec pochette* (pochette de l'album à gauche, actions regroupées dans le menu « Actions ») ou *Pochette compacte* (une ligne par morceau avec une petite pochette, un clic sur le titre déplie la carte avec pochette, style et toutes les statistiques, un nouveau clic sur le titre la replie). La pochette d'un album se choisit dans « Modifier » (Actions > Modifier le titre, le groupe, l'album…) : une image par album, partagée par ses morceaux (sans album, propre au morceau) ; à défaut, l'image YouTube ou TikTok de l'album est utilisée, rognée au centre. Les morceaux en cours ou en erreur gardent toujours la carte détaillée.
- Bloc Style : un **curseur de tempo** (60 à 260 BPM, pas de 1, barre colorée Largo → Presto) lit et écrit la balise « BPM » du style ; « Retirer le tempo » la supprime.
- Bloc Style (« Présentation » en haut du bloc) : *Classique* (onglets), *Pastilles* (vos balises en pastilles retirables et catégories repliables) ou *Constructeur* (une liste par catégorie).
- Bloc LoRA : une carte par LoRA, avec sa description, son type et sa force.
- Marques des morceaux terminés (Réglages > Marques des morceaux, avec aperçu) : *Pouces* (par défaut), *Coche et croix* (cercles, coche verte pour « validé », croix rouge pour « à problème »), ou *Commutateur* à trois positions « À écouter / Validé / Problème » (un clic choisit l'état affiché). Le filtre de la bibliothèque (Validés, Non validés, À problème) ne change pas.

**Solveur de la phase audio (NAR)** : sept réglages (méthode + nombre de pas) sont disponibles pour la dernière étape de génération, qui représente l'essentiel du temps. Du plus rapide au plus lent : **AB3 16** (3,8×), **AB3 20** (3,0×), **AB3 24** (2,6×), **Euler 32** (2,0×, déconseillé : niveau et spectre altérés), **AB2 32** (1,9×, défaut), **AB3 32** (1,9×, plus précis) et **MP 32** (1×, la référence). La vitesse est relative à MP 32 ; mesuré sur 3 minutes, AB2 32 ramène la phase audio de 297 s à 153 s (génération complète 322 s → 178 s). AB3 24 a la précision de MP 32 pour environ 115 s de phase audio. Comparez à l'écoute sur vos paroles. Le solveur par défaut se choisit dans Réglages, un choix rapide existe dans la barre du haut, et chaque morceau retient le sien. Les estimations de temps restant en tiennent compte.

**LoRA** (bloc « LoRA » du formulaire, sous la structure des paroles) : des extensions qui orientent le style, la voix ou l'ambiance d'un morceau (chanson française, rock rauque, curseurs pop/metal/lo-fi, etc.). Elles sont optionnelles et cumulables : cochez la carte de celles à utiliser pour ce morceau, chacune avec sa force (1,0 = réglage conseillé). Une LoRA qui a un mot déclencheur (par ex. `chnsn`, `grvl`) l'ajoute automatiquement au début du style quand elle est cochée. Le tableau **Réglages > LoRA** liste un catalogue de 40 LoRA avec leur taille, leur type et un bouton **Télécharger** qui les place directement dans `modeles\lora\` ; une LoRA déposée à la main dans ce dossier est reconnue aussi. Les LoRA sont fusionnées dans le modèle au chargement : 10 à 30 s de plus par morceau (mesuré avec Q6_K, une LoRA, les deux moitiés du modèle), sans ralentissement ensuite, et très peu de mémoire en plus. Elles ont leurs propres licences, le plus souvent non commerciales. La LoRA instrumentale reste liée au mode Instrumental.

**Préréglages de style** (liste « Préréglages… » du bloc Style, « Enregistrer le style actuel… ») : un préréglage mémorise le style et, au choix, les LoRA cochées avec leur force et la structure des paroles (la liste des sections : Intro, Verse, Chorus…, pour une chanson ou un instrumental). Choisir un préréglage applique le tout : style, LoRA et structure. Si des paroles sont déjà écrites, la structure ne les remplace qu'après confirmation. Une LoRA du préréglage qui n'est pas installée est signalée, à télécharger dans Réglages > LoRA. Les anciens préréglages (style seul) fonctionnent toujours.

**Barre de mémoire GPU** (sous l'en-tête) : à gauche la mémoire vidéo occupée sur la carte, à droite le débordement vers la mémoire système (barre pleine = 2 Go). Un débordement fait fortement ralentir la génération : fermez alors les programmes qui utilisent la carte (aperçus de fichiers, navigateurs, jeux).

**Vérification avant génération** (1.0.6) : si la mémoire vidéo libre ne suffit pas pour le morceau, la fenêtre « Mémoire vidéo insuffisante » liste les programmes qui occupent la carte ; fermez-les puis « Générer quand même », ou « Ajouter en pause ».

**Recommandations pour ce PC** (1.0.5, Réglages › Système) : modèle et solveur conseillés selon la carte, la mémoire vive et le disque (SSD ou disque dur), avec un bouton « Appliquer les réglages conseillés ».

**Style à éviter** (1.0.7, expérimental, bloc Style) : balises que le morceau doit éviter (« vocoder, autotune »), avec un curseur de force (1,5 conseillé). Effet à juger à l'oreille ; composition environ 1,3 fois plus lente et 2,7 Go de mémoire vidéo en plus quand le champ est rempli. Les balises rapides sous le champ se complètent toutes seules avec les termes tapés ; la croix d'une balise la supprime (avec confirmation), et la liste se gère aussi dans Réglages › Génération (1.0.8).

**Traduction des paroles** (1.0.10, bloc Paroles) : choisissez une langue puis « Traduire » ; seules les lignes chantées sont traduites, la structure ne bouge pas. Avec Internet : Google Traduction (gratuit, plus de 100 langues). Sans Internet : modèle local (dossier `traduction` + modèle de 2 Go à télécharger dans Réglages › Analyse et paroles). « Annuler la traduction » remet le texte d'origine.

**Application réduite** (1.0.10) : près de l'horloge, trois barres (CPU, GPU, mémoire GPU) et deux nombres (en attente en violet, terminés en vert). Sous Windows 11, épinglez ces icônes si elles restent cachées derrière la flèche ^.

**Limite d'utilisation du GPU** (1.0.10, Réglages › Système) : de 30 à 100 % ; le moteur fait de courtes pauses pour laisser souffler la carte (génération un peu plus longue).

**File d'attente** : générations en file, pause/reprise (individuelle ou « tout lancer »), relance des générations interrompues, estimation du temps restant (apprise sur l'historique). Une pastille dans la zone de notification indique les morceaux prêts (rouge) et les générations en cours (violet).

**Bibliothèque** : lecture intégrée (volume, lecture continue), filtres (texte, type, durée, style, validés / à problème) et tris, édition des informations (groupe, album, piste). Chaque morceau :
- **Pouce vert** : valider ; **pouce rouge** : signaler un problème ;
- **Re-rendre** : même composition, nouvelle (ou même) graine ;
- **Recomposer** : nouvelle composition avec les mêmes style et paroles ;
- **Prolonger** : génère la suite sous forme de nouveau morceau ;
- **Reprise (cover)** : même mélodie dans un autre style ; avec le modèle d'analyse audio (Réglages › Analyse audio), le fichier est analysé (genre, ambiance, instruments, voix, tempo, tonalité, structure) et des balises de style sont proposées, avec un mode automatique « Appliquer et générer » ; les paroles chantées sont aussi transcrites (Whisper) et rangées dans la structure détectée ;
- **Langue des paroles** : pastille « Paroles : Français »… sur chaque morceau chanté (déclarée par la balise de langue, sinon détectée) ; Réglages › Transcription des paroles › « Vérifier la langue des morceaux » : Whisper écoute les morceaux de la bibliothèque et corrige la langue si elle est nettement différente ;
- **Exporter** en FLAC, MP3 ou WAV (WAV = master sans limiteur ; FLAC/MP3 avec −1 dB de marge).

**Albums** : regroupement par groupe + album, 3 images par album (pochette 1:1 intégrée aux FLAC/MP3 à l'export, image YouTube 16:9, image TikTok 9:16). Export d'un dossier `Groupe - Album\NN - Titre.ext` avec `cover.jpg`.

**Vidéos** : « Vidéo » (sur un morceau) ou « Vidéos… » (sur un album) crée **une vidéo par titre et par réseau** : `videos\YouTube\<titre>_YouTube.mp4` (1920×1080), `videos\TikTok\<titre>_TikTok.mp4` (1080×1920) et `videos\IG\<titre>_Instagram.mp4` (1080×1080), dans le dossier de l'album (ou du groupe) — image fixe + son AAC 320 kbit/s. Chaque vidéo est contrôlée (lecture complète, vraie image) avant d'être enregistrée.

**Pause automatique pendant un jeu** (Réglages) : si un programme plein écran ou un jeu de votre liste se lance, la génération se met en pause et reprend à la fermeture du jeu.

**Paroles d'une reprise** : Whisper transcrit les paroles (ses phrases inventées du type « Thank you for watching » sont écartées) ; si l'option « Chercher les paroles sur Internet » est active (Réglages › Analyse et paroles), le groupe et le titre (étiquettes ou nom du fichier) sont envoyés à lrclib.net et les paroles trouvées remplacent la transcription quand elles correspondent, avec refrains repérés automatiquement. Sans connexion : transcription Whisper gardée, sans blocage.

**Jauges et version** : sous l'en-tête, quatre jauges (mémoire GPU, débordement, utilisation CPU, utilisation GPU). L'en-tête affiche le numéro de version et une pastille « Mise à jour x.y.z » quand une version plus récente est publiée (recherche désactivée tant qu'aucun site de publication n'est choisi ; jamais bloquante hors ligne).

**Carte graphique** (Réglages › Système) : carte détectée et moteur utilisé (Vulkan ou CUDA), « Compatibilité de la carte » (Automatique : stable sur AMD et Intel, rapide sur Nvidia ; Stable FP32 ; Rapide FP16) et « Moteur de calcul » (Automatique, Vulkan, CUDA ; redémarrage nécessaire).

**Test de santé de la carte** (Réglages) : rejoue deux fois un extrait ; un écart supérieur à 15 % signale un pilote Vulkan instable. L'application avertit aussi quand la version du pilote change.

## Particularités techniques
- Sur AMD (mode stable), le moteur tourne avec `--no-fa --clamp-fp16` et les noyaux Vulkan FP32 (`GGML_VK_DISABLE_F16`, `GGML_VK_DISABLE_INTEGER_DOT_PRODUCT`, `GGML_VK_DISABLE_MMVQ`) : indispensables sur AMD RDNA2 pour éviter grésillements et saturation. Sur Nvidia (mode rapide), ces variables sont retirées ; avec le moteur CUDA, seul `--clamp-fp16` est passé. Tout est appliqué automatiquement.
- La génération prend un peu plus que la durée du morceau sur une RX 6700 XT (ex. 91 s de musique en ≈ 2 min avant le passage en FP32, ≈ 35 % de plus depuis).
- Le fonctionnement par défaut consomme ≈ 40–50 Mo de RAM hors génération.

## Dépannage
- *« Impossible de trouver le moteur de synthèse »* : `yue-synth.exe` doit être dans le dossier `moteur\` de l'application.
- *Téléchargement interrompu* : relancez l'application, il reprend où il s'était arrêté.
- *Sons grésillants* : lancez le test de santé ; mettez à jour ou rétablissez le pilote graphique ; recomposez le morceau.
- *Plus de place sur le disque* : supprimez des morceaux (bouton « Supprimer » : envoi à la corbeille de Windows, à vider ensuite) ou des modèles et LoRA inutilisés (colonne « Supprimer » des Réglages, suppression définitive). Réglages › Bibliothèque : « Supprimer tous les morceaux » (corbeille, avec confirmation).

## Licences et crédits
- Studio Audio AI : application native (C++20, Dear ImGui, yyjson).
- Moteur : [yue2.cpp](https://github.com/ServeurpersoCom/yue2.cpp) (MIT).
- Modèles : [YuE2](https://huggingface.co/m-a-p/YuE2-3B), [Serveurperso/YuE2-GGUF](https://huggingface.co/Serveurperso/YuE2-GGUF) — licence **CC-BY-NC 4.0 (usage non commercial)**, vérifiez-la avant tout usage public.
- FFmpeg (build « essentials » de gyan.dev, GPL v3) : https://ffmpeg.org — code source disponible sur le site du projet.
- Analyse audio : modèles d'étiquetage [Essentia](https://essentia.upf.edu/models.html) (Discogs-EffNet et classificateurs, Music Technology Group, Universitat Pompeu Fabra) — licence **CC BY-NC-SA 4.0 (usage non commercial)** ; exécutés avec [ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT).
- Transcription des paroles : [Whisper](https://github.com/openai/whisper) (OpenAI, MIT) via [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT).
- Détails dans le dossier `LICENSES\`.
