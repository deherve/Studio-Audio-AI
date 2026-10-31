# Documentation de la 1.0.69 : aide FR/EN relue, nouvelles captures. Usage : python docs_169.py <music>
import re, sys
from pathlib import Path

app = Path(sys.argv[1])
dev = app / "_developpement"
F, E = app / "ressources/aide/aide.html", app / "ressources/aide/aide.en.html"


def fig(lang, name, alt, cap):
    d = "img/en/" if lang == "en" else "img/"
    return f'<figure><img src="{d}{name}.jpg" alt="{alt}"><figcaption>{cap}</figcaption></figure>'


def edit(p, lang, lines, after, images, drop):
    L = p.read_text(encoding="utf-8").split("\n")
    def find(prefix):
        hit = [i for i, s in enumerate(L) if s.startswith(prefix)]
        if len(hit) != 1:
            sys.exit(f"{p.name} : {len(hit)} lignes pour : {prefix}")
        return hit[0]
    for prefix in drop:
        del L[find(prefix)]
    for prefix, new in lines.items():
        L[find(prefix)] = new
    for prefix, block in after.items():
        L[find(prefix) + 1:find(prefix) + 1] = block
    t = "\n".join(L)
    for old, (name) in images.items():
        n = t.count(f'src="img/{old}.jpg"')
        if n == 0 and old in ("albums", "videos"):  # déjà remplacées par la nouvelle figure
            continue
        if n != 1:
            sys.exit(f"{p.name} : image {old} : {n}")
        t = t.replace(f'src="img/{old}.jpg"', f'src="img/{"en/" if lang == "en" else ""}{name}.jpg"')
    p.write_text(t, encoding="utf-8", newline="")


IMAGES = {"principal": "principal", "style_pastilles": "style_pastilles", "style_constructeur": "style_constructeur", "reglages_3": "reglages_lora",
          "cartes_compactes": "cartes_compactes", "cartes_pochette": "cartes_pochette", "cartes_pochette_compacte": "cartes_pochette_compacte",
          "modifier": "modifier", "albums": "albums", "videos": "videos", "reglages_1": "reglages_interface", "theme_studio_gris": "theme_gris",
          "theme_papier_encre": "theme_papier", "theme_menthe": "theme_menthe", "reglages_4": "reglages_systeme2"}

