# Documentation de la 1.0.83. Usage : python docs_183.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.83 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.83 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.83)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.83)</td><td>Contrôle qualité sans réécoute : dans Albums…, « Contrôle qualité (bibliothèque) » et, dans le menu « … » d'un album, « Contrôle qualité de l'album ». Pour chaque morceau : durée loin de la cible, paroles peu reconnues ou fin du texte non chantée (Whisper), silence de 5 s ou plus, dernière partie d'un morceau long trop courte. Les morceaux à refaire reçoivent un pouce rouge (sauf ceux que vous avez validés) ; le détail est dans « controle_qualite.txt » (dossier de l'album, ou dossier Audios pour toute la bibliothèque).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.83)</td><td>Quality check without listening: in Albums…, “Quality check (library)” and, in an album's “…” menu, “Album quality check”. For each track: length far from the target, lyrics poorly recognised or end of the text not sung (Whisper), silence of 5 s or more, last part of a long track too short. Tracks to redo get a red thumb (except those you validated); details are in “controle_qualite.txt” (album folder, or the Audios folder for the whole library).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.82)", "(version 1.0.83)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.83)** : contrôle qualité sans réécoute (durée, paroles, silences, partie trop courte)."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
