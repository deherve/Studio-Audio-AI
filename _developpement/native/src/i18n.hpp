// i18n : traduction de l'interface par fichiers de langue (un fichier JSON par langue, dossier « langues »).
//
// Les textes du code sont en français : le texte français sert de clé. Un fichier de langue associe à chaque
// clé sa traduction ; une clé absente ou vide retombe sur l'anglais, puis sur le français d'origine.
// Un nouveau fichier déposé dans « langues » apparaît tout seul dans la liste des langues.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace i18n {

struct Language {
    std::string           code;    // « fr », « en », « zh-Hans »… (nom du fichier sans extension)
    std::string           name;    // nom dans la langue elle-même (« Deutsch », « 日本語 »)
    std::string           script;  // écriture, pour les polices : latin, ja, ko, zh-Hans, zh-Hant
    std::filesystem::path file;
};

// Parcourt le dossier des langues ; à appeler une fois au démarrage.
void init(const std::filesystem::path & dir);
const std::vector<Language> & languages();
// Code de la langue du système (« fr », « ja »…) s'il existe un fichier pour elle, sinon « en » puis « fr ».
std::string system_language();
// Change de langue (charge le fichier). Renvoie faux si le code est inconnu.
bool               set_language(const std::string & code);
const std::string & current();
std::string        current_script();
// Tous les caractères des textes de la langue courante (construction des polices).
const std::u32string & glyph_text();

// Traduit une clé (texte français). Sûr entre threads.
const char * tr(const char * key);
std::string  tr(const std::string & key);
// Traduit puis formate (printf) : trf("%d morceaux", n). Les arguments gardent l'ordre du texte français.
std::string  trf(const char * key, ...);
// Traduit un tableau de textes (pour ImGui::Combo) ; les pointeurs restent valables jusqu'au changement de langue.
std::vector<const char *> tr_list(const char * const * items, int count);

}  // namespace i18n

using i18n::tr;   // tr("Texte français") renvoie la traduction ; tr(std::string) pour une clé calculée

using i18n::trf;
using i18n::tr_list;

// N_("Texte français") : marque un texte de tableau statique à traduire plus tard (au moment de l'afficher, avec tr()).
#define N_(text) text
