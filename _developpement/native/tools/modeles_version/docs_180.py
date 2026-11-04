# Documentation de la 1.0.80. Usage : python docs_180.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.80 · mise à jour le 4 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.80 · updated 4 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.80)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>4 nov. 2026 (1.0.80)</td><td>Albums… : bouton « Refaire les pochettes » au bout de la ligne « Images » quand les 3 images existent (pochette 1:1, YouTube 16:9, TikTok 9:16 recréées après confirmation « Tout remplacer »).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>4 Nov 2026 (1.0.80)</td><td>Albums…: “Redo covers” button at the end of the “Images” line when all 3 images exist (1:1 cover, YouTube 16:9 and TikTok 9:16 recreated after a “Replace all” confirmation).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.79)", "(version 1.0.80)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **4 nov. 2026 (1.0.80)** : bouton Refaire les pochettes dans Albums…."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
