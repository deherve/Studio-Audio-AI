# 1.1.013 : aide FR / EN (ligne du journal des évolutions). Usage : python -X utf8 docs_1113.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

ROW_FR = ('<tr><td>13 nov. 2026 (1.1.013)</td><td><b>Révision du code : corrections et optimisations.</b> '
          '<b>Plus léger au repos</b> : la bibliothèque n\'est plus relue toutes les 2 secondes ni les fichiers de chaque carte revérifiés toutes les 3 secondes ; '
          'le Studio attend que Windows signale un changement dans le dossier <code>Audios</code>. Mesuré avec 148 morceaux, page Bibliothèque ouverte : '
          'accès disque de ≈ 820 à ≈ 12 par seconde, processeur de 2,85 % à 0,86 % d\'un cœur, et plus d\'image saccadée toutes les 3 secondes. '
          '<b>Corrections</b> : symboles affichés « ? » (étoiles ★ des modèles d\'IA, croix ✕ des boutons d\'annulation et des pastilles, note ♪, signe −) ; '
          'barre de progression d\'un téléchargement qui restait figée pendant chaque gros fichier ; '
          'reprise à partir d\'un fichier dont le nom porte un accent ; libellés « En attente » / « En pause » restés en français dans les autres langues ; '
          'fiche d\'un morceau jamais remplacée par un fichier incomplet si le disque est plein ; '
          'une erreur imprévue dans un travail de fond (vidéos, clips, export, mastering, stems…) le termine « en échec » avec un message au lieu de pouvoir fermer le Studio. '
          'Aucun changement dans les fonctions ni dans le son des morceaux.</td></tr>\n')
ROW_EN = ('<tr><td>13 Nov 2026 (1.1.013)</td><td><b>Code review: fixes and optimisations.</b> '
          '<b>Lighter when idle</b>: the library is no longer re-read every 2 seconds, nor the files of each card re-checked every 3 seconds; '
          'the Studio waits for Windows to report a change in the <code>Audios</code> folder. Measured with 148 tracks, Library page open: '
          'disk accesses from ≈ 820 to ≈ 12 per second, processor from 2.85% to 0.86% of one core, and no more stuttering frame every 3 seconds. '
          '<b>Fixes</b>: symbols shown as “?” (★ stars of the AI models, ✕ cross of the cancel buttons and chips, ♪ note, − sign); '
          'download progress bar that stayed frozen during each large file; '
          'cover from a file whose name has an accent; “Waiting” / “Paused” labels left in French in the other languages; '
          'a track\'s record is never replaced by an incomplete file when the disk is full; '
          'an unexpected error in a background job (videos, clips, export, mastering, stems…) ends it as “failed” with a message instead of possibly closing the Studio. '
          'No change to the features or to the sound of the tracks.</td></tr>\n')


def edit(name, anchor, row):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, row.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>12 nov. 2026 (1.1.012)</td>', ROW_FR)
edit('aide.en.html', '<tr><td>12 Nov 2026 (1.1.012)</td>', ROW_EN)
print('OK')
