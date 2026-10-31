// Recherche d'une version plus récente du Studio Audio AI sur Internet (au démarrage, en tâche de fond).
//
// L'adresse interrogée est UPDATE_MANIFEST_URL (version.hpp), ou la clé « update_url » de studio_reglages.json.
// Deux formats de réponse sont compris :
//  - une publication GitHub (api.github.com/repos/<compte>/<dépôt>/releases/latest) : « tag_name » (« v1.0.4 »),
//    « html_url », « body » ;
//  - un petit fichier JSON : { "version": "1.0.4", "url": "https://…", "notes": "…" }.
// Sans adresse, rien n'est demandé ; hors ligne, la recherche abandonne en quelques secondes.
#pragma once

#include <atomic>
#include <memory>
#include <string>

struct UpdateInfo {
    std::atomic<int> state{ 0 };  // 0 non configurée, 1 recherche, 2 à jour, 3 mise à jour disponible, 4 hors ligne ou réponse illisible
    std::string      latest;      // « 1.0.4 » (lu seulement quand state vaut 2 ou 3)
    std::string      url;         // page de téléchargement
    std::string      notes;       // nouveautés (texte court)
};

// Compare deux numéros « 1.0.12 » : négatif si a < b, 0 si égaux, positif si a > b (un « v » initial est ignoré).
int compare_versions(const std::string & a, const std::string & b);

// Interroge l'adresse et remplit info (à lancer dans un thread : appel bloquant de quelques secondes au plus).
void check_for_update(std::shared_ptr<UpdateInfo> info, std::string manifest_url);
