# 1.1.018 : aide FR / EN (journal des évolutions). Usage : python -X utf8 docs_1118.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

FR = ("<tr><td>25 nov. 2026 (1.1.018)</td><td><b>Paroles plus naturelles.</b> Les demandes de paroles envoyées à l'IA (Composer avec l'IA, album IA, suite d'un morceau long) ajoutent des règles d'écriture naturelle, "
      "inspirées des skills <i>humanizer</i> et <i>humanizer-fr</i> (MIT) : images concrètes, pas de « ce n'est pas X, c'est Y », pas de maximes ni de listes de trois mots creux, pas de mots ni d'images typiques de l'IA, "
      "pas de calques de l'anglais dans la langue choisie ; pour le français, des règles en plus (pas d'anglicismes comme « adresser » ou « au final », pas de connecteurs de rapport, pas de formules vides). "
      "Case <b>Paroles plus naturelles</b> dans Réglages › IA (cochée par défaut). Les règles sont des consignes données à l'IA : elles réduisent les tournures « IA » sans les supprimer, "
      "et une IA locale petite en suit moins qu'une grosse. Crédits dans <code>LICENSES\\humanizer-MIT.txt</code>.</td></tr>\n")
EN = ("<tr><td>25 Nov 2026 (1.1.018)</td><td><b>More natural lyrics.</b> The lyrics requests sent to the AI (Compose with AI, AI album, continuation of a long track) now add natural-writing rules, "
      "inspired by the <i>humanizer</i> and <i>humanizer-fr</i> skills (MIT): concrete images, no “it's not X, it's Y”, no sayings or lists of three hollow words, no typical AI words or images, "
      "no calques from English in the chosen language; for French, extra rules (no anglicisms such as “adresser” or “au final”, no report-style connectors, no empty phrases). "
      "<b>More natural lyrics</b> box in Settings › AI (ticked by default). The rules are instructions given to the AI: they reduce “AI” phrasing without removing it, "
      "and a small local AI follows fewer of them than a large one. Credits in <code>LICENSES\\humanizer-MIT.txt</code>.</td></tr>\n")


def edit(name, anchor, rows):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, rows.replace('\n', nl) + anchor)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('aide.html', '<tr><td>22 nov. 2026 (1.1.017)</td>', FR)
edit('aide.en.html', '<tr><td>22 Nov 2026 (1.1.017)</td>', EN)
print('OK')
