# 1.1.014 : aide FR / EN (ligne du journal des évolutions). Usage : python -X utf8 docs_1114.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

ROW_FR = ('<tr><td>13 nov. 2026 (1.1.014)</td><td><b>Découpage du code, sans changement pour vous.</b> Le fichier principal du programme (16 900 lignes) est rangé en un fichier d\'en-tête commun '
          '(<code>app.hpp</code>), neuf fichiers par sujet (<code>app_form.cpp</code>, <code>app_ai.cpp</code>, <code>app_score.cpp</code>…) et un <code>main.cpp</code> réduit au démarrage. '
          'Les corrections et ajouts futurs seront plus rapides et plus sûrs. Vérifié : même temps par image, mêmes essais automatiques, mêmes écrans.</td></tr>\n')
ROW_EN = ('<tr><td>13 Nov 2026 (1.1.014)</td><td><b>Code split, no change for you.</b> The main program file (16,900 lines) is now organised into one shared header '
          '(<code>app.hpp</code>), nine files by topic (<code>app_form.cpp</code>, <code>app_ai.cpp</code>, <code>app_score.cpp</code>…) and a <code>main.cpp</code> reduced to startup. '
          'Future fixes and additions will be faster and safer. Checked: same time per frame, same automated tests, same screens.</td></tr>\n')


def edit(name, anchor, row):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, row.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>13 nov. 2026 (1.1.013)</td>', ROW_FR)
edit('aide.en.html', '<tr><td>13 Nov 2026 (1.1.013)</td>', ROW_EN)
print('OK')
