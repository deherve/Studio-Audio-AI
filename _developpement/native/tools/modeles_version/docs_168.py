# Documentation de la 1.0.68. Usage : python docs_168.py <music>
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ octobre 2026", "version 1.0.68 · mise à jour le 31 octobre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ October 2026", "version 1.0.68 · updated 31 October 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.68)")
sub(F, r"<tr><td>(\d+ oct\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 oct. 2026 (1.0.68)</td><td>En-tête : le texte « Génération locale · YuE2-3B · Vulkan » est retiré pour laisser plus de place aux compteurs.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Oct 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 Oct 2026 (1.0.68)</td><td>Header: the “Local generation · YuE2-3B · Vulkan” text is removed to leave more room for the dials.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.67)", "(version 1.0.68)", 1)
entry = r"- 1.0.68 : en-tête : texte « Génération locale · YuE2-3B · Vulkan/CUDA » retiré (demande de Herve 31/10), le moteur reste affiché dans Réglages › Système. Docs : `native\tools\modeles_version\docs_168.py`. Sauvegarde : `_sauvegardes\avant_1.0.68`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("## Journal des sessions\n", "## Journal des sessions\n- **31 oct. 2026 (1.0.68)** : texte du moteur retiré de l'en-tête.\n", 1).replace("## Journal des sessions\r\n", "## Journal des sessions\r\n- **31 oct. 2026 (1.0.68)** : texte du moteur retiré de l'en-tête.\r\n", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
