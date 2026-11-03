# Documentation de la 1.0.73. Usage : python docs_173.py <music>
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.73 · mise à jour le 3 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.73 · updated 3 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.73)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>3 nov. 2026 (1.0.73)</td><td>Composer avec l'IA : paroles plus variées. Une ligne de couplet ou de pont recopiée (dans le morceau ou dans un morceau précédent de l'album) est retirée et réécrite ; les refrains repris gardent leurs deux premières lignes et changent les autres.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>3 Nov 2026 (1.0.73)</td><td>Compose with AI: more varied lyrics. A verse or bridge line copied from earlier in the song, or from an earlier song of the album, is removed and rewritten; returning choruses keep their first two lines and change the others.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.72)", "(version 1.0.73)", 1)
entry = sys.argv[2] if len(sys.argv) > 2 else ""
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **3 nov. 2026 (1.0.73)** : paroles plus variées (lignes recopiées retirées, refrains variés, phrases de l'album évitées)."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
