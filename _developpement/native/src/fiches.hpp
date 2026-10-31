// Fiches texte (1.0.52) : à la fin d'un morceau, le Studio écrit dans son dossier « paroles.txt » (les paroles, que
// l'utilisateur peut corriger : l'atelier clip s'en sert pour caler les bonnes paroles) et « fiche.txt » ; puis
// « fiche_album.txt » dans le dossier de l'album et « fiche_groupe.txt » dans celui du groupe (reconstruites d'après les
// fiches des morceaux présents sur le disque). Texte brut UTF-8, lisible dans le Bloc-notes.
#pragma once

#include "studio.hpp"

// Écrit paroles.txt (sans écraser une version déjà présente) et fiche.txt dans le dossier du morceau, puis met à jour
// la fiche de l'album et celle du groupe. À appeler quand le morceau est terminé, et à la demande pour l'existant.
void write_fiches(const Song & song, const fs::path & song_dir);

// Morceau long assemblé (1.0.48 : partie 1 + partie 2) : le morceau final ne porte que les paroles de la partie 2, alors que
// son audio contient les deux parties. Renvoie une copie dont les paroles sont celles de la partie 1 suivies de celles de la
// partie 2 (all = tous les morceaux connus) ; tout autre morceau est rendu tel quel.
Song with_full_lyrics(const Song & s, const std::vector<Song> & all);
// Si paroles.txt du morceau ne contient encore que les paroles de la partie 2 (jamais corrigé à la main), il est supprimé :
// write_fiches le réécrira avec les paroles complètes.
void drop_stale_lyrics_file(const Song & original, const Song & full, const fs::path & song_dir);
