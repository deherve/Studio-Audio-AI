# Documentation de la 1.0.67. Usage : python docs_167.py <music>
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ octobre 2026", "version 1.0.67 · mise à jour le 31 octobre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ October 2026", "version 1.0.67 · updated 31 October 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.67)")
sub(F, r"Les quatre jauges : mémoire vidéo", "Les quatre compteurs : mémoire vidéo")
sub(E, r"The four gauges: video memory", "The four dials: video memory")
sub(F, r"Sous la barre du haut, quatre jauges segmentées, relues toutes les 2 secondes\.", "Dans l'en-tête, entre le solveur et le bouton Aide, quatre compteurs à aiguille, relus toutes les 2 secondes. Réglages › Système propose deux styles : <b>Sport</b> (grands cadrans) ou <b>Compact</b> (plus de place pour la bibliothèque). Au survol, une bulle donne le détail.")
sub(E, r"Under the top bar, four segmented gauges, refreshed every 2 seconds\.", "In the header, between the solver and the Help button, four needle dials, refreshed every 2 seconds. Settings › System offers two styles: <b>Sport</b> (large dials) or <b>Compact</b> (more room for the library). Hovering shows the details.")
sub(F, r"(tous programmes confondus )\(« 9,7 Go / 12,0 Go · 81 % »\)", r"\1(« 9,7 Go »)")
sub(E, r"(all programs combined )\(“9\.7 GB / 12\.0 GB · 81 %”\)", r"\1(“9.7 GB”)")
sub(F, r"La barre est pleine à 2 Go", "Le compteur est plein à 2 Go")
sub(E, r"The bar is full at 2 GB", "The dial is full at 2 GB")
sub(F, r"<tr><td>(\d+ oct\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 oct. 2026 (1.0.67)</td><td>Compteurs à aiguille dans l'en-tête à la place des quatre barres, en style Sport ou Compact au choix (Réglages › Système).</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Oct 2026 \(1\.0\.\d+)", lambda m: "<tr><td>31 Oct 2026 (1.0.67)</td><td>Needle dials in the header instead of the four bars, in Sport or Compact style (Settings › System).</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.66)", "(version 1.0.67)", 1)
entry = r"- 1.0.67 : **compteurs de voiture dans l'en-tête** (demande de Herve 31/10, maquettes A Sport et D Compact retenues) : `draw_dial`, `draw_gauges` (remplacent `draw_segment_bar`, `draw_gpu_bars`), entre le solveur et Aide ; réglage `gauge_style` (0 Sport, 1 Compact par défaut), Réglages › Système ; aiguille lissée (`gauge_anim`), repère orange de limite GPU, moyenne 16 s conservée ; texte caché si la fenêtre est étroite, toujours en bulle. Essai : `STUDIO_TEST_GAUGE=0|1`. Captures des 2 styles vérifiées ; boutons radio des Réglages non vus. Traductions : `fill_167.py` (7 textes) ; docs : `docs_167.py`. Sauvegarde : `_sauvegardes\avant_1.0.67`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("## Journal des sessions\n", "## Journal des sessions\n- **31 oct. 2026 (1.0.67)** : compteurs Sport ou Compact dans l'en-tête.\n", 1).replace("## Journal des sessions\r\n", "## Journal des sessions\r\n- **31 oct. 2026 (1.0.67)** : compteurs Sport ou Compact dans l'en-tête.\r\n", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
