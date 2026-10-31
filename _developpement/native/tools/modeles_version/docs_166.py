# Documentation de la 1.0.66. Usage : python docs_166.py <music>
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ octobre 2026", "version 1.0.66 · mise à jour le 31 octobre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ October 2026", "version 1.0.66 · updated 31 October 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.66)")
sub(F, r"<tr><td>(\d+ oct\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 oct. 2026 (1.0.66)</td><td>L'en-tête de la fenêtre affiche le nouveau nom, Studio Audio AI.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Oct 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 Oct 2026 (1.0.66)</td><td>The window header now shows the new name, Studio Audio AI.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.65)", "(version 1.0.66)", 1)
entry = r"- 1.0.66 : en-tête de la fenêtre (`frame`, main.cpp) : « Studio Audio AI » au lieu de « Studio YuE2 », oublié en 1.0.40, signalé par Herve 31/10. Docs : `docs_166.py`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("- **31 oct. 2026 (1.0.65)** :", "- **31 oct. 2026 (1.0.66)** : nouveau nom dans l'en-tête de la fenêtre.\n- **31 oct. 2026 (1.0.65)** :", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
