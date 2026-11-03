# Documentation de la 1.0.74. Usage : python docs_174.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.74 · mise à jour le 3 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.74 · updated 3 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.74)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>3 nov. 2026 (1.0.74)</td><td>Créer des vidéos › Vrai clip : case « Short 30 s » : un extrait vertical 9:16 de 30 secondes par morceau, qui commence 2 s avant le refrain, avec les images et les paroles du clip et un fondu du son (dossier « Shorts » de l'album, un fichier pour YouTube Shorts et TikTok).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>3 Nov 2026 (1.0.74)</td><td>Create videos › Real clip: “30 s Short” box: a 30-second vertical 9:16 excerpt per track, starting 2 s before the chorus, with the clip's images and lyrics and a sound fade (album “Shorts” folder, one file for YouTube Shorts and TikTok).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.73)", "(version 1.0.74)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **3 nov. 2026 (1.0.74)** : Short 30 s 9:16 centré sur le refrain."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
