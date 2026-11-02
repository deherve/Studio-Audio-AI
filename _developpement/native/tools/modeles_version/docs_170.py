# Documentation de la 1.0.70. Usage : python docs_170.py <music>
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


sub(F, r"version 1\.0\.\d+ · mise à jour le \d+ (octobre|novembre) 2026", "version 1.0.70 · mise à jour le 2 novembre 2026")
sub(E, r"version 1\.0\.\d+ · updated \d+ (October|November) 2026", "version 1.0.70 · updated 2 November 2026")
sub(dev / "native/distribution_fichiers/LISEZMOI.md", r"# Studio Audio AI \(1\.0\.\d+\)", "# Studio Audio AI (1.0.70)")
sub(F, r"« Dont instrumentaux » : 0 laisse l'IA répartir", "« Dont instrumentaux » : 0 = aucun instrumental")
sub(E, r"“Of which instrumentals”: 0 lets the AI decide", "“Of which instrumentals”: 0 = no instrumental")
sub(F, r"<tr><td>(\d+ oct\. 2026 \(1\.0\.\d+)", lambda m: "<tr><td>2 nov. 2026 (1.0.70)</td><td>Album mélangé : 0 instrumental veut maintenant dire aucun. Morceau « Très longue » : 3 parties, paroles complétées par l'IA jusqu'à environ 110 lignes, et une partie conclue trop tôt par le moteur est refaite automatiquement.</td></tr>\n<tr><td>" + m.group(1))
sub(E, r"<tr><td>(\d+ Oct 2026 \(1\.0\.\d+)", lambda m: "<tr><td>2 Nov 2026 (1.0.70)</td><td>Mixed album: 0 instrumentals now means none. “Very long” track: 3 parts, lyrics extended by the AI up to about 110 lines, and a part ended too early by the engine is redone automatically.</td></tr>\n<tr><td>" + m.group(1))
c = app / "CLAUDE.md"
t = c.read_text(encoding="utf-8")
t = t.replace("(version 1.0.69)", "(version 1.0.70)", 1)
entry = r"- 1.0.70 : demandes de Herve 2/11. (1) Album mélangé : 0 instrumental = aucun (`ai_album_user_prompt`, `ai_parse_album_plan` ; plus de répartition par l'IA). (2) **Morceaux très longs** : cause mesurée sur 8 morceaux : l'IA écrit 40 à 60 lignes au lieu de 110-130, une ligne ≈ 6,5 s, et le moteur conclut parfois juste après la reprise (partie 2 de 34 s). `ai_lengthen_song` (ai_writer : l'IA écrit la suite, 6 demandes au plus, `AI_LONG_LINES` = 110, insérée avant [Outro], morceau seul et album ; mesuré avec 90 : 48-70 lignes → 84-96) ; 3 parties si ≥ 60 lignes ou ≥ 18 sections (`LONG_PART_MARK` sépare les parties 2 et 3 dans `long_rest`, `split_long_lyrics(paroles, parties)`, partie 2 titrée « (partie 2) » et sans clip) ; `launch_long_part` en chaîne ; `source_start` calculé sur `partie2_seule.wav` (les codes d'une partie 2 ne couvrent pas le morceau assemblé) ; partie retentée 2 fois si elle ajoute moins de 40 % de l'attendu (`long_retry`, non enregistré) ; `with_full_lyrics` remonte toute la chaîne. Essai réel : 94 lignes, 3 parties : 200 + 192 + 138 s, 481 s (8 min 01) assemblés en 632 s de calcul, d'où la cible de 110 lignes ; 9 min non mesurées, écoute à faire par Herve. Essais : `--lengthen-test sortie.txt`, `--long-test sortie.txt [paroles.txt]`. Traductions : `fill_170.py` (à lancer avant i18n_sync) ; docs : `docs_170.py`. Sauvegarde : `_sauvegardes\avant_1.0.70`."
t = t.replace("- « Importer depuis l'IA… » retiré", entry + "\n- « Importer depuis l'IA… » retiré", 1)
t = t.replace("## Journal des sessions\n", "## Journal des sessions\n- **2 nov. 2026 (1.0.70)** : 0 instrumental = aucun ; morceaux très longs en 3 parties.\n", 1).replace("## Journal des sessions\r\n", "## Journal des sessions\r\n- **2 nov. 2026 (1.0.70)** : 0 instrumental = aucun ; morceaux très longs en 3 parties.\r\n", 1)
c.write_text(t, encoding="utf-8", newline="")
print("OK")
