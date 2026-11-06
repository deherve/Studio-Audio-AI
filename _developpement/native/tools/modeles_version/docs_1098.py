# -*- coding: utf-8 -*-
# Aide FR + EN de la 1.0.98 (atelier clip : texte de fin, graine commune, fiches texte). Usage : python docs_1098.py <ressources\aide>
import sys, pathlib

AIDE = pathlib.Path(sys.argv[1])


def patch(name, pairs):
    p = AIDE / name
    t = p.read_bytes().decode("utf-8")
    nl = "\r\n" if "\r\n" in t else "\n"
    for old, new in pairs:
        o, n = old.replace("\n", nl), new.replace("\n", nl)
        assert t.count(o) == 1, (name, old[:80], t.count(o))
        t = t.replace(o, n)
    p.write_bytes(t.encode("utf-8"))


patch("aide.html", [
    ("<li><b>Texte de début et de fin</b> : affichés sur la carte d'ouverture et pendant les 8 dernières secondes.</li>\n",
     "<li><b>Texte de début et de fin</b> : affichés sur la carte d'ouverture et pendant les 8 dernières secondes. Le texte de fin se place <b>au centre</b> ou <b>en bas</b> (à la place des paroles, pour ne pas cacher le personnage), avec un <b>fond sombre</b> facultatif (bandeau noir semi-transparent, lisible sur une image claire).</li>\n"
     "<li><b>Même graine pour toutes les images d'un clip</b> : chaque scène garde sa description, mais le tirage de départ du modèle d'images est commun : visage, couleurs et cadrage se ressemblent davantage d'une image à l'autre. C'est une aide, pas une garantie. Elle ne touche que les nouvelles images : cochez aussi « Refaire » pour un clip déjà fait.</li>\n"
     "<li><b>Fiches texte</b> (<code>paroles.txt</code>, <code>fiche.txt</code>, <code>fiche_album.txt</code>, <code>fiche_groupe.txt</code>) : le scénario du clip s'en sert. Pour les anciens morceaux, <b>Actions ▾ › Écrire les fiches texte</b> (ou menu « … » d'un album) les crée tout de suite ; un <code>paroles.txt</code> déjà présent n'est jamais remplacé.</li>\n"),
    ("<tr><td>6 nov. 2026 (1.0.97)</td>",
     "<tr><td>6 nov. 2026 (1.0.98)</td><td>Atelier clip : texte de fin <b>au centre ou en bas</b>, avec <b>fond sombre</b> facultatif ; case <b>Même graine pour toutes les images d'un clip</b> (personnage et décor plus réguliers, sans garantie) ; <b>Écrire les fiches texte</b> dans Actions ▾ d'un morceau et le menu « … » d'un album, pour les morceaux faits avant les fiches.</td></tr>\n"
     "<tr><td>6 nov. 2026 (1.0.97)</td>"),
])
patch("aide.en.html", [
    ("<li><b>Start and end text</b>: shown on the opening card and during the last 8 seconds.</li>\n",
     "<li><b>Start and end text</b>: shown on the opening card and during the last 8 seconds. The end text goes <b>in the centre</b> or <b>at the bottom</b> (where the lyrics go, so it does not hide the character), with an optional <b>dark background</b> (semi-transparent black band, readable on a bright image).</li>\n"
     "<li><b>Same seed for all images of a video</b>: each scene keeps its own description, but the image model's starting draw is shared: face, colours and framing look more alike from one image to the next. It helps, it does not guarantee. It only affects new images: also tick “Redo” for a video already made.</li>\n"
     "<li><b>Text sheets</b> (<code>paroles.txt</code>, <code>fiche.txt</code>, <code>fiche_album.txt</code>, <code>fiche_groupe.txt</code>): the video's story uses them. For older tracks, <b>Actions ▾ › Write the text sheets</b> (or an album's “…” menu) creates them at once; an existing <code>paroles.txt</code> is never replaced.</li>\n"),
    ("<tr><td>6 Nov 2026 (1.0.97)</td>",
     "<tr><td>6 Nov 2026 (1.0.98)</td><td>Music video workshop: end text <b>in the centre or at the bottom</b>, with an optional <b>dark background</b>; <b>Same seed for all images of a video</b> box (more consistent character and setting, not guaranteed); <b>Write the text sheets</b> in a track's Actions ▾ and an album's “…” menu, for tracks made before the sheets existed.</td></tr>\n"
     "<tr><td>6 Nov 2026 (1.0.97)</td>"),
])
print("aide 1.0.98 OK")
