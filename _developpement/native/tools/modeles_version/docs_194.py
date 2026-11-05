# Documentation de la 1.0.94. Usage : python docs_193.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.94 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.94 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.94)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.94)</td><td>Atelier partition, édition : cliquez sur une note, puis ▲ ▼ (flèches du clavier ; Maj + flèche = octave), ♯ ♭ ♮, durée de la ronde à la double croche, note pointée, silence, dupliquer, supprimer, accord au-dessus de la note ; flèches gauche/droite pour passer d’une note à l’autre ; Annuler / Rétablir (Ctrl+Z / Ctrl+Y) ; mesures de durée incorrecte signalées ; « Paroles… » place les paroles sous la mélodie chantée, une syllabe par note (aussi dans l’export MusicXML) ; « Texte ABC » pour modifier la partition en texte avec redessin immédiat.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.94)</td><td>Score workshop, editing: click a note, then ▲ ▼ (keyboard arrows; Shift + arrow = octave), ♯ ♭ ♮, durations from whole to sixteenth note, dotted note, rest, duplicate, delete, chord above the note; left/right arrows move from note to note; Undo / Redo (Ctrl+Z / Ctrl+Y); measures with a wrong length are flagged; “Lyrics…” places the lyrics under the sung melody, one syllable per note (also in the MusicXML export); “ABC text” edits the score as text, redrawn at once.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.93)", "(version 1.0.94)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.94)** : Atelier partition (étape 4) : édition des notes, durées, accords, paroles, texte ABC, annuler/rétablir."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
