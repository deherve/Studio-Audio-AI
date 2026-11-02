# Documentation de la 1.0.71. Usage : python docs_171.py <music>
import re, sys
from pathlib import Path

app = Path(sys.argv[1])
dev = app / "_developpement"
F, E = app / "ressources/aide/aide.html", app / "ressources/aide/aide.en.html"


def sub(p, pat, new):
    t = p.read_text(encoding="utf-8")
    t, n = re.subn(pat, new, t, count=1)
    if n != 1:
        sys.exit(f"{p} : motif absent : {pat}")
    p.write_text(t, encoding="utf-8", newline="")


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.71 · mise à jour le 2 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.71 · updated 2 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.71)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>2 nov. 2026 (1.0.71)</td><td>Morceau « Très longue » : la suite des paroles est plus fiable (plus de 110 lignes à chaque essai).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>2 Nov 2026 (1.0.71)</td><td>“Very long” track: the lyrics continuation is more reliable (over 110 lines in every test).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.70)", "(version 1.0.71)", 1)
entry = r"- 1.0.71 : `ai_lengthen_song` : la suite est demandée en texte simple (avec JSON, Mistral rendait souvent `lyrics` en objet illisible : 30 → 62 lignes au dernier essai de la 1.0.70) ; mesuré sur 4 essais : 32-60 lignes → 118-140. Durée réelle à ce nombre de lignes non mesurée. Docs : `docs_171.py`. Sauvegarde : `_sauvegardes\avant_1.0.71`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("## Journal des sessions\n", "## Journal des sessions\n- **2 nov. 2026 (1.0.71)** : suite des paroles fiabilisée.\n", 1).replace("## Journal des sessions\r\n", "## Journal des sessions\r\n- **2 nov. 2026 (1.0.71)** : suite des paroles fiabilisée.\r\n", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
