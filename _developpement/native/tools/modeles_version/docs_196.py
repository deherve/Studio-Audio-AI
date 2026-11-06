# Documentation de la 1.0.96. Usage : python docs_195.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.96 · mise à jour le 6 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.96 · updated 6 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.96)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>6 nov. 2026 (1.0.96)</td><td>Bibliothèque : bouton « Paroles » sur chaque morceau (cartes détaillée, compacte et avec pochette) : une fenêtre affiche la structure (sections entre crochets) et les paroles, texte sélectionnable et copiable (Ctrl+C, clic droit : « Copier la sélection » / « Copier tout »), avec les boutons « Copier les paroles », « Copier la structure » et « Tout copier ». Morceau instrumental : structure seule ; morceau long : paroles de toutes les parties.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>6 Nov 2026 (1.0.96)</td><td>Search: the Library search box also looks inside the lyrics and structures of tracks.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = re.sub(r"\(version 1\.0\.\d+\)", "(version 1.0.96)", t, count=1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **6 nov. 2026 (1.0.96)** : la recherche de la Bibliothèque cherche aussi dans les paroles."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
