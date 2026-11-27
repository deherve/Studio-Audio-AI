# 1.1.022 : aide FR / EN (journal des évolutions). Usage : python -X utf8 docs_1121.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ("<tr><td>26 nov. 2026 (1.1.022)</td><td><b>Trois nouveaux modèles d'images locaux</b> (Réglages › IA › Pochettes, aussi pour les clips) : "
      "<b>Iris-3B Turbo</b> (Apache 2.0, ≈ 14 s par image 768 × 768, texte bien écrit dans l'image), <b>Qwen-Image 2.1 Turbo</b> (≈ 54 s, très bon texte, licence Qwen : usage commercial à vérifier) et <b>Qwen-Image 2.1</b> (≈ 158 s, version complète). "
      "Chacun a son téléchargement et sa description ; ils tiennent dans les 12 Go de la carte (8 à 8,5 Go) : si d'autres programmes prennent de la mémoire vidéo, la génération peut devenir plusieurs fois plus lente. "
      "Essayés sans succès : Mage-Flow (format int8 non lu par le moteur), SANA-Sprint et Lumina-Image 2.0 (non gérés par le moteur).</td></tr>\n")
EN = ("<tr><td>26 Nov 2026 (1.1.022)</td><td><b>Three new local image models</b> (Settings › AI › Covers, also used for clips): "
      "<b>Iris-3B Turbo</b> (Apache 2.0, about 14 s per 768 × 768 image, text well written in the image), <b>Qwen-Image 2.1 Turbo</b> (about 54 s, very good text, Qwen licence: check commercial use) and <b>Qwen-Image 2.1</b> (about 158 s, full version). "
      "Each has its own download and description; they fit in the 12 GB of the card (8 to 8.5 GB): if other programs take video memory, generation can become several times slower. "
      "Tried without success: Mage-Flow (int8 format not read by the engine), SANA-Sprint and Lumina-Image 2.0 (not supported by the engine).</td></tr>\n")


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>26 nov. 2026 (1.1.021)</td>', FR)
edit('aide.en.html', '<tr><td>26 Nov 2026 (1.1.021)</td>', EN)
print('OK')
