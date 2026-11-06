# Documentation de la 1.0.97. Usage : python docs_197.py <music> "<entrée CLAUDE.md>"
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


FR = ("Outils audio : <b>Actions ▾</b> d'un morceau (et menu « … » d'un album) : <b>Masteriser</b> (volume normalisé −14 / −11 / −16 LUFS, crête limitée, fichiers dans le dossier « master »), "
      "<b>Paroles synchronisées</b> (paroles.lrc et paroles.srt calés par Whisper), <b>Séparer les pistes</b> (voix, batterie, basse, autres : demande le moteur demucs.cpp et son modèle), "
      "<b>Exporter pour un logiciel de musique</b> (audio, partition ABC, paroles, stems, tempo et tonalité dans le nom), <b>Fiche de publication</b> (titre, description et hashtags par l'IA locale, boutons YouTube Studio / TikTok), "
      "<b>Livret PDF</b> de l'album. Albums… : <b>Doublons…</b> (compositions ou paroles presque identiques). Plusieurs versions d'un même envoi : la <b>meilleure</b> est désignée à la fin. "
      "File de génération : « Quand la file est terminée » (rien, veille, extinction après 60 s) et rapport « rapport_nuit.txt ». Nouveau : le champ de recherche cherche aussi dans les paroles.")
EN = ("Audio tools: a track's <b>Actions ▾</b> (and an album's “…” menu): <b>Master</b> (normalised loudness −14 / −11 / −16 LUFS, limited peak, files in the “master” folder), "
      "<b>Synchronised lyrics</b> (paroles.lrc and paroles.srt aligned by Whisper), <b>Separate stems</b> (vocals, drums, bass, other: needs the demucs.cpp engine and its model), "
      "<b>Export for music software</b> (audio, ABC score, lyrics, stems, tempo and key in the name), <b>Publication sheet</b> (title, description and hashtags by the local AI, YouTube Studio / TikTok buttons), "
      "album <b>PDF booklet</b>. Albums…: <b>Duplicates…</b> (almost identical compositions or lyrics). Several versions of one submission: the <b>best</b> one is named at the end. "
      "Generation queue: “When the queue is finished” (nothing, sleep, shutdown after 60 s) and a “rapport_nuit.txt” report. New: the search box also looks inside lyrics.")
sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.97 · mise à jour le 6 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.97 · updated 6 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.97)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>6 nov. 2026 (1.0.97)</td><td>" + FR + "</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>6 Nov 2026 (1.0.97)</td><td>" + EN + "</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = re.sub(r"\(version 1\.0\.\d+\)", "(version 1.0.97)", t, count=1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **6 nov. 2026 (1.0.97)** : outils audio (mastering, .lrc/.srt, livret PDF, export logiciel, fiche de publication, doublons, meilleur essai, session de nuit, stems en attente du moteur)."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
