// Paroles d'un morceau de reprise : recherche LRCLIB et filtre des inventions de Whisper (voir lyrics_web.hpp).
#include "lyrics_web.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>  // _strnicmp
#include <set>
#include <sstream>

#include "net.hpp"     // https_get
#include "studio.hpp"  // run_command, quote_arg, narrow, widen
#include "version.hpp"
#include "yyjson.h"

namespace {

// ------------------------------------------------------------------ texte

std::string trim(const std::string & s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// Minuscules et ponctuation remplacée par des espaces (les lettres accentuées UTF-8 sont gardées telles quelles).
std::string normalize(const std::string & s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 0x80) out += (char) c;                       // octet d'un caractère UTF-8 : gardé
        else if (std::isalnum(c)) out += (char) std::tolower(c);
        else if (c == '\'') continue;                         // « don't » = « dont »
        else out += ' ';
    }
    return out;
}

std::vector<std::string> words(const std::string & s) {
    std::vector<std::string> out;
    std::istringstream       in(normalize(s));
    std::string              w;
    while (in >> w) out.push_back(w);
    return out;
}

// Encodage d'un paramètre d'adresse web (UTF-8, %XX).
std::string url_encode(const std::string & s) {
    std::string out;
    char        buf[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char) c;
        else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Retire un préfixe du type « 08 », « 08 - », « 08. » (numéro de piste) ou « 1990. » (année d'un dossier d'album).
std::string strip_number_prefix(const std::string & s, bool year) {
    size_t i = 0;
    while (i < s.size() && std::isdigit((unsigned char) s[i])) i++;
    if (i == 0 || i > 4) return s;
    if (year && i != 4) return s;
    if (!year && i > 3) return s;
    size_t j = i;
    while (j < s.size() && (s[j] == ' ' || s[j] == '.' || s[j] == '-' || s[j] == '_')) j++;
    if (j == i || j >= s.size()) return s;  // « 1984 » seul : c'est un titre
    return s.substr(j);
}

// (requêtes HTTPS : https_get() dans net.cpp, délais courts)

// Une fiche LRCLIB : paroles brutes, durée, et si le morceau est marqué instrumental.
struct LrcRecord {
    std::string plain, artist, title, album;
    double      duration     = 0;
    bool        instrumental = false;
};

LrcRecord read_record(yyjson_val * o) {
    LrcRecord    r;
    yyjson_val * v = yyjson_obj_get(o, "plainLyrics");
    if (yyjson_is_str(v)) r.plain = yyjson_get_str(v);
    if (yyjson_is_str(v = yyjson_obj_get(o, "artistName"))) r.artist = yyjson_get_str(v);
    if (yyjson_is_str(v = yyjson_obj_get(o, "trackName"))) r.title = yyjson_get_str(v);
    if (yyjson_is_str(v = yyjson_obj_get(o, "albumName"))) r.album = yyjson_get_str(v);
    if (yyjson_is_num(v = yyjson_obj_get(o, "duration"))) r.duration = yyjson_get_num(v);
    if (yyjson_is_bool(v = yyjson_obj_get(o, "instrumental"))) r.instrumental = yyjson_get_bool(v);
    return r;
}

// ------------------------------------------------------------------ structure

struct Stanza {
    std::vector<std::string> lines;
    std::set<std::string>    keys;  // lignes normalisées, pour comparer les strophes entre elles
    int                      group = -1;
};

// Deux strophes sont « la même » (refrain) si au moins 60 % de leurs lignes sont communes.
bool same_stanza(const Stanza & a, const Stanza & b) {
    if (a.keys.empty() || b.keys.empty()) return false;
    size_t common = 0;
    for (const std::string & k : a.keys) common += b.keys.count(k);
    return (double) common / (double) std::max(a.keys.size(), b.keys.size()) >= 0.6;
}

}  // namespace

// ------------------------------------------------------------------ inventions de Whisper

