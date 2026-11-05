# Documentation de la 1.0.92. Usage : python docs_192.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.92 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.92 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.92)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.92)</td><td>Atelier partition, exports : boutons MIDI…, MusicXML… et ABC… (le Studio demande où enregistrer). Le MusicXML garde notes, durées, mesures, silences, liaisons, triolets et symboles d’accords ; il s’ouvre dans MuseScore, Finale, Sibelius ou Dorico. Contrôle aller-retour ABC → MusicXML → ABC sur 10 partitions : 100 % des notes et des accords identiques.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.92)</td><td>Score workshop, exports: MIDI…, MusicXML… and ABC… buttons (the Studio asks where to save). MusicXML keeps notes, durations, measures, rests, ties, triplets and chord symbols; it opens in MuseScore, Finale, Sibelius or Dorico. Round-trip check ABC → MusicXML → ABC on 10 scores: 100% identical notes and chords.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.91)", "(version 1.0.92)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.92)** : Atelier partition (étape 2) : exports MIDI, MusicXML, ABC, aller-retour vérifié."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
