# Documentation de la 1.0.90. Usage : python docs_190.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.90 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.90 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.90)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.90)</td><td>Morceaux longs cohérents : quand la partie 1 a une partition, les parties 2 et 3 gardent sa tonalité et son tempo. La partition de chaque suite est préparée d'après ses paroles, transposée dans la tonalité de la partie précédente et réglée sur son tempo, puis le moteur continue le morceau en la suivant (≈ 20 à 60 s de plus par partie). Essai sur 3 parties : la même tonalité (Ré dièse mineur) entendue dans chaque partie, tempo 126 à 128.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.90)</td><td>Consistent long tracks: when part 1 has a score, parts 2 and 3 keep its key and tempo. Each continuation's score is prepared from its lyrics, transposed to the previous part's key and set to its tempo, then the engine continues the track following it (≈ 20 to 60 s more per part). Test on 3 parts: the same key (D♯ minor) heard in every part, tempo 126 to 128.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.89)", "(version 1.0.90)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.90)** : partition (3/4) : morceaux longs, même tonalité et même tempo dans chaque partie."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
