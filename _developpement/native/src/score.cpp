// 1.0.89 : voir score.hpp.
#include "score.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

static std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

ScoreInfo parse_score(const std::string & abc) {
    ScoreInfo info;
    info.present = !abc.empty();
    info.text    = abc;
    std::istringstream in(abc);
    std::string        section, line, last_chord;  // vide = pas de section nommée (partition reconnue)
    std::vector<std::string> chords;
    auto flush = [&]() {
        // motifs répétés regroupés : « D A D A D A » -> « (D A) ×3 », « Em Em » déjà fusionnés plus haut
        std::string list;
        for (size_t i = 0; i < chords.size();) {
            size_t best_p = 0, best_k = 1;
            for (size_t p = 1; p <= 4 && i + 2 * p <= chords.size(); p++) {
                size_t k = 1;
                while (i + (k + 1) * p <= chords.size() && std::equal(chords.begin() + i, chords.begin() + i + p, chords.begin() + i + k * p)) k++;
                if (k >= 2 && k * p > best_k * best_p) { best_p = p; best_k = k; }
            }
            std::string part;
            const size_t len = best_p ? best_p : 1;
            for (size_t j = i; j < i + len; j++) part += (part.empty() ? "" : " ") + chords[j];
            if (best_p) part = (best_p > 1 ? "(" + part + ")" : part) + " ×" + std::to_string(best_k);
            list += (list.empty() ? "" : "  ") + part;
            i += best_p ? best_p * best_k : 1;
        }
        if (!list.empty()) info.sections.push_back({ section, list });
        chords.clear();
        last_chord.clear();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("K:", 0) == 0 && info.key.empty()) info.key = trim(line.substr(2));
        else if (line.rfind("M:", 0) == 0 && info.meter.empty()) info.meter = trim(line.substr(2));
        else if (line.rfind("Q:", 0) == 0 && info.tempo == 0) {
            size_t eq = line.find('=');
            info.tempo = atoi(line.c_str() + (eq == std::string::npos ? 2 : eq + 1));
        } else if (line.rfind("%", 0) == 0) {
            flush();
            section = trim(line.substr(1));
        } else if (line.rfind("V:", 0) != 0) {
            for (size_t p = line.find('"'); p != std::string::npos; p = line.find('"', p + 1)) {
                size_t q = line.find('"', p + 1);
                if (q == std::string::npos) break;
                std::string c = line.substr(p + 1, q - p - 1);
                if (!c.empty() && c[0] >= 'A' && c[0] <= 'G' && c != last_chord) {
                    chords.push_back(c);
                    last_chord = c;
                }
                p = q;
            }
        }
    }
    flush();
    return info;
}
// Transposition d'une partition ABC de k demi-tons : armure, notes (lettre, altération, octave) et accords.
// shortcut: une altération écrite vaut pour sa note seulement (en ABC elle vaut jusqu'à la barre) ; sans effet notable sur
// les partitions de YuE2, qui répètent les altérations. Modes (dorien…) traités comme majeur/mineur.
int abc_step(char L) { static const int st[] = { 9, 11, 0, 2, 4, 5, 7 }; return st[L - 'A']; }  // A B C D E F G
int key_signature(const std::string & key, bool * minor_out) {
    if (key.empty()) return 0;
    const char L = (char) toupper((unsigned char) key[0]);
    if (L < 'A' || L > 'G') return 0;
    size_t i  = 1;
    int    acc = 0;
    if (i < key.size() && key[i] == '#') { acc = 1; i++; }
    else if (i < key.size() && key[i] == 'b') { acc = -1; i++; }
    std::string rest = key.substr(i);
    for (char & c : rest) c = (char) tolower((unsigned char) c);
    const bool minor = rest.rfind("m", 0) == 0 && rest.rfind("maj", 0) != 0 && rest.rfind("mix", 0) != 0;
    if (minor_out) *minor_out = minor;
    static const int maj[] = { 3, 5, 0, 2, 4, -1, 1 }, min[] = { 0, 2, -3, -1, 1, -4, -2 };  // A..G
    return (minor ? min : maj)[L - 'A'] + 7 * acc;
}
std::map<char, int> key_alter(int sig) {
    std::map<char, int> m;
    const char * sharps = "FCGDAEB", * flats = "BEADGCF";
    for (int i = 0; i < std::abs(sig) && i < 7; i++) m[sig > 0 ? sharps[i] : flats[i]] = sig > 0 ? 1 : -1;
    return m;
}
std::string key_for(int pc, bool minor) {  // nom de tonalité le plus simple (moins d'altérations à l'armure)
    std::string best;
    int         best_abs = 99;
    for (char L : std::string("CDEFGAB"))
        for (int a = -1; a <= 1; a++) {
            if (((abc_step(L) + a) % 12 + 12) % 12 != pc) continue;
            std::string name = std::string(1, L) + (a > 0 ? "#" : a < 0 ? "b" : "") + (minor ? "m" : "");
            int         sig  = std::abs(key_signature(name));
            if (sig < best_abs) { best_abs = sig; best = name; }
        }
    return best;
}
std::string pc_name(int pc, bool flats) {
    static const char * sh[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char * fl[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (flats ? fl : sh)[((pc % 12) + 12) % 12];
}
std::string transpose_chord(const std::string & c, int k, bool flats) {
    if (c.empty() || c[0] < 'A' || c[0] > 'G') return c;
    size_t i  = 1;
    int    pc = abc_step(c[0]);
    if (i < c.size() && c[i] == '#') { pc++; i++; }
    else if (i < c.size() && c[i] == 'b') { pc--; i++; }
    std::string rest = c.substr(i);
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash + 1) + transpose_chord(rest.substr(slash + 1), k, flats);
    return pc_name(pc + k, flats) + rest;
}
std::string abc_transpose(const std::string & abc, int k) {
    if (k == 0) return abc;
    std::string src_key;
    {
        std::istringstream in(abc);
        for (std::string l; std::getline(in, l);)
            if (l.rfind("K:", 0) == 0) { src_key = trim(l.substr(2)); break; }
    }
    bool       minor = false;
    const int  sig   = key_signature(src_key, &minor);
    const int  tonic = src_key.empty() ? 0 : ((abc_step((char) toupper((unsigned char) src_key[0])) +
                                                (src_key.size() > 1 && src_key[1] == '#' ? 1 : src_key.size() > 1 && src_key[1] == 'b' ? -1 : 0)) % 12 + 12) % 12;
    const std::string dst_key = key_for(tonic + k, minor);
    const int         dsig    = key_signature(dst_key);
    const auto        sa = key_alter(sig), da = key_alter(dsig);
    (void) sig;
    std::istringstream in(abc);
    std::string        out, line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("K:", 0) == 0) { out += "K:" + dst_key + "\n"; continue; }
        if (line.empty() || line[0] == '%' || (line.size() > 1 && isalpha((unsigned char) line[0]) && line[1] == ':')) { out += line + "\n"; continue; }
        std::string res;
        for (size_t i = 0; i < line.size();) {
            const char ch = line[i];
            if (ch == '"') {  // accord
                const size_t q = line.find('"', i + 1);
                if (q == std::string::npos) { res += line.substr(i); break; }
                res += '"' + transpose_chord(line.substr(i + 1, q - i - 1), k, dsig < 0) + '"';
                i = q + 1;
                continue;
            }
            if (ch == '!') {  // décoration
                const size_t q = line.find('!', i + 1);
                if (q == std::string::npos) { res += line.substr(i); break; }
                res += line.substr(i, q - i + 1);
                i = q + 1;
                continue;
            }
            size_t j   = i;
            int    acc = 0;
            bool   has_acc = false;
            while (j < line.size() && (line[j] == '^' || line[j] == '_' || line[j] == '=')) {
                acc += line[j] == '^' ? 1 : line[j] == '_' ? -1 : 0;
                has_acc = true;
                j++;
            }
            if (j < line.size() && strchr("ABCDEFGabcdefg", line[j])) {
                const char L      = (char) toupper((unsigned char) line[j]);
                int        octave = islower((unsigned char) line[j]) ? 1 : 0;
                j++;
                while (j < line.size() && (line[j] == '\'' || line[j] == ',')) { octave += line[j] == '\'' ? 1 : -1; j++; }
                const int alter = has_acc ? acc : (sa.count(L) ? sa.at(L) : 0);
                const int pitch = 12 * octave + abc_step(L) + alter + k;
                // écriture dans la nouvelle tonalité : lettre dont la note (avec armure) tombe juste, sinon altération écrite
                std::string best;
                int         best_cost = 99;
                for (char M : std::string("CDEFGAB"))
                    for (int o = octave - 2; o <= octave + 2; o++) {
                        const int base = 12 * o + abc_step(M);
                        const int dd   = pitch - base;
                        const int key_a = da.count(M) ? da.at(M) : 0;
                        if (dd < -1 || dd > 1) continue;
                        int cost = dd == key_a ? 0 : (dsig >= 0 ? (dd >= 0 ? 1 : 2) : (dd <= 0 ? 1 : 2));
                        if (cost >= best_cost) continue;
                        std::string s = dd == key_a ? "" : dd == 1 ? "^" : dd == -1 ? "_" : "=";
                        s += o >= 1 ? (char) tolower((unsigned char) M) : M;
                        for (int x = o; x > 1; x--) s += '\'';
                        for (int x = o; x < 0; x++) s += ',';
                        best = s;
                        best_cost = cost;
                    }
                res += best.empty() ? line.substr(i, j - i) : best;
                i = j;
                continue;
            }
            res += ch;
            i++;
        }
        out += res + "\n";
    }
    return out;
}
std::string abc_set_tempo(const std::string & abc, int bpm) {
    std::istringstream in(abc);
    std::string        out, line;
    bool               done = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!done && line.rfind("Q:", 0) == 0) {
            const size_t eq = line.find('=');
            line = (eq == std::string::npos ? std::string("Q:1/4") : line.substr(0, eq)) + "=" + std::to_string(bpm);
            done = true;
        }
        out += line + "\n";
    }
    return out;
}

