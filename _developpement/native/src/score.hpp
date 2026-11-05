// 1.0.89 : partitions ABC écrites par YuE2 (score.abc) : lecture (tonalité, tempo, mesure, accords par section),
// transposition et tempo. Utilisé par l'interface (main.cpp) et par la file de génération (studio.cpp).
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

struct ScoreInfo {
    bool                                             present = false;
    std::string                                      key, meter, text;
    int                                              tempo = 0;
    std::vector<std::pair<std::string, std::string>> sections;  // nom de section, accords
};
ScoreInfo   parse_score(const std::string & abc);
int         abc_step(char letter);                                         // A..G -> demi-tons depuis Do
int         key_signature(const std::string & key, bool * minor_out = nullptr);  // dièses (+) ou bémols (-) à l'armure
std::map<char, int> key_alter(int sig);
std::string key_for(int pc, bool minor);                                    // nom de tonalité le plus simple
std::string pc_name(int pc, bool flats);
std::string transpose_chord(const std::string & chord, int k, bool flats);
std::string abc_transpose(const std::string & abc, int k);                // k demi-tons, armure + notes + accords
std::string abc_set_tempo(const std::string & abc, int bpm);
int         key_distance(const std::string & from, const std::string & to);  // demi-tons de from à to (-5..+6), 0 si inconnu
