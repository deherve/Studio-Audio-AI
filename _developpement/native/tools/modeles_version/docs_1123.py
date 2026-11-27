# 1.1.022 : aide FR / EN (journal des évolutions). Usage : python -X utf8 docs_1121.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ("<tr><td>26 nov. 2026 (1.1.023)</td><td><b>Iris-3B et Qwen-Image nettement plus rapides quand la carte est partagée.</b> "
      "Ces modèles gardent 8 à 8,5 Go dans la mémoire vidéo ; dès que Windows ou un navigateur en prennent aussi (environ 2 Go), la carte de 12 Go déborde et chaque image devient 3 fois plus lente (Iris : 48 s au lieu de 14 s en 768 × 768, 74 s en 1344 × 768 pour un clip). "
      "L'encodeur de texte passe maintenant sur le processeur (2 à 3 s de plus par image) : Iris 14 s en 768 × 768 et 24 s en 1344 × 768, Qwen-Image Turbo 79 s au lieu de 51 s.</td></tr>\n")
EN = ("<tr><td>26 Nov 2026 (1.1.023)</td><td><b>Iris-3B and Qwen-Image much faster when the card is shared.</b> "
      "These models keep 8 to 8.5 GB in video memory; as soon as Windows or a browser also take some (about 2 GB), the 12 GB card overflows and every image becomes 3 times slower (Iris: 48 s instead of 14 s at 768 × 768, 74 s at 1344 × 768 for a clip). "
      "The text encoder now runs on the processor (2 to 3 s more per image): Iris 14 s at 768 × 768 and 24 s at 1344 × 768, Qwen-Image Turbo 51 s instead of 79 s.</td></tr>\n")


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>26 nov. 2026 (1.1.022)</td>', FR)
edit('aide.en.html', '<tr><td>26 Nov 2026 (1.1.022)</td>', EN)
print('OK')
