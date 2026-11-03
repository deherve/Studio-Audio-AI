// Nom et numéro de version du programme (Studio Audio AI ; « Studio YuE2 » jusqu'à la 1.0.39), affiché dans la barre de titre et l'en-tête.
// Règle : +0.0.1 à chaque ajout, correction ou modification du programme (noter la version dans CLAUDE.md
// et dans le journal des évolutions de l'aide).
#pragma once

#define STUDIO_VERSION   "1.0.76"
#define APP_NAME         "Studio Audio AI"
#define APP_NAME_W       L"Studio Audio AI"
#define STUDIO_VERSION_W L"1.0.76"

// Adresse interrogée au démarrage pour savoir si une version plus récente existe (voir update_check.hpp).
// Vide tant qu'aucun site de publication n'est choisi : la recherche est alors désactivée. On peut aussi la
// renseigner sans recompiler, avec la clé « update_url » de studio_reglages.json.
#define UPDATE_MANIFEST_URL ""




