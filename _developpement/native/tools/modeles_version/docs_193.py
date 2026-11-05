# Documentation de la 1.0.93. Usage : python docs_193.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.93 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.93 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.93)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.93)</td><td>Atelier partition, import : « Importer… » ouvre un fichier MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) ou ABC ; la partition est mise au format du moteur (voix chantée et instrument ; MIDI : rythmes arrondis à la double croche, tonalité et accords estimés) puis « Utiliser dans le Studio » la place dans l’onglet « Partition ». Onglet « Partition » : « Importer MusicXML ou MIDI (Atelier partition)… ». Essai : « L’Hymne à la joie » importé en MIDI, chanson générée avec, 97 % des notes de la mélodie retrouvées dans le chant (Ré majeur, 99 BPM).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.93)</td><td>Score workshop, import: “Import…” opens a MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) or ABC file; the score is converted to the engine’s format (sung voice and instrument; MIDI: rhythms rounded to sixteenth notes, key and chords estimated) and “Use in the Studio” places it in the “Score” tab. “Score” tab: “Import MusicXML or MIDI (Score workshop)…”. Test: “Ode to Joy” imported as MIDI, song generated with it, 97% of the melody notes found in the singing (D major, 99 BPM).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.92)", "(version 1.0.93)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.93)** : Atelier partition (étape 3) : import MusicXML, MIDI, ABC vers l’onglet Partition."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
