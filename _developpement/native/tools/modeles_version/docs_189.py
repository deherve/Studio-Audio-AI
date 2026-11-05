# Documentation de la 1.0.89. Usage : python docs_189.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.89 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.89 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.89)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.89)</td><td>Partition reconnue : dans l'onglet « Partition », « Reconnaître la partition d'un fichier audio… » fait écouter un morceau par SheetSage2 (≈ 1 min pour 3 min de musique) ; sa partition (tonalité, tempo, sections, accords, mélodie) arrive dans l'onglet, où on la transpose ou la retouche, puis on génère avec ses propres paroles et son style : comme une reprise, mais la partition est visible et modifiable. Aussi : « Ouvrir une partition (.abc)… », et Actions › « Reconnaître sa partition… » pour un morceau de la bibliothèque qui n'en a pas.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.89)</td><td>Recognised score: in the “Score” tab, “Recognise the score of an audio file…” has SheetSage2 listen to a track (≈ 1 min for 3 min of music); its score (key, tempo, sections, chords, melody) comes into the tab, where you transpose or edit it, then generate with your own lyrics and style: like a cover, but the score is visible and editable. Also: “Open a score (.abc)…”, and Actions › “Recognise its score…” for a library track that has none.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.88)", "(version 1.0.89)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.89)** : partition reconnue (SheetSage2) dans l'onglet Partition, fichier .abc, essai suite + partition."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
