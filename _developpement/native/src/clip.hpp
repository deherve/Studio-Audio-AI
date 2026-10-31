// Atelier clip (1.0.52) : clip animé d'un morceau avec paroles synchronisées, fait par le seul ffmpeg du Studio
// (zoompan + xfade + sous-titres ASS), sans Node ni navigateur. Ce fichier ne contient que de la logique pure
// (aucune dépendance à l'interface ni aux tâches) ; l'enchaînement des étapes est dans media.cpp (start_clips).
#pragma once

#include <string>
#include <vector>

struct ClipLine {
    std::string text, section;  // section : « Verse », « Chorus »… (balise des paroles)
    double      start = 0, end = 0;
};

struct ClipWord {
    std::string text;
    double      start = 0, end = 0;
};

// Lignes de paroles et leur section, d'après le texte du Studio (« [Verse] » sur sa propre ligne).
std::vector<ClipLine> clip_lyrics_lines(const std::string & lyrics);
// Mots horodatés d'une sortie « whisper-cli -oj -ml 1 -sow » (un mot par segment).
std::vector<ClipWord> clip_parse_words(const std::string & whisper_json);
// Donne à chaque ligne son début et sa fin : mots connus rapprochés des mots entendus (plus longue suite commune),
// lignes sans mot reconnu réparties entre leurs voisines. Renvoie la part de mots retrouvés (0 à 1).
double clip_align(std::vector<ClipLine> & lines, const std::vector<ClipWord> & heard, double duration);
// Fichier de sous-titres ASS (UTF-8) : couplet blanc, refrain plus grand et orangé, majuscules.
// 1.0.54 : textes d'habillage du clip. Carte de début (0,4 s à 6 s) : titre du morceau, « groupe — album », puis le texte de
// début de l'utilisateur ; carte de fin (8 dernières secondes) : le texte de fin de l'utilisateur au centre. Tout vide = rien.
struct ClipTexts {
    std::string title, artist, album, start, end;  // start et end : plusieurs lignes possibles (retours à la ligne)
    double      duration = 0;
};
std::string clip_ass(const std::vector<ClipLine> & lines, int width, int height, const ClipTexts & texts = ClipTexts());
// Script de filtres ffmpeg (-filter_complex_script) : plans de caméra lents sur les images, fondus, vignette, sous-titres.
// Entrées 0..n-1 = les images (chacune en boucle), image_start[k] = seconde où l'image k apparaît (image_start[0] = 0) ;
// sortie « [v] ». ass_path : chemin déjà échappé pour un filtre (voir clip_filter_path).
std::string clip_filtergraph(const std::vector<ClipLine> & lines, double duration, int width, int height, const std::string & ass_path,
                             const std::vector<double> & image_start);

// ---- Scénario d'après les paroles (1.0.53) : l'IA raconte l'histoire de la chanson puis la découpe en scènes ----
struct SceneSpec {
    int         first = -1, last = -1;  // lignes de paroles couvertes (0 à n-1) ; -1 si instrumental
    std::string prompt;                  // description de l'image (anglais), personnage et style compris
    // Au-delà de 12 images, chaque scène de l'IA est déclinée en plusieurs plans (autres angles) : base = scène d'origine,
    // part / parts = rang du plan dans cette scène (voir clip_scene_starts).
    int base = 0, part = 0, parts = 1;
};
struct Storyboard {
    std::string            story;  // l'histoire en quelques phrases
    std::vector<SceneSpec> scenes;
};
// Types de rendu proposés dans l'atelier clip (liste de choix). prompt vide = « Auto » : l'IA choisit un style d'après la
// musique et les paroles.
struct ClipStyle {
    const char * id;      // nom de sous-dossier de clip_scenes (les images d'un style sont gardées à part)
    const char * label;   // texte affiché (français, passé à tr())
    const char * prompt;  // fragment de style envoyé à l'IA et ajouté à chaque image (anglais)
};
const std::vector<ClipStyle> & clip_styles();

std::string clip_story_system();
// instrumental : pas de lignes, l'IA imagine un voyage d'après le titre et le style.
// context : fiches du morceau, de l'album et du groupe (texte) pour que le scénario respecte l'univers du groupe.
// forced_style : type de rendu choisi par l'utilisateur (voir clip_styles), vide = l'IA choisit.
std::string clip_story_user(const std::string & title, const std::string & style, const std::string & context, const std::vector<ClipLine> & lines,
                            const std::string & forced_style = std::string(), int scenes = 0);
// Lit la réponse (JSON, même entouré de texte) : scènes consécutives couvrant toutes les lignes, au moins 3. Faux sinon.
// forced_style non vide : remplace le style proposé par l'IA dans chaque description d'image.
// max_scenes : nombre d'images demandé (0 = automatique, jusqu'à 12) ; au-delà, les scènes en trop sont retirées.
bool clip_story_parse(const std::string & answer, size_t line_count, Storyboard & out, const std::string & forced_style = std::string(), int max_scenes = 0);
// Nombre d'images conseillé pour un morceau, d'après ses paroles et sa durée : une image pour deux lignes (une phrase), jamais plus
// d'une image toutes les 6 secondes, entre 6 et 60. Instrumental (0 ligne) : une image toutes les 20 secondes.
int clip_recommend_images(size_t line_count, double seconds);

// Beaucoup d'images (plus de 12) : l'IA ne peut pas écrire autant de scènes d'un coup. Demande 1 : l'histoire, le personnage et
// le style ; puis une demande par morceau de paroles (a à b, 0-based ; lignes vides = instrumental, part / parts), m scènes
// chacune ; enfin les scènes brutes sont réunies en un JSON lisible par clip_story_parse.
std::string clip_plan_user(const std::string & title, const std::string & style, const std::string & context, const std::vector<ClipLine> & lines,
                           const std::string & forced_style);
bool        clip_plan_parse(const std::string & answer, std::string & story, std::string & character, std::string & style);
std::string clip_chunk_user(const std::string & story, const std::string & character, const std::string & style, const std::vector<ClipLine> & lines,
                            int a, int b, int m, int part, int parts);
bool        clip_chunk_parse(const std::string & answer, int a, int b, int m, std::vector<SceneSpec> & raw);
std::string clip_story_assemble(const std::string & story, const std::string & character, const std::string & style, const std::vector<SceneSpec> & raw);
// Seconde de début de chaque scène : première ligne de la scène (0 pour la première) ; instrumental : parts égales.
std::vector<double> clip_scene_starts(const Storyboard & sb, const std::vector<ClipLine> & lines, double duration);
// Chemin utilisable dans « ass='…' » : barres obliques et deux-points échappés.
std::string clip_filter_path(const std::string & path);