static int tonic_pc(const std::string & key) {
    if (key.empty() || toupper((unsigned char) key[0]) < 'A' || toupper((unsigned char) key[0]) > 'G') return -1;
    int pc = abc_step((char) toupper((unsigned char) key[0]));
    if (key.size() > 1 && key[1] == '#') pc++;
    else if (key.size() > 1 && key[1] == 'b') pc--;
    return ((pc % 12) + 12) % 12;
}
int key_distance(const std::string & from, const std::string & to) {
    const int a = tonic_pc(from), b = tonic_pc(to);
    if (a < 0 || b < 0) return 0;
    bool ma = false, mb = false;
    key_signature(from, &ma);
    key_signature(to, &mb);
    // tonalités relatives comparées sur leur majeur : Em -> G
    const int ra = (a + (ma ? 3 : 0)) % 12, rb = (b + (mb ? 3 : 0)) % 12;
    int d = ((rb - ra) % 12 + 12) % 12;
    return d > 6 ? d - 12 : d;
}

std::string abc_for_display(const std::string & abc, const std::string & title, const std::string & composer) {
    // 1. voix grave ? (plus de la moitié des notes sous le do du milieu : lettre majuscule suivie de « , ») -> clé de fa
    std::map<std::string, std::pair<int, int>> low;  // nom de voix -> (notes graves, notes)
    {
        std::istringstream in(abc);
        std::string        line, voice;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("V:", 0) == 0) {
                std::istringstream v(line.substr(2));
                v >> voice;
                continue;
            }
            if (voice.empty() || line.empty() || line[0] == '%' || (line.size() > 1 && isalpha((unsigned char) line[0]) && line[1] == ':')) continue;
            bool in_chord = false;
            for (size_t i = 0; i < line.size(); i++) {
                const char c = line[i];
                if (c == '"') { in_chord = !in_chord; continue; }
                if (in_chord || !strchr("ABCDEFGabcdefg", c)) continue;
                low[voice].second++;
                if (isupper((unsigned char) c) && i + 1 < line.size() && line[i + 1] == ',') low[voice].first++;
            }
        }
    }
    std::istringstream in(abc);
    std::string        out, line;
    bool               has_title = false, has_composer = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("T:", 0) == 0 && !has_title) {
            line      = "T:" + (title.empty() ? trim(line.substr(2)) : title);
            has_title = true;
        } else if (line.rfind("C:", 0) == 0) {
            has_composer = true;
        } else if (line.rfind("V:", 0) == 0) {
            std::istringstream v(line.substr(2));
            std::string        voice;
            v >> voice;
            const auto it = low.find(voice);
            if (it != low.end() && it->second.second > 0 && it->second.first * 2 > it->second.second) {
                const size_t c = line.find("clef=treble");
                if (c != std::string::npos) line.replace(c, 11, "clef=bass");
                else if (line.find("clef=") == std::string::npos && line.find("name=") != std::string::npos) line += " clef=bass";
            }
        } else if (line.rfind("K:", 0) == 0 && !has_composer && !composer.empty()) {
            out += "C:" + composer + "\n";  // avant K: (fin de l'en-tête)
            has_composer = true;
        }
        out += line + "\n";
    }
    if (!has_title && !title.empty()) out = "T:" + title + "\n" + out;
    return out;
}
