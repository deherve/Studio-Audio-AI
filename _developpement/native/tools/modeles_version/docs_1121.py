# 1.1.019 à 1.1.021 : aide FR / EN (journal des évolutions). Usage : python -X utf8 docs_1121.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ("<tr><td>26 nov. 2026 (1.1.021)</td><td><b>Clips plus rapides, morceaux longs mieux coupés, relecture des paroles.</b> "
      "(1) <b>Montage des clips</b> : les fondus entre les plans ne passent plus par une longue chaîne de filtres ; un clip de 3 min 30 se monte en 45 s au lieu de 60 s (même image à 45 dB près, même son, même durée). "
      "(2) <b>Morceau long</b> : le texte est aussi coupé en 3 parties quand la durée estimée dépasse ce que la carte supporte (440 s environ sur une carte de 12 Go ou moins, 660 s au-delà), pour éviter les pannes de mémoire vidéo en fin de morceau. "
      "(3) Réglages › IA : case <b>Relire les paroles</b> (décochée par défaut) : après l'écriture, l'IA relit et ne réécrit que les lignes toutes faites ; les sections, le nombre de lignes et les refrains restent identiques ; environ une minute de plus par chanson avec l'IA locale, effet modeste avec une petite IA.</td></tr>\n")
EN = ("<tr><td>26 Nov 2026 (1.1.021)</td><td><b>Faster clips, better-split long tracks, lyrics proofreading.</b> "
      "(1) <b>Clip assembly</b>: the fades between shots no longer go through a long chain of filters; a 3 min 30 clip is assembled in 45 s instead of 60 s (same picture within 45 dB, same sound, same length). "
      "(2) <b>Long track</b>: the text is also split into 3 parts when the estimated length exceeds what the card can take (about 440 s on a card of 12 GB or less, 660 s above), to avoid video memory failures near the end of a track. "
      "(3) Settings › AI: <b>Proofread the lyrics</b> box (off by default): after writing, the AI reads the lyrics again and rewrites only the stock lines; sections, number of lines and choruses stay the same; about one more minute per song with the local AI, modest effect with a small AI.</td></tr>\n")


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>25 nov. 2026 (1.1.018)</td>', FR)
edit('aide.en.html', '<tr><td>25 Nov 2026 (1.1.018)</td>', EN)
print('OK')