bool is_whisper_hallucination(const std::string & line) {
    // Phrases de fin de vidéo et crédits de sous-titres que Whisper produit sur de la musique ou du silence.
    static const char * PATTERNS[] = {
        "thank you for watching", "thanks for watching", "thank you for listening", "subscribe", "like this video",
        "like and share", "next video", "see you next time", "see you in the next", "subtitles by", "captions by",
        "amara org", "transcribed by", "merci d avoir regard", "merci de votre attention", "abonnez vous", "sous titres",
        "sous titrage", "a bient t pour une nouvelle", "gracias por ver", "suscr", "untertitel", "danke f r",
        "grazie per la visione", "iscriviti", "obrigado por assistir", "inscreva se",
    };
    // Normalisation propre à ce filtre : les accents (octets UTF-8) deviennent des espaces, pour que
    // « merci d'avoir regardé » et « merci davoir regarde » se ressemblent.
    std::string n;
    for (unsigned char c : line) n += (c < 0x80 && std::isalnum(c)) ? (char) std::tolower(c) : ' ';
    std::string compact;  // espaces multiples réduits à un seul
    for (char c : n) {
        if (c == ' ' && (compact.empty() || compact.back() == ' ')) continue;
        compact += c;
    }
    for (const char * p : PATTERNS) {
        if (compact.find(p) != std::string::npos) return true;
    }
    return false;
}

// ------------------------------------------------------------------ identité du morceau

SongIdentity guess_song_identity(const fs::path & audio, double seconds) {
    SongIdentity id;
    id.seconds = seconds;
    // 1. étiquettes du fichier (ID3, Vorbis…), lues par ffprobe : « TAG:artist=… »
    std::wstring cmd = ffprobe_exe() + L" -v error -show_entries format_tags=artist,title,album -of default=nw=1 " + quote_arg(audio.wstring());
    run_command(cmd,
                [&](const std::string & l) {
                    std::string line = trim(l);
                    auto        take = [&](const char * key, std::string & dst) {
                        std::string k = std::string("TAG:") + key + "=";
                        if (line.size() > k.size() && _strnicmp(line.c_str(), k.c_str(), k.size()) == 0 && dst.empty()) dst = trim(line.substr(k.size()));
                    };
                    take("artist", id.artist);
                    take("title", id.title);
                    take("album", id.album);
                },
                nullptr, true);
    if (id.ok()) {
        id.origin = "tags";
        return id;
    }
    // 2. nom du fichier et dossiers : « Groupe - Titre.mp3 », « 08 Titre.mp3 » dans « Groupe\Année.Album\ »
    std::string stem   = narrow(audio.stem().wstring());
    std::string parent = narrow(audio.parent_path().filename().wstring());
    std::string grand  = narrow(audio.parent_path().parent_path().filename().wstring());
    std::string title  = strip_number_prefix(trim(stem), false);
    bool        numbered = title != trim(stem);
    std::string artist;
    size_t      dash = title.find(" - ");
    if (dash != std::string::npos) {  // « Groupe - Titre »
        artist = trim(title.substr(0, dash));
        title  = trim(title.substr(dash + 3));
    }
    std::string album = strip_number_prefix(parent, true);
    bool        album_dir = album != parent || numbered;  // dossier « 1990.Vision Thing » ou pistes numérotées : dossier d'album
    if (artist.empty()) artist = album_dir ? grand : parent;
    if (id.title.empty()) id.title = title;
    if (id.artist.empty()) id.artist = trim(artist);
    if (id.album.empty() && album_dir) id.album = trim(album);
    id.origin = "name";
    if (!id.ok()) {
        id.artist.clear();
        id.title.clear();
    }
    return id;
}

// ------------------------------------------------------------------ recherche

