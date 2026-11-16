# 1.1.015 et 1.1.016 : aide FR / EN. Usage : python -X utf8 docs_1116.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ('<tr><td>13 nov. 2026 (1.1.016)</td><td><b>Image de référence d\'un groupe.</b> Dans « Albums », menu « … » d\'un album : <b>Image de référence du groupe…</b> copie une image '
      '(pochette, portrait du chanteur…) dans le dossier du groupe (<code>reference_visuelle.png</code>). Avec le modèle <b>FLUX.3 klein</b>, les prochaines pochettes et les images des clips '
      'de ce groupe s\'en inspirent pour garder le même style et les mêmes sujets (essai : palette, style et lettrage repris ; un même visage n\'est pas garanti). Plus lent : ≈ 90 s par image au lieu de ≈ 40 s, '
      'et les clips créent alors leurs images une par une (sans le serveur d\'images). <b>Retirer l\'image de référence</b> l\'envoie à la Corbeille. Les autres modèles d\'images ignorent la référence.</td></tr>\n'
      '<tr><td>13 nov. 2026 (1.1.015)</td><td><b>Prévision de la file.</b> Sous le bouton « Générer », <i>Temps estimé : environ … </i> (par morceau, ou pour toutes les versions) ; '
      'dans la file de génération, <i>Fin estimée : … (vers HH:MM)</i> pour le morceau en cours et ceux en attente (hors pause). L\'estimation vient des temps déjà mesurés sur ce PC : sur 147 morceaux, '
      'écart moyen −1 % (donc juste pour la file entière), ≈ 26 % d\'écart par morceau. Un avertissement apparaît pour un morceau de plus de 4 minutes sur une carte de 12 Go ou moins '
      '(la mémoire vidéo peut manquer vers la fin ; le nouvel essai automatique de la 1.1.009 s\'en charge).</td></tr>\n')
EN = ('<tr><td>13 Nov 2026 (1.1.016)</td><td><b>Band reference image.</b> In “Albums”, “…” menu of an album: <b>Band reference image…</b> copies an image '
      '(cover, portrait of the singer…) into the band\'s folder (<code>reference_visuelle.png</code>). With the <b>FLUX.3 klein</b> model, the next covers and clip images '
      'of this band draw on it to keep the same style and subjects (test: palette, style and lettering kept; the same face is not guaranteed). Slower: ≈ 90 s per image instead of ≈ 40 s, '
      'and clips then create their images one by one (without the image server). <b>Remove the reference image</b> sends it to the Recycle Bin. Other image models ignore the reference.</td></tr>\n'
      '<tr><td>13 Nov 2026 (1.1.015)</td><td><b>Queue forecast.</b> Under the “Generate” button, <i>Estimated time: about …</i> (per track, or for all versions); '
      'in the generation queue, <i>Estimated end: … (around HH:MM)</i> for the running track and the waiting ones (paused excluded). The estimate comes from the times already measured on this PC: over 147 tracks, '
      'average error −1% (so right for the whole queue), ≈ 26% error per track. A warning appears for a track longer than 4 minutes on a card with 12 GB or less '
      '(video memory can run out near the end; the automatic retry of 1.1.009 handles it).</td></tr>\n')


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>13 nov. 2026 (1.1.014)</td>', FR)
edit('aide.en.html', '<tr><td>13 Nov 2026 (1.1.014)</td>', EN)
print('OK')
