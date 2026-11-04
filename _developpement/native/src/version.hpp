// Nom et numéro de version du programme (Studio Audio AI ; « Studio YuE2 » jusqu'à la 1.0.39), affiché dans la barre de titre et l'en-tête.
// Règle : +0.0.1 à chaque ajout, correction ou modification du programme (noter la version dans CLAUDE.md
// et dans le journal des évolutions de l'aide).
#pragma once

#define STUDIO_VERSION   "1.0.79"
#define APP_NAME         "Studio Audio AI"
#define APP_NAME_W       L"Studio Audio AI"
#define STUDIO_VERSION_W L"1.0.79"

// Adresse interrogée au démarrage pour savoir si une version plus récente existe (voir update_check.hpp).
// 1.0.77 : dernière publication (release) du dépôt GitHub public du Studio ; « html_url » = sa page de téléchargement.
// On peut la remplacer sans recompiler, avec la clé « update_url » de config\reglages.json.
#define UPDATE_MANIFEST_URL "https://api.github.com/repos/deherve/Studio-Audio-AI/releases/latest"




