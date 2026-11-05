# Documentation de la 1.0.82. Usage : python docs_182.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.82 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.82 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.82)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.82)</td><td>Shorts en série : dans Albums…, le bouton « Shorts manquants (bibliothèque)… » et, dans le menu « … » d'un album, « Shorts manquants de l'album… » créent un Short pour chaque morceau terminé qui n'en a pas encore, dans la même file que les clips. Chaque Short est rangé dans « videos\\Shorts » de son album ; les images d'un clip déjà fait sont reprises ; une fenêtre indique d'abord le nombre de morceaux concernés.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.82)</td><td>Shorts in series: in Albums…, the “Missing Shorts (library)…” button and, in an album's “…” menu, “Missing Shorts of the album…” create a Short for every finished track that has none yet, in the same queue as the clips. Each Short is saved in its album's “videos\\Shorts”; images of an existing clip are reused; a window first shows how many tracks are concerned.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.81)", "(version 1.0.82)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.82)** : Shorts en série (un album ou toute la bibliothèque)."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
