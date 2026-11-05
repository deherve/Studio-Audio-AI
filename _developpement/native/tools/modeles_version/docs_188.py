# Documentation de la 1.0.88. Usage : python docs_188.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.88 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.88 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.88)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.88)</td><td>Onglet « Partition » (à côté de « Nouveau morceau » et « Composer avec l'IA ») : « Préparer la partition » fait écrire au modèle la partition seule (≈ 1 min) d'après le titre, le style et les paroles de « Nouveau morceau » ; on la vérifie (tonalité, tempo, mesure, accords par section), on la transpose (± 1 demi-ton, notes et accords), on change son tempo (le « BPM » du style suit) ou son texte, puis « Générer avec cette partition ». Actions › « Utiliser cette partition… » reprend un morceau et sa partition pour le rejouer, le transposer ou le refaire dans un autre style.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.88)</td><td>“Score” tab (next to “New track” and “Compose with AI”): “Prepare the score” has the model write the score alone (≈ 1 min) from the title, style and lyrics of “New track”; you check it (key, tempo, time signature, chords per section), transpose it (± 1 semitone, notes and chords), change its tempo (the style's “BPM” follows) or its text, then “Generate with this score”. Actions › “Use this score…” takes a track and its score to replay it, transpose it or redo it in another style.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.87)", "(version 1.0.88)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.88)** : partition (2/4) : onglet Partition (préparer, transposer, tempo, générer avec la partition)."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
