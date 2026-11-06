// 1.1.001 : petites fonctions de texte et de fichier communes à tout le Studio.
// Avant, chacune était recopiée dans plusieurs fichiers (trim 6 fois, lecture d'un fichier 5 fois…).
#pragma once

#include <filesystem>
#include <string>

std::string trim(const std::string & s);                                      // sans espaces, tabulations ni fins de ligne autour
std::string lower(std::string s);                                             // minuscules ASCII (les lettres accentuées UTF-8 restent telles quelles)
std::string read_text(const std::filesystem::path & file);                    // contenu entier du fichier, vide s'il est absent
void        write_text(const std::filesystem::path & file, const std::string & text);  // crée ou remplace le fichier
std::string url_encode(const std::string & s);                                // texte pour une adresse web (lettres, chiffres, - _ . ~ gardés)
