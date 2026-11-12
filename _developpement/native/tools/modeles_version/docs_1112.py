# 1.1.012 : aide FR / EN (FLUX.3 klein). Usage : python -X utf8 docs_1112.py
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'

ROW_FR = ('<tr><td>12 nov. 2026 (1.1.012)</td><td><b>FLUX.3 klein</b> remplace FLUX.2 klein 4B parmi les modèles d\'images locaux '
          '(pochettes et clips ; même choix « klein » dans les Réglages, nouveau téléchargement de 6,3 Go : modèle 3,5 Go + encodeur de texte Qwen3-VL 2,5 Go + VAE 0,25 Go). '
          'Mesuré sur une Radeon RX 6700 XT 12 Go : ≈ 40 s par image 768 × 768 (FLUX.2 klein : ≈ 14 s), rendu plus soigné. '
          '<b>Licence</b> : FLUX Kommunity License (Black Forest Labs), pas Apache 2.0 : usage commercial des images seulement si vos revenus annuels sont inférieurs à 5 M$, avec contrôle du contenu et mention « généré par IA » si la loi l\'exige (tableau des licences dans Réglages › Système). '
          'Le moteur d\'images stable-diffusion.cpp (<code>outils\\sd-vulkan</code>) est mis à jour (version du 10/11/2026), nécessaire pour FLUX.3 ; Z-Image, MOSS-Image et SDXL-Lightning vérifiés avec ce moteur. '
          'L\'ancien dossier <code>modeles\\images\\flux2-klein</code> n\'est plus utilisé : vous pouvez le supprimer.</td></tr>\n')
ROW_EN = ('<tr><td>12 Nov 2026 (1.1.012)</td><td><b>FLUX.3 klein</b> replaces FLUX.2 klein 4B among the local image models '
          '(covers and clips; same “klein” choice in Settings, new 6.3 GB download: 3.5 GB model + 2.5 GB Qwen3-VL text encoder + 0.25 GB VAE). '
          'Measured on a Radeon RX 6700 XT 12 GB: ≈ 40 s per 768 × 768 image (FLUX.2 klein: ≈ 14 s), finer rendering. '
          '<b>Licence</b>: FLUX Kommunity License (Black Forest Labs), not Apache 2.0: commercial use of the images only if your annual revenue is below US$5M, with content checking and an “AI-generated” notice where the law requires it (licence table in Settings › System). '
          'The stable-diffusion.cpp image engine (<code>outils\\sd-vulkan</code>) is updated (10 Nov 2026 version), required for FLUX.3; Z-Image, MOSS-Image and SDXL-Lightning were checked with this engine. '
          'The old <code>modeles\\images\\flux2-klein</code> folder is no longer used: you can delete it.</td></tr>\n')


def edit(name, anchor, row):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(anchor) == 1, (name, anchor)
    s = s.replace(anchor, row.replace('\n', nl) + anchor)
    lines = s.split(nl)
    n = 0
    for i, l in enumerate(lines):
        if '<tr><td>' not in l and 'FLUX.2 klein' in l:  # description actuelle (les lignes du journal restent d'époque)
            lines[i] = l.replace('FLUX.2 klein', 'FLUX.3 klein')
            n += 1
    open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))
    print(name, n, 'ligne(s) mises à jour')


edit('aide.html', '<tr><td>11 nov. 2026 (1.1.011)</td>', ROW_FR)
edit('aide.en.html', '<tr><td>11 Nov 2026 (1.1.011)</td>', ROW_EN)
