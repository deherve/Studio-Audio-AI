# 1.1.011 : aide FR / EN (ligne du journal des évolutions + descriptions). Usage : python -X utf8 docs_1111.py
import re
AIDE = r'C:\Users\deher\Downloads\dev\music\ressources\aide'


def edit(name, pairs):
    p = AIDE + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        b = b.replace('\n', nl)
        assert s.count(a) == 1, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


FR = [
 ('<tr><td>11 nov. 2026 (1.1.010)</td>',
  '<tr><td>11 nov. 2026 (1.1.011)</td><td>Quatre ajouts en une version. <b>Style du groupe</b> : case « Sauvegarder le style de ce groupe » sous « Groupe ou chanteur » (Nouveau morceau, et Composer avec l\'IA en mode album) : chaque morceau lancé enregistre son style dans vos styles sous le nom du groupe, s\'il n\'existe pas déjà ; dans « Albums », bouton « Style perso… » sur chaque album : crée un style personnalisé au nom du groupe avec les balises communes à ses morceaux. <b>Journal de débogage</b> : le fichier <code>journal\\journal.log</code> (à côté du Studio) note le démarrage, les clics, les morceaux et travaux lancés ou terminés, les messages et les erreurs avec les dernières actions qui les ont précédées (et les plantages) ; Réglages › Système › « Ouvrir le journal ». <b>Point lumineux</b> dans le menu de gauche (disposition B) : un point vert qui pulse à droite de Morceaux, Albums, Composer avec l\'IA, Partition, Réglages ou Aide tant qu\'un travail correspondant est en cours (morceaux, clips et vidéos, pochettes, IA, partition, téléchargements). <b>Réglages › Styles</b> : ajouter ou retirer des balises de Genres, Ambiance, Voix, Instruments et Son, avec « Rétablir ce groupe » et « Tout réinitialiser ».</td></tr>\n<tr><td>11 nov. 2026 (1.1.010)</td>'),
 ('Chaque champ a un bouton ✕ pour l\'effacer.</li>',
  'Chaque champ a un bouton ✕ pour l\'effacer. La case <b>Sauvegarder le style de ce groupe</b> (1.1.011) enregistre, à chaque morceau lancé, son style dans vos styles sous le nom du groupe, sans jamais remplacer un style qui porte déjà ce nom ; elle existe aussi dans l\'onglet « Composer avec l\'IA » (album, sous « Nom du groupe »).</li>'),
 ('<h3 id="analyse-reglage">Analyse audio</h3>',
  '<h3 id="styles-reglage">Styles (balises)</h3>\n<p>Réglages › Styles (1.1.011) : les listes de balises du bloc Style de « Nouveau morceau » (Genres, Ambiance, Voix, Instruments, Son). La croix <b>×</b> d\'une balise la retire de la liste (un style déjà écrit la garde) ; le champ <b>Nouvelle balise</b> en ajoute (en anglais comme le modèle ; plusieurs balises séparées par des virgules) ; les balises ajoutées sont précédées d\'un point. Les balises retirées s\'affichent sous la liste : un clic les remet. <b>Rétablir ce groupe</b> remet la liste d\'origine d\'une catégorie, <b>Tout réinitialiser</b> celle de toutes les catégories (après confirmation). Les choix sont gardés dans <code>config\\reglages.json</code>.</p>\n<h3 id="analyse-reglage">Analyse audio</h3>'),
 ('<h3 id="sante">Carte graphique et test de santé</h3>',
  '<h3 id="journal-debug">Journal de débogage</h3>\n<p>Le Studio écrit ce qui se passe dans <code>journal\\journal.log</code> (dossier du Studio ; au-delà de 2 Mo, l\'ancien contenu passe dans <code>journal.ancien.log</code>) : démarrage, <b>clics</b> (texte du bouton et fenêtre), morceaux et travaux lancés ou terminés, messages affichés et, pour chaque <b>erreur</b>, les 20 dernières actions qui l\'ont précédée ; un plantage du programme y est noté aussi. Réglages › Système › <b>Ouvrir le journal</b> l\'ouvre. À joindre à un message en cas de problème ; il ne recopie pas le texte des paroles.</p>\n<h3 id="sante">Carte graphique et test de santé</h3>'),
 ('Le navigateur de l\'aide est fermé dès qu\'on quitte la page (environ 220 Mo de mémoire rendus).</li>',
  'Le navigateur de l\'aide est fermé dès qu\'on quitte la page (environ 220 Mo de mémoire rendus). Depuis la 1.1.011, un <b>point vert lumineux</b> s\'affiche à droite d\'une entrée du menu tant qu\'un travail correspondant est en cours : Morceaux (morceaux en file ou en cours, clips, vidéos), Albums (pochettes, descriptions, vidéos), Composer avec l\'IA, Partition, Réglages (téléchargements), Aide (question à l\'IA) ; il disparaît quand les travaux sont finis.</li>'),
]
EN = [
 ('<tr><td>11 Nov 2026 (1.1.010)</td>',
  '<tr><td>11 Nov 2026 (1.1.011)</td><td>Four additions in one version. <b>Band style</b>: a “Save this band\'s style” box under “Band or singer” (New track, and Compose with AI in album mode): each track started saves its style in your styles under the band\'s name, if it does not exist yet; in “Albums”, a “Custom style…” button on each album creates a custom style named after the band with the tags common to its tracks. <b>Debug log</b>: the file <code>journal\\journal.log</code> (next to the Studio) records startup, clicks, tracks and jobs started or finished, messages and errors with the last actions before them (and crashes); Settings › System › “Open the log”. <b>Glowing dot</b> in the left menu (layout B): a pulsing green dot to the right of Tracks, Albums, Compose with AI, Score, Settings or Help while a matching job is running (tracks, clips and videos, covers, AI, score, downloads). <b>Settings › Styles</b>: add or remove tags for Genres, Mood, Voice, Instruments and Sound, with “Reset this group” and “Reset everything”.</td></tr>\n<tr><td>11 Nov 2026 (1.1.010)</td>'),
 ('<h3 id="analyse-reglage">Audio analysis</h3>',
  '<h3 id="styles-reglage">Styles (tags)</h3>\n<p>Settings › Styles (1.1.011): the tag lists of the Style block of “New track” (Genres, Mood, Voice, Instruments, Sound). The <b>×</b> of a tag removes it from the list (a style already written keeps it); the <b>New tag</b> field adds some (in English like the model; several tags separated by commas); added tags are preceded by a dot. Removed tags are shown under the list: a click puts them back. <b>Reset this group</b> restores a category\'s original list, <b>Reset everything</b> that of all categories (after confirmation). The choices are kept in <code>config\\reglages.json</code>.</p>\n<h3 id="analyse-reglage">Audio analysis</h3>'),
 ('<h3 id="sante">Graphics card and health test</h3>',
  '<h3 id="journal-debug">Debug log</h3>\n<p>The Studio writes what happens to <code>journal\\journal.log</code> (Studio folder; beyond 2 MB the old content moves to <code>journal.ancien.log</code>): startup, <b>clicks</b> (button text and window), tracks and jobs started or finished, messages shown and, for each <b>error</b>, the 20 last actions before it; a program crash is noted too. Settings › System › <b>Open the log</b> opens it. Attach it to a message if there is a problem; it does not copy the lyrics text.</p>\n<h3 id="sante">Graphics card and health test</h3>'),
]
EN += [
 ('Each field has a ✕ button to clear it.</li>',
  'Each field has a ✕ button to clear it. The <b>Save this band’s style</b> box (1.1.011) saves, for each track started, its style in your styles under the band’s name, never replacing a style that already has this name; it is also in the “Compose with AI” tab (album, under “Band name”).</li>'),
 ('is closed as soon as you leave the page (about 220 MB of memory given back).</li>',
  'is closed as soon as you leave the page (about 220 MB of memory given back). Since 1.1.011, a <b>glowing green dot</b> appears to the right of a menu entry while a matching job is running: Tracks (tracks queued or running, clips, videos), Albums (covers, descriptions, videos), Compose with AI, Score, Settings (downloads), Help (question to the AI); it disappears when the jobs are finished.</li>'),
]
edit('aide.html', FR)
edit('aide.en.html', EN)
print('OK')