FR = dict(
    lines={
        "<li>Toutes les jauges, comme": "<li>Les compteurs, comme toutes les barres de progression de l'application, suivent un <b>dégradé du vert au rouge vif</b> (jaune puis orange entre les deux). Sur le compteur GPU, un <b>repère orange</b> marque la limite réglée dans Réglages › Système ; avec une limite, le compteur montre la moyenne des 16 dernières secondes.</li>",
        "<p>« Albums… » ouvre la liste": "<p>« Albums… » ouvre la liste des albums : des morceaux terminés ayant le même <b>groupe</b> et le même <b>titre d'album</b> (renseignés à l'import ou dans « Modifier »). Chaque album tient sur une ligne : vignette, nombre de morceaux, voyants verts des images et des vidéos déjà créées, boutons <b>Exporter</b>, <b>Vidéos…</b>, <b>Compléter…</b> et menu <b>…</b>. La flèche déplie sa fiche, en deux onglets : <b>Pochettes</b> et <b>Vidéos</b>.</p>",
        '<figure><img src="img/albums.jpg"': fig("fr", "albums", "Fenêtre Albums", "Une fiche d'album dépliée, onglet Pochettes : les trois images et la description modifiable.") + "\n" + fig("fr", "albums_videos", "Onglet Vidéos de la fiche d'album", "L'onglet Vidéos : nombre de vidéos par plateforme et accès au dossier."),
        "<li><b>Trois images par album</b>": "<li><b>Trois images par album</b> : <i>Pochette 1:1</i> (intégrée aux fichiers exportés et affichée dans les cartes), <i>YouTube 16:9</i> et <i>TikTok 9:16</i> (servent aux vidéos). Un clic sur une image l'affiche en grand.</li>",
        "<li><b>Images créées par l'IA</b>": "<li><b>Images créées par l'IA</b> : boutons <b>Générer</b> ou <b>Régénérer ▾</b> sous chaque image et <b>Toutes les pochettes ▾</b>, à partir de la <b>description</b> modifiable (en anglais, sans texte dans l'image). Le menu propose le réglage par défaut ou un modèle local précis : FLUX.2 klein, Z-Image Turbo, MOSS-Image ou SDXL-Lightning, avec le temps estimé.</li>",
        "<li><b>Exporter…</b> — choisissez un dossier": "<li><b>Exporter</b> : les fichiers sont écrits dans le sous-dossier <code>Audios</code> de l'album, sous la forme <code>NN - Titre.ext</code>, avec les étiquettes (titre, artiste, album, piste n/total) et la pochette intégrée aux FLAC et MP3, plus <code>cover.jpg</code>.</li>\n<li><b>Compléter…</b> : ouvre « Composer avec l'IA » pour ajouter des morceaux à cet album, avec le même groupe, le même son et les pistes à la suite.</li>\n<li><b>Menu …</b> : <b>Renommer…</b> (groupe ou titre de l'album ; étiquettes, dossiers et images suivent), <b>Ouvrir le dossier de l'album</b>, <b>Rédiger les descriptions (IA locale)</b> (un fichier <code>description.txt</code> pour le groupe, l'album et chaque morceau, dans la langue de l'interface, sans jamais écraser un fichier existant) et <b>Supprimer l'album…</b> (envoi à la corbeille, avec confirmation).</li>",
        "<li><b>Vidéos…</b> — crée une vidéo": "<li><b>Vidéos…</b> : crée une vidéo fixe ou un clip par titre de l'album (voir ci-dessous).</li>",
        "<h2>12. Vidéos YouTube et TikTok</h2>": "<h2>12. Vidéos et clips</h2>",
        '<figure><img src="img/videos.jpg"': fig("fr", "videos", "Fenêtre Créer des vidéos", "La fenêtre « Créer des vidéos » : une image par réseau, et les options du vrai clip."),
        "<li>Le dossier de destination par défaut": "<li>Les vidéos sont rangées dans le dossier <code>videos</code> de l'album. Les images choisies sont mémorisées pour l'album.</li>",
        "<li>Chaque vidéo est une <b>image fixe</b>": "<li><b>Vidéo fixe</b> (case « Vrai clip » décochée) : une image avec le son du morceau (AAC 320 kbit/s), créée <b>individuellement par titre</b>, une par réseau coché.</li>\n<li><b>Vrai clip</b> : l'IA locale écrit un scénario d'après les paroles et les fiches du morceau, le modèle d'images local crée une image par scène, puis le montage ajoute des mouvements de caméra lents, des fondus et les paroles synchronisées en sous-titres (refrains plus grands et orangés). Un instrumental n'a pas de sous-titres. Fichiers <code>&lt;titre&gt;_&lt;Réseau&gt;_Clip.mp4</code> dans les mêmes dossiers.</li>\n<li><b>Type de rendu</b> (réaliste, BD, comics, manga, anime, aquarelle…) et <b>moteur d'images</b> : FLUX.2 klein, Z-Image Turbo, MOSS-Image ou SDXL-Lightning, à installer dans Réglages › IA › Pochettes.</li>\n<li><b>Nombre d'images</b> : Recommandé (selon les paroles et la durée), Plus, Moins ou Nombre fixe (jusqu'à 60). La fenêtre affiche le détail par morceau et une estimation du temps.</li>\n<li><b>Refaire le scénario et les images</b> : sinon ceux déjà créés sont réutilisés (dossier <code>clip_scenes</code> du morceau) : seul le montage est refait, c'est bien plus rapide. Les paroles se corrigent dans la fiche <code>paroles.txt</code> du morceau.</li>\n<li><b>Texte de début et de fin</b> : affichés sur la carte d'ouverture et pendant les 8 dernières secondes.</li>\n<li>À lancer <b>au repos</b> : les IA de texte et d'images utilisent la carte graphique.</li>",
        '<tr><td><code>outils\\llama-vulkan\\</code></td>': '<tr><td><code>outils\\llama-vulkan\\</code></td><td>llama.cpp pour la carte graphique, téléchargé depuis l\'onglet <b>Composer avec l\'IA</b>.</td></tr>\n<tr><td><code>outils\\sd-vulkan\\</code></td><td>stable-diffusion.cpp : création des pochettes et des images de clips.</td></tr>',
        "<p>Les versions précédentes rangeaient ces éléments": "<p>Les versions précédentes rangeaient ces éléments ailleurs (<code>voix\\</code>, <code>traduction\\</code>, <code>models\\</code>, <code>bibliotheque\\</code>…) : au premier lancement d'une version récente, ils sont déplacés automatiquement au bon endroit, sans rien écraser (journal : <code>cache\\migration.log</code>).</p>",
    },
    after={
        '<h3 id="jauges">': [fig("fr", "compact", "Compteurs en style Compact", "Style Compact."), fig("fr", "sport", "Compteurs en style Sport", "Style Sport.")],
        "<p>Le formulaire « Nouveau morceau » se compose": [fig("fr", "formulaire", "Formulaire Nouveau morceau", "Le formulaire, avec les blocs Morceau et Style dépliés.")],
        '<h3 id="composer-ia">': [fig("fr", "composer", "Onglet Composer avec l'IA", "L'onglet « Composer avec l'IA ».")],
        "<h3>Modèle de génération</h3>": [fig("fr", "reglages_generation", "Réglages, catégorie Génération", "Réglages › Génération : modèle, fin des morceaux, styles et packs.")],
        '<h3 id="analyse-reglage">': [fig("fr", "reglages_analyse", "Réglages, catégorie Analyse et paroles", "Réglages › Analyse et paroles.")],
        '<h3 id="ia-reglage">': [fig("fr", "reglages_ia", "Réglages, catégorie IA", "Réglages › IA : moteur, mots à éviter, albums et pochettes."), fig("fr", "reglages_ia2", "Modèles d'images et IA locale", "Plus bas : les modèles d'images et les modèles de l'IA locale.")],
        '<h3 id="sante">': [fig("fr", "reglages_systeme", "Réglages, catégorie Système", "Réglages › Système : jeux, recommandations, carte graphique.")],
        '<h3 id="cartes">': [fig("fr", "cartes_detail", "Cartes détaillées", "Affichage « Détaillées ». En haut de chaque carte, les logos YouTube, TikTok et Instagram : gris sans vidéo, couleur atténuée pour une vidéo fixe, pleine couleur pour un clip.")],
    },
    images=IMAGES,
    drop=['<figure><img src="img/preregles.jpg"', '<figure><img src="img/formulaire_bas.jpg"', '<figure><img src="img/marques_commutateur.jpg"'],
)
EN = dict(
    lines={
        "<li>All gauges, like every progress bar": "<li>The dials, like every progress bar in the application, follow a <b>green to bright red gradient</b> (yellow then orange in between). On the GPU dial, an <b>orange mark</b> shows the limit set in Settings › System; with a limit, the dial shows the average of the last 16 seconds.</li>",
        "<p>“Albums…” opens the list": "<p>“Albums…” opens the list of albums: finished tracks with the same <b>band</b> and the same <b>album title</b> (filled in at import or in “Edit”). Each album fits on one line: thumbnail, number of tracks, green lights for the images and videos already created, <b>Export</b>, <b>Videos…</b>, <b>Complete…</b> buttons and a <b>…</b> menu. The arrow unfolds its sheet, with two tabs: <b>Covers</b> and <b>Videos</b>.</p>",
        '<figure><img src="img/albums.jpg"': fig("en", "albums", "Albums window", "An unfolded album sheet, Covers tab: the three images and the editable description.") + "\n" + fig("en", "albums_videos", "Videos tab of the album sheet", "The Videos tab: number of videos per platform and access to the folder."),
        "<li><b>Three images per album</b>": "<li><b>Three images per album</b>: <i>Cover 1:1</i> (embedded in exported files and displayed in cards), <i>YouTube 16:9</i> and <i>TikTok 9:16</i> (used for videos). Clicking an image shows it full size.</li>",
        "<li><b>AI-made images</b>": "<li><b>AI-made images</b>: <b>Generate</b> or <b>Regenerate ▾</b> buttons under each image and <b>All covers ▾</b>, from the editable <b>description</b> (in English, no text in the image). The menu offers the default setting or a specific local model: FLUX.2 klein, Z-Image Turbo, MOSS-Image or SDXL-Lightning, with the estimated time.</li>",
        "<li><b>Export…</b> — choose a destination folder": "<li><b>Export</b>: files are written to the album's <code>Audios</code> subfolder as <code>NN - Title.ext</code>, with tags (title, artist, album, track n/total) and the cover embedded in FLAC and MP3, plus <code>cover.jpg</code>.</li>\n<li><b>Complete…</b>: opens “Compose with AI” to add tracks to this album, with the same band, the same sound and track numbers following on.</li>\n<li><b>… menu</b>: <b>Rename…</b> (band or album title; tags, folders and images follow), <b>Open the album folder</b>, <b>Write the descriptions (local AI)</b> (a <code>description.txt</code> file for the band, the album and each track, in the interface language, never overwriting an existing file) and <b>Delete the album…</b> (sent to the Recycle Bin, with confirmation).</li>",
        "<li><b>Videos…</b> — creates one video": "<li><b>Videos…</b>: creates a still video or a clip per title of the album (see below).</li>",
        "<h2>12. YouTube and TikTok videos</h2>": "<h2>12. Videos and clips</h2>",
        '<figure><img src="img/videos.jpg"': fig("en", "videos", "Create videos window", "The “Create videos” window: one image per network, and the true clip options."),
        "<li>The default destination folder": "<li>Videos are stored in the album's <code>videos</code> folder. The chosen images are remembered for the album.</li>",
        "<li>Each video is a <b>still image</b>": "<li><b>Still video</b> (“True clip” unticked): an image with the track's sound (AAC 320 kbit/s), created <b>individually per title</b>, one per ticked network.</li>\n<li><b>True clip</b>: the local AI writes a story from the lyrics and the track's sheets, the local image model creates one image per scene, then the editing adds slow camera moves, fades and the lyrics synchronized as subtitles (choruses larger and orange). An instrumental has no subtitles. Files <code>&lt;title&gt;_&lt;Network&gt;_Clip.mp4</code> in the same folders.</li>\n<li><b>Render style</b> (realistic, comic book, manga, anime, watercolor…) and <b>image engine</b>: FLUX.2 klein, Z-Image Turbo, MOSS-Image or SDXL-Lightning, installed from Settings › AI › Covers.</li>\n<li><b>Number of images</b>: Recommended (from the lyrics and duration), More, Fewer or Fixed number (up to 60). The window shows the detail per track and a time estimate.</li>\n<li><b>Redo the story and the images</b>: otherwise those already made are reused (the track's <code>clip_scenes</code> folder): only the editing is redone, which is much faster. Lyrics can be corrected in the track's <code>paroles.txt</code> file.</li>\n<li><b>Start and end text</b>: shown on the opening card and during the last 8 seconds.</li>\n<li>Run it <b>when idle</b>: the text and image AIs use the graphics card.</li>",
        '<tr><td><code>outils\\llama-vulkan\\</code></td>': '<tr><td><code>outils\\llama-vulkan\\</code></td><td>llama.cpp for the graphics card, downloaded from the <b>Compose with AI</b> tab.</td></tr>\n<tr><td><code>outils\\sd-vulkan\\</code></td><td>stable-diffusion.cpp: creates the covers and clip images.</td></tr>',
        "<p>Previous versions kept these items": "<p>Previous versions kept these items elsewhere (<code>voix\\</code>, <code>traduction\\</code>, <code>models\\</code>, <code>bibliotheque\\</code>…): on the first launch of a recent version, they are moved automatically to the right place, without overwriting anything (log: <code>cache\\migration.log</code>).</p>",
    },
    after={
        '<h3 id="jauges">': [fig("en", "compact", "Dials in Compact style", "Compact style."), fig("en", "sport", "Dials in Sport style", "Sport style.")],
        "<h2>4. Creating a track</h2>": [fig("en", "formulaire", "New track form", "The form, with the Track and Style blocks unfolded.")],
        '<h3 id="composer-ia">': [fig("en", "composer", "Compose with AI tab", "The “Compose with AI” tab.")],
        "<h3>Generation model</h3>": [fig("en", "reglages_generation", "Settings, Generation category", "Settings › Generation: model, end of tracks, styles and packs.")],
        '<h3 id="analyse-reglage">': [fig("en", "reglages_analyse", "Settings, Analysis and lyrics category", "Settings › Analysis and lyrics.")],
        '<h3 id="ia-reglage">': [fig("en", "reglages_ia", "Settings, AI category", "Settings › AI: engine, words to avoid, albums and covers."), fig("en", "reglages_ia2", "Image models and local AI", "Further down: the image models and the local AI models.")],
        '<h3 id="sante">': [fig("en", "reglages_systeme", "Settings, System category", "Settings › System: games, recommendations, graphics card.")],
        '<h3 id="cartes">': [fig("en", "cartes_detail", "Detailed cards", "“Detailed” layout. At the top of each card, the YouTube, TikTok and Instagram logos: grey without a video, dimmed colour for a still video, full colour for a clip.")],
    },
    images=IMAGES,
    drop=['<figure><img src="img/preregles.jpg"', '<figure><img src="img/formulaire_bas.jpg"', '<figure><img src="img/marques_commutateur.jpg"'],
)
edit(E, "en", **EN)


