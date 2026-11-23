# 1.1.017 : aide FR / EN (journal des évolutions). Usage : python -X utf8 docs_1117.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ('<tr><td>22 nov. 2026 (1.1.017)</td><td><b>Vidéos et clips : reprise, pause, erreurs lisibles.</b> '
      '(1) <b>Reprendre</b> : la case « Reprendre : ne pas refaire les vidéos déjà créées » (cochée par défaut, fenêtre « Créer des vidéos ») saute les vidéos, clips et Shorts dont le fichier existe déjà ; '
      'après une interruption, seul ce qui manque est refait (avant, tous les montages étaient refaits). Sans effet si « Refaire le scénario et les images » est coché. '
      '(2) <b>Pause</b> : un bouton « Pause » / « Reprendre » sur chaque travail de vidéos ou de clips. ffmpeg et Whisper sont suspendus tout de suite (le processeur est rendu) ; '
      'le modèle d\'images est déchargé pour <b>rendre la carte graphique</b>, à la fin de l\'image en cours (≈ 1 min au plus). À la reprise, la création repart là où elle s\'était arrêtée. '
      '(3) <b>Erreurs de ffmpeg</b> : quand ffmpeg échoue, sa raison est maintenant affichée sous le message d\'échec et écrite dans le journal (avant, elle était perdue). '
      'Essais : une pause de 30 s en plein montage (ffmpeg figé, clip terminé ensuite et contrôlé), reprise sur un titre déjà fait (rien relancé), échec voulu (raison lue dans le journal).</td></tr>\n')
EN = ('<tr><td>22 Nov 2026 (1.1.017)</td><td><b>Videos and clips: resume, pause, readable errors.</b> '
      '(1) <b>Resume</b>: the “Resume: do not redo the videos already created” box (ticked by default, “Create videos” window) skips the videos, clips and Shorts whose file already exists; '
      'after an interruption, only what is missing is made (before, all the montages were redone). No effect if “Redo the scenario and the images” is ticked. '
      '(2) <b>Pause</b>: a “Pause” / “Resume” button on each video or clip job. ffmpeg and Whisper are suspended at once (the processor is released); '
      'the image model is unloaded to <b>release the graphics card</b>, at the end of the current image (about 1 min at most). On resume, the creation restarts where it stopped. '
      '(3) <b>ffmpeg errors</b>: when ffmpeg fails, its reason is now shown under the failure message and written to the journal (before, it was lost). '
      'Tests: a 30 s pause in the middle of a montage (ffmpeg frozen, clip finished and checked afterwards), resume on an already made title (nothing launched), forced failure (reason read in the journal).</td></tr>\n')


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>13 nov. 2026 (1.1.016)</td>', FR)
edit('aide.en.html', '<tr><td>13 Nov 2026 (1.1.016)</td>', EN)
print('OK')
