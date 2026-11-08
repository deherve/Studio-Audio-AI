// Nom et numÃ©ro de version du programme (Studio Audio AI ; Â« Studio YuE2 Â» jusqu'Ã  la 1.0.39), affichÃ© dans la barre de titre et l'en-tÃªte.
// RÃ¨gle : +0.0.1 Ã  chaque ajout, correction ou modification du programme (noter la version dans CLAUDE.md
// et dans le journal des Ã©volutions de l'aide).
#pragma once

#define STUDIO_VERSION   "1.1.007"
#define APP_NAME         "Studio Audio AI"
#define APP_NAME_W       L"Studio Audio AI"
#define STUDIO_VERSION_W L"1.1.007"

// Adresse interrogÃ©e au dÃ©marrage pour savoir si une version plus rÃ©cente existe (voir update_check.hpp).
// 1.0.77 : derniÃ¨re publication (release) du dÃ©pÃ´t GitHub public du Studio ; Â« html_url Â» = sa page de tÃ©lÃ©chargement.
// On peut la remplacer sans recompiler, avec la clÃ© Â« update_url Â» de config\reglages.json.
#define UPDATE_MANIFEST_URL "https://api.github.com/repos/deherve/Studio-Audio-AI/releases/latest"