def sub(p, pat, new):
    t = p.read_text(encoding="utf-8")
    t, n = re.subn(pat, new, t, count=1)
    if n != 1:
        sys.exit(f"{p} : motif absent : {pat}")
    p.write_text(t, encoding="utf-8", newline="")


sub(F, r"\bsix catégories\b", "sept catégories")
sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ octobre 2026", "version 1.0.69 · mise à jour le 31 octobre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ October 2026", "version 1.0.69 · updated 31 October 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.69)")
sub(F, r"<tr><td>(\d+ oct\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 oct. 2026 (1.0.69)</td><td>Aide relue et mise à jour (albums, clips, compteurs, dossiers), 26 nouvelles captures d'écran en français et en anglais.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Oct 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 Oct 2026 (1.0.69)</td><td>Help reviewed and updated (albums, clips, dials, folders), 26 new screenshots in French and English.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.68)", "(version 1.0.69)", 1)
entry = r"- 1.0.69 : **aide relue, captures FR et EN** (demande de Herve 31/10) : `ressources\aide\img\` (français) et `img\en\` (anglais), 26 images chacune. Essais : `STUDIO_TEST_LANG=<code>` et `STUDIO_TEST_SHOT=\"clé=valeur;…\"` (filter, cards, marks, style, gauge, theme, reglages, scroll, ia, open, fold, reuse, edit) ; avec SHOT, les balises de plusieurs mots à majuscule sont masquées (`test_clean_style`) : pas de noms de personnes réelles dans les images livrées. Captures faites sur l'album Chronicles of the Cosmic Dawn, recadrées sans LoRA personnelles ni chemins du PC. Les 8 autres langues ouvrent toujours l'aide anglaise. Docs : `native\tools\modeles_version\docs_169.py`. Sauvegarde : `_sauvegardes\avant_1.0.69`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("## Journal des sessions\n", "## Journal des sessions\n- **31 oct. 2026 (1.0.69)** : aide relue, nouvelles captures FR et EN.\n", 1).replace("## Journal des sessions\r\n", "## Journal des sessions\r\n- **31 oct. 2026 (1.0.69)** : aide relue, nouvelles captures FR et EN.\r\n", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
