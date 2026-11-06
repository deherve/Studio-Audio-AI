// 1.0.89 : voir score.hpp.
#include "score.hpp"
#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode

#include <algorithm>
#include <array>
#include <map>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

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


// ======================================================= 1.0.97 : partition de tous les instruments (stems)
namespace {
struct StemParse {
    std::string                                   meter, unit, tempo, key;
    std::vector<std::pair<std::string, std::string>> voices;  // identifiant, texte de la voix (lignes de notes jointes)
};
std::string field_value(const std::string & line) {
    std::string v = line.size() > 2 ? line.substr(2) : std::string();
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
    return v;
}
StemParse parse_stem(const std::string & abc) {
    StemParse           r;
    std::istringstream  in(abc);
    std::string         line, current;
    bool                body = false;
    std::map<std::string, std::string> text;
    std::vector<std::string>           order;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!body) {
            if (line.rfind("M:", 0) == 0) r.meter = field_value(line);
            else if (line.rfind("L:", 0) == 0) r.unit = field_value(line);
            else if (line.rfind("Q:", 0) == 0) r.tempo = field_value(line);
            else if (line.rfind("K:", 0) == 0) { r.key = field_value(line); body = true; }
            continue;
        }
        if (line.rfind("V:", 0) == 0) {
            std::istringstream v(line.substr(2));
            v >> current;
            if (!text.count(current)) order.push_back(current);
            continue;
        }
        if (line.empty() || line[0] == '%') continue;
        if (line.size() > 1 && line[1] == ':' && isalpha((unsigned char) line[0])) continue;  // champs au milieu du corps (M:, K:, Q:…) : ignorés
        text[current] += line + "|";
    }
    for (const std::string & id : order) r.voices.push_back({ id, text[id] });
    return r;
}
int note_count(const std::string & s) {
    int  n = 0;
    bool quote = false;
    for (char c : s) {
        if (c == '"') quote = !quote;
        else if (!quote && ((c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g'))) n++;
    }
    return n;
}
int measure_units(const std::string & meter, const std::string & unit) {  // durée d'une mesure en unités L:
    int num = 4, den = 4;
    if (meter == "C|") { num = 2; den = 2; }
    else if (meter != "C" && sscanf(meter.c_str(), "%d/%d", &num, &den) != 2) { num = 4; den = 4; }
    int u = 8;
    if (sscanf(unit.c_str(), "1/%d", &u) != 1) u = (double) num / den < 0.75 ? 16 : 8;
    const double v = (double) num / den * u;
    return v > 0 && v == (int) v ? (int) v : 0;
}
std::vector<std::string> measures_of(const std::string & text, int mes) {  // mesures séparées ; « Z3 » devient 3 silences de mesure
    std::vector<std::string> out;
    std::string              cur;
    auto flush = [&] {
        size_t b = cur.find_first_not_of(" \t");
        std::string t = b == std::string::npos ? std::string() : cur.substr(b);
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
        cur.clear();
        if (t.empty()) return;
        if (t[0] == 'Z') {
            const int k = t.size() > 1 ? std::max(1, atoi(t.c_str() + 1)) : 1;
            for (int i = 0; i < k; i++) out.push_back("z" + std::to_string(mes ? mes : 8));
        } else out.push_back(t);
    };
    for (char c : text) {
        if (c == '|') flush();
        else cur += c;
    }
    flush();
    return out;
}
}  // namespace

// Plusieurs partitions d'une voix (une par stem) -> une partition à plusieurs portées. stems : identifiant de voix (Vocal, Other, Bass), nom
// affiché, partition. La première fixe la mesure, l'unité, le tempo ; les autres sont rangées mesure par mesure (décalage possible si leur
// tempo diffère : signalé dans notes) ; une portée dont la mesure ou l'unité diffère est écartée.
std::string abc_combine_stems(const std::string & title, const std::vector<std::array<std::string, 3>> & stems, std::string & notes) {
    std::vector<StemParse> parsed;
    for (const auto & s : stems) parsed.push_back(parse_stem(s[2]));
    if (parsed.empty()) return std::string();
    const StemParse & master = parsed[0];
    const int         mes    = measure_units(master.meter, master.unit);
    struct Line { std::string id, name, key, unit; int mes = 0; std::vector<std::string> bars; };
    std::vector<Line> lines;
    size_t            longest = 0;
    for (size_t i = 0; i < parsed.size(); i++) {
        const StemParse & p = parsed[i];
        if (i > 0 && p.meter != master.meter) {
            notes += stems[i][1] + " : mesure " + p.meter + " différente de la première portée (" + master.meter + "), portée écartée. ";
            continue;
        }
        const std::pair<std::string, std::string> * best = nullptr;
        int best_n = 0;
        for (const auto & v : p.voices) {
            const int n = note_count(v.second);
            if (n > best_n) { best_n = n; best = &v; }
        }
        if (!best) { notes += stems[i][1] + " : aucune note reconnue. "; continue; }
        Line l;
        l.id   = stems[i][0];
        l.name = stems[i][1];
        l.key  = p.key != master.key ? p.key : std::string();
        l.unit = p.unit != master.unit ? p.unit : std::string();  // unité de note propre à cette portée (L: au début de sa voix)
        l.mes  = l.unit.empty() ? mes : measure_units(p.meter, p.unit);
        l.bars = measures_of(best->second, l.mes);
        longest = std::max(longest, l.bars.size());
        if (i > 0) {
            int a = 0, b = 0;
            if (sscanf(p.tempo.c_str(), "%*[^=]=%d", &a) == 1 && sscanf(master.tempo.c_str(), "%*[^=]=%d", &b) == 1 && a > 0 && b > 0 &&
                std::abs(a - b) * 100 > 4 * b)
                notes += stems[i][1] + " : tempo " + std::to_string(a) + " BPM contre " + std::to_string(b) + " : décalage possible vers la fin. ";
        }
        lines.push_back(l);
    }
    if (lines.empty()) return std::string();
    std::string out = "X:1\nT:" + title + "\nM:" + master.meter + "\nL:" + master.unit + "\n" + (master.tempo.empty() ? std::string() : "Q:" + master.tempo + "\n") + "%%score";
    for (const Line & l : lines) out += " " + l.id;
    out += "\n";
    for (const Line & l : lines) out += "V:" + l.id + " clef=treble name=\"" + l.name + "\" snm=\"" + l.name.substr(0, 4) + ".\"\n";
    out += "K:" + master.key + "\n";
    for (const Line & l : lines) {
        out += "V:" + l.id + "\n";
        if (!l.key.empty()) out += "K:" + l.key + "\n";
        if (!l.unit.empty()) out += "L:" + l.unit + "\n";
        for (size_t k = 0; k < longest; k++) {
            out += k < l.bars.size() ? l.bars[k] : "z" + std::to_string(l.mes ? l.mes : 8);
            out += "|";
            if (k % 4 == 3 || k + 1 == longest) out += "\n";
        }
    }
    return out;
}
