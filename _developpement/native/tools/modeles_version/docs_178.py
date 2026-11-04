# Documentation de la 1.0.78. Usage : python docs_178.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.78 · mise à jour le 4 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.78 · updated 4 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.78)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>4 nov. 2026 (1.0.78)</td><td>Albums… : bouton « Pochettes » quand une image manque (crée seulement les manquantes) ; Créer des vidéos : bouton « Générer » sous une image absente ; Composer avec l'IA : bouton « Vider les champs » ; bibliothèque : 4e logo pour les Shorts ; le nombre de morceaux ne chevauche plus « Lire les nouveaux » (il passe dans la bulle du titre si la place manque).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>4 Nov 2026 (1.0.78)</td><td>Albums…: “Covers” button when an image is missing (creates only the missing ones); Create videos: “Generate” button under a missing image; Compose with AI: “Clear fields” button; library: 4th logo for Shorts; the track count no longer overlaps “Play new” (it moves to the title tooltip when space is short).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.77)", "(version 1.0.78)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **4 nov. 2026 (1.0.78)** : pochettes manquantes, Vider les champs, logo des Shorts, en-tête de la bibliothèque."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
