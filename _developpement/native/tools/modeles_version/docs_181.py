# Documentation de la 1.0.81. Usage : python docs_181.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.81 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.81 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.81)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.81)</td><td>Sauvegarde automatique, une fois par jour au lancement, dans le dossier « sauvegardes » du programme : réglages, styles, mots à éviter, liste des modèles et des LoRA, fiche de chaque morceau (15 jours gardés). Au démarrage, un modèle ou une LoRA disparu depuis la dernière sauvegarde, ou une LoRA de vos styles absente, est signalé dans une fenêtre et dans Réglages › Système.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.81)</td><td>Automatic backup, once a day at startup, in the program's “sauvegardes” folder: settings, styles, words to avoid, list of models and LoRA, each track's sheet (15 days kept). At startup, a model or LoRA gone since the last backup, or a LoRA missing from your styles, is reported in a window and in Settings › System.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.80)", "(version 1.0.81)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.81)** : sauvegarde automatique quotidienne et vérification au démarrage ; audit du dépôt public et test d'installation neuve."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
