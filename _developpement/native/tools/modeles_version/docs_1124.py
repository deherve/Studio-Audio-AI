# 1.1.022 : aide FR / EN (journal des évolutions). Usage : python -X utf8 docs_1121.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ("<tr><td>26 nov. 2026 (1.1.024)</td><td><b>Images de clip en 1024 × 576 (rapide).</b> "
      "Nouvelle case dans « Créer des vidéos » (cochée par défaut) : les images de scène sont créées en 1024 × 576 (16:9) puis agrandies par le montage, au lieu de 1344 × 768. Avec FLUX.3 klein : 35 s par image au lieu de 74 s ; un titre complet (scénario, 10 images, clip YouTube, TikTok et Short) passe de 15 min 9 s à 8 min 34 s, soit environ 1 h 25 au lieu de 2 h 30 pour un album de 10 titres. "
      "Un agrandissement par IA (Real-ESRGAN) a été essayé : 14 s de plus par image pour un résultat pas meilleur, il n'est pas utilisé. Décochez la case pour garder l'ancienne taille.</td></tr>\n")
EN = ("<tr><td>26 Nov 2026 (1.1.024)</td><td><b>Clip images at 1024 × 576 (fast).</b> "
      "New box in “Create videos” (ticked by default): scene images are created at 1024 × 576 (16:9) and enlarged by the editing, instead of 1344 × 768. With FLUX.3 klein: 35 s per image instead of 74 s; a whole title (script, 10 images, YouTube clip, TikTok and Short) goes from 15 min 9 s to 8 min 34 s, about 1 h 25 instead of 2 h 30 for a 10-title album. "
      "An AI upscaler (Real-ESRGAN) was tried: 14 s more per image for a result that was not better, so it is not used. Untick the box to keep the old size.</td></tr>\n")


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>26 nov. 2026 (1.1.023)</td>', FR)
edit('aide.en.html', '<tr><td>26 Nov 2026 (1.1.023)</td>', EN)
print('OK')
