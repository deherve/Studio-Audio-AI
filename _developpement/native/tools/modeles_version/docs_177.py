# Documentation de la 1.0.77. Usage : python docs_177.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.77 · mise à jour le 3 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.77 · updated 3 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.77)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>3 nov. 2026 (1.0.77)</td><td>Mises à jour : à chaque lancement, le Studio regarde la dernière version publiée sur GitHub ; si elle est plus récente, une pastille « Mettre à jour (x.y.z) » apparaît à côté du numéro de version et ouvre sa page de téléchargement. Hors connexion, rien ne s'affiche.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>3 Nov 2026 (1.0.77)</td><td>Updates: at each start, the Studio checks the latest version published on GitHub; if it is newer, an “Update (x.y.z)” badge appears next to the version number and opens its download page. Offline, nothing is shown.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.76)", "(version 1.0.77)", 1)
t = t.replace("https://github.com/deherve/Studio-Audio-AI (privé), accord de Herve 31/10", "https://github.com/deherve/Studio-Audio-AI (**public** depuis le 3/11, confirmé par Herve : la recherche de mise à jour en dépend), accord de Herve 31/10", 1)
t = t.replace("`UPDATE_MANIFEST_URL` vide dans `version.hpp` : **à décider avec Herve** (site de publication : GitHub releases ou fichier JSON public).", "`UPDATE_MANIFEST_URL` : releases GitHub depuis la 1.0.77.", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **3 nov. 2026 (1.0.77)** : pastille « Mettre à jour » branchée sur les releases GitHub."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
