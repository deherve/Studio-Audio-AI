# Documentation de la 1.0.91. Usage : python docs_191.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.91 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.91 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.91)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.91)</td><td>Atelier partition : « Voir en notes… » (fenêtre « Voir la partition… » et onglet « Partition ») ouvre une fenêtre du Studio qui affiche la partition en vraies notes (bibliothèque abcjs, dans le moteur WebView2 fourni avec Windows) : voix chantée et instrument (clé de fa automatique pour l'instrument grave), accords, titre, tonalité, tempo ; écoute au piano sans internet avec la note jouée en couleur, zoom, impression et export PDF au format A4.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.91)</td><td>Score workshop: “View as notes…” (in the “View the score…” window and the “Score” tab) opens a Studio window showing the score as real notes (abcjs library, in the WebView2 engine included with Windows): sung voice and instrument (automatic bass clef for a low instrument), chords, title, key, tempo; offline piano playback with the playing note highlighted, zoom, printing and A4 PDF export.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.90)", "(version 1.0.91)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.91)** : Atelier partition (étape 1) : notes, écoute, impression, PDF."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