WebLyrics fetch_web_lyrics(const SongIdentity & id, const std::string & whisper_text, const std::atomic<bool> & cancel) {
    WebLyrics out;
    if (!id.ok()) {
        out.status = WebLyricsStatus::NoIdentity;
        return out;
    }
    const std::wstring host = L"lrclib.net";
    std::string        body;
    LrcRecord          best;
    bool               found = false;

    // a) correspondance exacte (groupe + titre + album + durée)
    std::string q = "/api/get?artist_name=" + url_encode(id.artist) + "&track_name=" + url_encode(id.title);
    if (!id.album.empty()) q += "&album_name=" + url_encode(id.album);
    if (id.seconds > 0) q += "&duration=" + std::to_string((int) std::lround(id.seconds));
    int code = https_get(host, widen(q), body);
    if (code == 0) {
        out.status = WebLyricsStatus::Offline;
        return out;
    }
    if (code == 200) {
        if (yyjson_doc * doc = yyjson_read(body.data(), body.size(), 0)) {
            best  = read_record(yyjson_doc_get_root(doc));
            found = !best.plain.empty() || best.instrumental;
            yyjson_doc_free(doc);
        }
    }
    // b) sinon recherche (groupe + titre) : la fiche avec paroles la plus proche en durée
    if (!found && !cancel) {
        q    = "/api/search?artist_name=" + url_encode(id.artist) + "&track_name=" + url_encode(id.title);
        code = https_get(host, widen(q), body);
        if (code == 0) {
            out.status = WebLyricsStatus::Offline;
            return out;
        }
        if (code == 200) {
            if (yyjson_doc * doc = yyjson_read(body.data(), body.size(), 0)) {
                size_t       i, n;
                yyjson_val * item;
                double       best_gap = 1e9;
                yyjson_arr_foreach(yyjson_doc_get_root(doc), i, n, item) {
                    LrcRecord r = read_record(item);
                    if (r.plain.empty()) continue;
                    double gap = (id.seconds > 0 && r.duration > 0) ? std::fabs(r.duration - id.seconds) : 0;
                    if (gap < best_gap) {
                        best_gap = gap;
                        best     = r;
                        found    = true;
                    }
                }
                yyjson_doc_free(doc);
            }
        }
    }
    if (!found || best.plain.empty()) {
        out.status = WebLyricsStatus::NotFound;
        return out;
    }
    out.plain  = best.plain;
    out.source = "LRCLIB · " + (best.artist.empty() ? id.artist : best.artist) + " – " + (best.title.empty() ? id.title : best.title);
    if (!best.album.empty()) out.source += " (" + best.album + ")";

    // c) vérification : les mots entendus par Whisper doivent se retrouver dans les paroles trouvées.
    std::vector<std::string> heard = words(whisper_text);
    std::set<std::string>    known;
    for (const std::string & w : words(best.plain)) known.insert(w);
    size_t total = 0, hit = 0;
    for (const std::string & w : heard) {
        if (w.size() < 3) continue;  // « a », « le », « to »… : peu parlants
        total++;
        hit += known.count(w);
    }
    if (total >= 8) {
        out.match = (double) hit / (double) total;
        // Moins de 25 % de mots communs : probablement pas le même morceau (ou Whisper n'a rien compris).
        // Si Whisper n'a reconnu presque rien de commun mais que le titre vient des étiquettes du fichier,
        // on fait confiance aux étiquettes à partir de 10 %.
        double need = id.origin == "tags" ? 0.10 : 0.25;
        out.status  = out.match >= need ? WebLyricsStatus::Found : WebLyricsStatus::Mismatch;
    } else {
        out.status = WebLyricsStatus::Found;  // Whisper n'a presque rien entendu : rien à comparer
    }
    return out;
}

// ------------------------------------------------------------------ format YuE2

std::string web_lyrics_to_yue2(const std::string & plain, bool intro, bool outro) {
    // 1. découpe en strophes (séparées par une ligne vide) ; on ignore d'éventuelles balises [Chorus] du site.
    std::vector<Stanza> st;
    std::istringstream  in(plain);
    std::string         raw;
    Stanza              cur;
    auto                flush = [&]() {
        if (!cur.lines.empty()) st.push_back(cur);
        cur = Stanza();
    };
    while (std::getline(in, raw)) {
        std::string line = trim(raw);
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.front() == '[' && line.back() == ']') continue;
        cur.lines.push_back(line);
        std::string k = trim(normalize(line));
        if (!k.empty()) cur.keys.insert(k);
    }
    flush();
    if (st.empty()) return "";

    // 2. groupes de strophes identiques ou presque : un groupe vu au moins deux fois = refrain
    int groups = 0;
    for (size_t i = 0; i < st.size(); i++) {
        if (st[i].group >= 0) continue;
        st[i].group = groups;
        for (size_t j = i + 1; j < st.size(); j++) {
            if (st[j].group < 0 && same_stanza(st[i], st[j])) st[j].group = groups;
        }
        groups++;
    }
    std::vector<int> count(groups, 0);
    for (const Stanza & s : st) count[s.group]++;

    // 3. étiquettes : refrain, couplet, ou pont (strophe unique après au moins deux refrains, suivie d'un refrain)
    std::string out;
    if (intro) out += "[Intro]\n\n";
    int choruses_seen = 0;
    for (size_t i = 0; i < st.size(); i++) {
        const Stanza & s   = st[i];
        bool           rep = count[s.group] >= 2;
        std::string    tag = rep ? "Chorus" : "Verse";
        if (!rep && choruses_seen >= 2) {
            bool next_is_chorus = i + 1 < st.size() && count[st[i + 1].group] >= 2;
            if (next_is_chorus || i + 1 == st.size()) tag = "Bridge";
        }
        if (rep) choruses_seen++;
        out += "[" + tag + "]\n";
        for (const std::string & l : s.lines) out += l + "\n";
        out += "\n";
    }
    if (outro) out += "[Outro]\n";
    return out;
}
