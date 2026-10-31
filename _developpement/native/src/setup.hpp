// Premier lancement : recherche des modèles, téléchargement ou choix d'un dossier existant.
#pragma once

#include "studio.hpp"

// Renseigne paths.model/vae/... ; si les modèles sont introuvables, propose de les télécharger (dans
// <dossier de l'application>\models par défaut) ou d'indiquer un dossier qui les contient déjà.
// Renvoie false si l'utilisateur quitte. Le dossier choisi est mémorisé à côté de l'exécutable.
bool ensure_models(void * instance, const fs::path & exe_dir, Paths & paths);
