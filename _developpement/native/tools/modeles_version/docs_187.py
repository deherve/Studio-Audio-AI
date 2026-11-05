# Documentation de la 1.0.87. Usage : python docs_187.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.87 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.87 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.87)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.87)</td><td>Partition : le mode de composition « Complet (mélodie + accords) » devient le mode par défaut d'un nouveau morceau (≈ 28 % de temps en plus ; tonalité, tempo et accords suivis). La partition écrite par le modèle est gardée dans le dossier du morceau (score.abc). La carte détaillée affiche sa tonalité et son tempo ; Actions › « Voir la partition… » montre la tonalité, le tempo, la mesure, les accords de chaque section et le texte complet. « Sans partition (plus rapide) » et « Mélodie seule » restent au choix.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.87)</td><td>Score: the composition mode “Full (melody + chords)” becomes the default for a new track (≈ 28% more time; key, tempo and chords are followed). The score written by the model is kept in the track folder (score.abc). The detailed card shows its key and tempo; Actions › “View the score…” shows the key, tempo, time signature, the chords of each section and the full text. “No score (faster)” and “Melody only” remain available.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.86)", "(version 1.0.87)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.87)** : partition (1/4) : mode Complet par défaut, partition gardée, résumé et fenêtre « Voir la partition… »."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
