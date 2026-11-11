// Journal de débogage (1.1.011) : <application>\journal\journal.log.
// Garde ce qui se passe dans le Studio (démarrage, clics de l'utilisateur, travaux lancés et terminés, erreurs)
// pour retrouver ce qui a été fait avant un problème. Une erreur écrit aussi les dernières actions de l'utilisateur.
#pragma once
#include <string>
#include <filesystem>

namespace journal {

// Ouvre le journal (un seul fichier, ancien contenu gardé dans journal.ancien.log au-delà de 2 Mo). Sans effet répété.
void init(const std::filesystem::path & dir, const std::string & version);
// Évènement ordinaire : categorie courte (« appli », « morceau », « clip »…) + message.
void event(const std::string & category, const std::string & message);
// Erreur : ajoute les dernières actions de l'utilisateur pour savoir ce qui l'a provoquée.
void error(const std::string & category, const std::string & message);
// Action de l'utilisateur (clic, choix) : gardée en mémoire et écrite dans le fichier.
void action(const std::string & message);
// Chemin du fichier (vide si non ouvert).
std::filesystem::path path();

// Interface : à appeler une fois après ImGui::CreateContext, puis à la fin de chaque image (après app.frame()).
void imgui_install();
void imgui_frame_end();

}  // namespace journal
