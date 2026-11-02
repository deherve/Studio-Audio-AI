# Documentation de la 1.0.72. Usage : python docs_172.py <music>
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.72 · mise à jour le 2 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.72 · updated 2 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.72)")
sub(F, r"<tr><td>(\d+ nov\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>2 nov. 2026 (1.0.72)</td><td>Z-Image Turbo bien plus rapide : environ 33 s par image en 1344 × 768 dans un clip, avec un rendu presque identique (nouvelle version du modèle de 3,4 Go à télécharger si vous l'utilisiez).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Nov 2026 \(1\.0\.\d+)", lambda m: "<tr><td>2 Nov 2026 (1.0.72)</td><td>Much faster Z-Image Turbo: about 33 s per 1344 × 768 image in a clip, with a nearly identical look (a new 3.4 GB model file must be downloaded if you used it).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.71)", "(version 1.0.72)", 1)
entry = r"- 1.0.72 : **Z-Image accéléré** (demande de Herve 2/11) : Q4_0 au lieu de Q4_K, 5 étapes au lieu de 8, `--vae-tiling` (`image_weights`) et `--offload-to-cpu` (`image_offload`), absents jusque-là. Banc 1344 × 768 : Q4_K 8 étapes 60 s, Q4_0 5 étapes 39 s, Q4_0 4 étapes 34 s ; Q3_K, Q5_0, Q6_K plus lents ; cache-mode ≤ 10 % ; sans offload 176 s ; flash-attn 287 s ; sans tuiles 159 s de décodage. Clip réel, modèle gardé chargé : 33 s par image (6 images), moteur installé master-1024. Banc 5 modèles : SDXL 14-17 s, klein 22-26 s, Qwen 2.1 Turbo Q5 178-190 s, écarté (lent, licence non commerciale) ; GLM-Image sans GGUF. Dans la catalogue, `seconds` = 22 (768 × 768, × 1,75 pour les clips). Non publiée sur GitHub : **Herve veut d'abord tester** (ne pas pousser ni créer de release avant son accord). Docs : `docs_172.py`. Sauvegarde : `_sauvegardes\avant_1.0.72`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("## Journal des sessions\n", "## Journal des sessions\n- **2 nov. 2026 (1.0.72)** : Z-Image accéléré, 33 s par image dans un clip.\n", 1).replace("## Journal des sessions\r\n", "## Journal des sessions\r\n- **2 nov. 2026 (1.0.72)** : Z-Image accéléré, 33 s par image dans un clip.\r\n", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
