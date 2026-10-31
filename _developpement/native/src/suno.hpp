// Conversion des prompts au format Suno (script de génération par IA) vers le format attendu par YuE2,
// et profils de style pré-renseignés par influence (groupes et chanteurs).
#pragma once

#include <string>
#include <vector>

struct Influence {
    const char * name;
    const char * tags;       // balises YuE2 en anglais, séparées par des virgules
    const char * singer;     // chant (formation classique) ; "" si inconnu
    const char * musicians;  // « Nom instrument » séparés par des virgules ; "" si inconnu
};
extern const std::vector<Influence> INFLUENCES;

// Style complet d'un profil : balises de son, puis chanteur (« … vocals ») et musiciens.
// Non documenté par YuE2 : à l'essai pour rapprocher le timbre et le jeu des musiciens.
std::string influence_style(const Influence & inf);
std::string singer_tag(const Influence & inf);

struct Language {
    const char * label;  // libellé affiché
    const char * tag;    // balise de style YuE2 ("" = aucune)
};
extern const std::vector<Language> LANGUAGES;

// Bloc « Titre » : « 11. Titre », « Album : … », « Groupe : … ».
struct TitleBlock {
    std::string title, album, artist, track;
};
TitleBlock parse_title_block(const std::string & text);

// Bloc « Style » : drapeaux MAX retirés, description française traduite en balises anglaises,
// influences citées remplacées par leur profil. Renvoie "" si rien n'a été reconnu.
std::string convert_style(const std::string & text);

// Bloc « Structure » : sections Suno ([Verse 1 | …], [(Mandatory Acoustic Bridge)], [(End)]…) ramenées
// aux sections YuE2, phonétique et annotations retirées. Les en-têtes [Style:], [Mood:], [Tempo:] et
// les balises hors section alimentent style_extra.
std::string convert_lyrics(const std::string & text, bool instrumental, std::vector<std::string> & style_extra);

// Balises de style : découpe, fusion sans doublon, langue placée en tête.
std::vector<std::string> split_style(const std::string & style);
std::string              join_style(const std::vector<std::string> & tags);
std::string              merge_style(const std::string & style, const std::vector<std::string> & extra);
bool                     needs_conversion(const std::string & lyrics);  // syntaxe Suno détectée
std::string              set_language(const std::string & style, const char * tag);
int                      detect_language(const std::string & style);  // indice dans LANGUAGES, 0 si aucune
// Langue des paroles d'un morceau : code (« fr », « en », « ja »…) déclaré dans le style, sinon détecté d'après le texte
// (écriture, puis mots courants) ; vide si inconnue. declared (facultatif) : vrai si elle vient du style.
std::string              detect_lyrics_language(const std::string & style, const std::string & lyrics, bool * declared = nullptr);
int                      language_index_for_code(const std::string & code);  // indice dans LANGUAGES, 0 si inconnu
std::string              fold(const std::string & s);                 // minuscules, accents compris
