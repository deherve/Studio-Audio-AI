# Documentation de la 1.0.84. Usage : python docs_184.py <music> "<entrée CLAUDE.md>"
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.84 · mise à jour le 5 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.84 · updated 5 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.84)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 nov. 2026 (1.0.84)</td><td>Réglages › Système : carte « Licences des modèles ». La commercialisation des morceaux, pochettes et vidéos est soumise aux licences des modèles utilisés : YuE2, MERT-v2 et SheetSage2 en CC BY-NC 4.0 (non commercial), Essentia en CC BY-NC-SA 4.0 (non commercial), Whisper en MIT, FLUX.2 klein, Z-Image Turbo et MOSS-Image en Apache 2.0, SDXL-Lightning en CreativeML OpenRAIL++-M, IA de texte selon le modèle (Apache 2.0, Qwen Research non commercial, Llama 3.1, Gemma), LoRA selon chacune. La fenêtre Réglages se recentre aussi quand la fenêtre du Studio change de taille.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>5 Nov 2026 (1.0.84)</td><td>Settings › System: “Model licences” card. Selling the tracks, covers and videos is subject to the licences of the models used: YuE2, MERT-v2 and SheetSage2 under CC BY-NC 4.0 (non-commercial), Essentia under CC BY-NC-SA 4.0 (non-commercial), Whisper under MIT, FLUX.2 klein, Z-Image Turbo and MOSS-Image under Apache 2.0, SDXL-Lightning under CreativeML OpenRAIL++-M, text AI depending on the model (Apache 2.0, Qwen Research non-commercial, Llama 3.1, Gemma), each LoRA under its own. The Settings window also re-centres when the Studio window changes size.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.83)", "(version 1.0.84)", 1)
t = t.replace("- « Importer depuis l'IA… » retiré", sys.argv[2] + "\n- « Importer depuis l'IA… » retiré", 1)
line = "- **5 nov. 2026 (1.0.84)** : licences des modèles dans Réglages › Système ; banc « partition » hors programme."
for nl in ("\n", "\r\n"):
    t = t.replace("## Journal des sessions" + nl, "## Journal des sessions" + nl + line + nl, 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
