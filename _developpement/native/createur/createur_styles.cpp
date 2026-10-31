// Créateur de styles du Studio Audio AI (programme séparé, réservé à l'éditeur, hors distribution).
//
// Fenêtre Win32 + DirectX 11 + Dear ImGui, avec les couleurs et les polices du Studio. Le principe :
//   1. on ajoute une ou plusieurs chansons d'inspiration (bouton ou glisser-déposer) ;
//   2. chacune est analysée comme le « morceau d'inspiration » du Studio (mêmes modèles, même code : analysis.cpp),
//      mais sans les paroles : genres, ambiances, voix, instruments, tempo, tonalité et structure ;
//   3. les balises sont fusionnées : celles qui reviennent dans au moins la moitié des chansons sont cochées,
//      chacune reste cochable ; on peut ajouter ses propres balises ;
//   4. on choisit la structure (la plus fréquente est proposée, modifiable) et on peut l'inclure dans le style,
//      comme les cases du préréglage dans le Studio, et/ou l'enregistrer comme structure séparée ;
//   5. « Ajouter au pack » range le style dans un fichier .yue2pack (nom, auteur, version, description,
//      plusieurs styles et structures), importable dans les Réglages d'un Studio sous licence.
// Depuis la 1.0.22 : balises choisies d'après les scores moyens de toutes les chansons (une famille de genres dominante,
// une seule voix, seuils plus stricts pour les instruments), et genres recoupés avec MusicBrainz (★ = confirmé).
//
// Emplacement : music\createur\Createur de styles.exe (copié là par la compilation, avec les DLL d'ONNX Runtime).
// Le programme retrouve le dossier music en remontant les dossiers (comme le Studio) pour utiliser models\analysis,
// le moteur de partition yue-transcribe.exe et le modèle SheetSage. ffmpeg doit être trouvable (PATH ou dossier).
// Ses réglages (dernier pack ouvert, auteur, dernier dossier) sont dans Admin_tools\Createur_styles\createur_reglages.json (Studio 1.0.44 ; music\createur avant).
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"  // état des champs de texte (sélection, pour le menu clic droit)
#include "misc/cpp/imgui_stdlib.h"
#include "yyjson.h"

#include "analysis.hpp"
#include "lyrics_web.hpp"  // guess_song_identity (groupe et titre d'après le fichier)
#include "net.hpp"         // https_get
#include "studio.hpp"  // Paths, run_command, widen, narrow, other_synth_running
#include "version.hpp"

namespace fs = std::filesystem;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Mise en minuscules (ASCII). Celle d'analysis.cpp est privée à ce fichier : on a la nôtre.
static std::string lower(const std::string & s) {
    std::string o = s;
    for (char & c : o) c = (char) tolower((unsigned char) c);
    return o;
}

// ============================================================================ Petits outils

static fs::path exe_dir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path();
}

static std::string read_file(const fs::path & p) {
    std::FILE * f = _wfopen(p.c_str(), L"rb");
    std::string text;
    if (f) {
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
        fclose(f);
    }
    return text;
}

static bool write_file(const fs::path & p, const std::string & text) {
    std::FILE * f = _wfopen(p.c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return ok;
}

static std::string trim_text(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// « a, b ,c » → { "a", "b", "c" } (éléments vides retirés).
static std::vector<std::string> split_list(const std::string & s) {
    std::vector<std::string> out;
    size_t                   pos = 0;
    while (pos <= s.size()) {
        size_t      end  = s.find(',', pos);
        std::string item = trim_text(s.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        if (!item.empty()) out.push_back(item);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

static std::string join_list(const std::vector<std::string> & items, const char * sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) out += (i ? sep : "") + items[i];
    return out;
}

// Liste de sections « Intro,Verse,Chorus » affichée « Intro > Verse > Chorus ».
static std::string arrow_text(const std::string & sections) {
    return join_list(split_list(sections), " > ");
}

// Sections saisies à la main : crochets retirés, majuscule au début et après un tiret (« pre-chorus » → « Pre-Chorus »).
static std::string normalize_sections(const std::string & text) {
    std::vector<std::string> out;
    for (std::string s : split_list(text)) {
        s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '[' || c == ']'; }), s.end());
        s = trim_text(s);
        if (s.empty()) continue;
        bool up = true;
        for (char & c : s) {
            c  = up ? (char) toupper((unsigned char) c) : (char) tolower((unsigned char) c);
            up = c == '-' || c == ' ';
        }
        out.push_back(s);
    }
    return join_list(out, ",");
}

// Même structure pour un instrumental : balises en minuscules, [interlude] remplacée par [bridge]
// (la LoRA instrumentale ne connaît pas [interlude]), comme dans le Studio.
static std::string instrumental_sections(const std::string & song) {
    std::vector<std::string> out;
    for (std::string s : split_list(song)) {
        s = lower(s);
        if (s == "interlude") s = "bridge";
        out.push_back(s);
    }
    return join_list(out, ",");
}

static bool same_name(const std::string & a, const std::string & b) { return lower(trim_text(a)) == lower(trim_text(b)); }

// ============================================================================ Fichier .yue2pack (même format que le Studio)
//
// { "format": "yue2pack", "version": 1,
//   "pack": { "name": "Rock 80s", "author": "…", "version": "1.0", "description": "…" },
//   "styles": [ { "name", "style", "structure_song", "structure_inst", "negative_style", "negative_scale", "loras" } ],
//   "structures": [ { "name": "…", "sections": "Intro,Verse,Chorus,…" } ] }

struct LoraUse {
    std::string file;
    float       scale = 1.f;
};
struct PackStyle {
    std::string          name, style, structure_song, structure_inst;
    bool                 has_negative = false;  // style à éviter mémorisé (même vide)
    std::string          negative_style;
    float                negative_scale = 1.5f;
    std::vector<LoraUse> loras;  // conservées telles quelles si le pack en contient (le créateur n'en ajoute pas)
};
struct PackStructure {
    std::string name, sections;
};
struct Pack {
    std::string                name, author, version = "1.0", description;
    std::vector<PackStyle>     styles;
    std::vector<PackStructure> structures;
};

static std::string jstr(yyjson_val * obj, const char * key) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string();
}

static bool read_pack(const fs::path & path, Pack & out, std::string & error) {
    std::string  text = read_file(path);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) {
        error = "Fichier illisible ou abîmé.";
        return false;
    }
    yyjson_val * r = yyjson_doc_get_root(doc);
    if (jstr(r, "format") != "yue2pack") {
        yyjson_doc_free(doc);
        error = "Ce fichier n'est pas un pack de Studio Audio AI.";
        return false;
    }
    out             = Pack();
    yyjson_val * pi = yyjson_obj_get(r, "pack");
    out.name        = jstr(pi, "name");
    out.author      = jstr(pi, "author");
    out.version     = jstr(pi, "version");
    out.description = jstr(pi, "description");
    size_t       i, n;
    yyjson_val * item;
    yyjson_arr_foreach(yyjson_obj_get(r, "styles"), i, n, item) {
        PackStyle s;
        s.name           = jstr(item, "name");
        s.style          = jstr(item, "style");
        s.structure_song = jstr(item, "structure_song");
        s.structure_inst = jstr(item, "structure_inst");
        yyjson_val * neg = yyjson_obj_get(item, "negative_style");
        if (yyjson_is_str(neg)) {
            s.has_negative   = true;
            s.negative_style = yyjson_get_str(neg);
            yyjson_val * sc  = yyjson_obj_get(item, "negative_scale");
            if (yyjson_is_num(sc)) s.negative_scale = (float) yyjson_get_num(sc);
        }
        size_t       j, m;
        yyjson_val * lo;
        yyjson_arr_foreach(yyjson_obj_get(item, "loras"), j, m, lo) {
            yyjson_val * file = yyjson_obj_get(lo, "file");
            yyjson_val * sc   = yyjson_obj_get(lo, "scale");
            if (yyjson_is_str(file)) s.loras.push_back({ yyjson_get_str(file), yyjson_is_num(sc) ? (float) yyjson_get_num(sc) : 1.f });
        }
        if (!s.name.empty()) out.styles.push_back(std::move(s));
    }
    yyjson_arr_foreach(yyjson_obj_get(r, "structures"), i, n, item) {
        PackStructure st{ jstr(item, "name"), jstr(item, "sections") };
        if (!st.name.empty() && !st.sections.empty()) out.structures.push_back(std::move(st));
    }
    yyjson_doc_free(doc);
    return true;
}

static bool write_pack(const fs::path & path, const Pack & p) {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_str(doc, o, "format", "yue2pack");
    yyjson_mut_obj_add_int(doc, o, "version", 1);
    yyjson_mut_val * pi = yyjson_mut_obj_add_obj(doc, o, "pack");
    yyjson_mut_obj_add_strcpy(doc, pi, "name", p.name.c_str());
    yyjson_mut_obj_add_strcpy(doc, pi, "author", p.author.c_str());
    yyjson_mut_obj_add_strcpy(doc, pi, "version", p.version.c_str());
    yyjson_mut_obj_add_strcpy(doc, pi, "description", p.description.c_str());
    yyjson_mut_val * sa = yyjson_mut_obj_add_arr(doc, o, "styles");
    for (const PackStyle & s : p.styles) {
        yyjson_mut_val * item = yyjson_mut_arr_add_obj(doc, sa);
        yyjson_mut_obj_add_strcpy(doc, item, "name", s.name.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "style", s.style.c_str());
        if (!s.structure_song.empty()) yyjson_mut_obj_add_strcpy(doc, item, "structure_song", s.structure_song.c_str());
        if (!s.structure_inst.empty()) yyjson_mut_obj_add_strcpy(doc, item, "structure_inst", s.structure_inst.c_str());
        if (s.has_negative) {
            yyjson_mut_obj_add_strcpy(doc, item, "negative_style", s.negative_style.c_str());
            yyjson_mut_obj_add_real(doc, item, "negative_scale", s.negative_scale);
        }
        if (!s.loras.empty()) {
            yyjson_mut_val * la = yyjson_mut_obj_add_arr(doc, item, "loras");
            for (const LoraUse & u : s.loras) {
                yyjson_mut_val * lo = yyjson_mut_arr_add_obj(doc, la);
                yyjson_mut_obj_add_strcpy(doc, lo, "file", u.file.c_str());
                yyjson_mut_obj_add_real(doc, lo, "scale", u.scale);
            }
        }
    }
    yyjson_mut_val * ta = yyjson_mut_obj_add_arr(doc, o, "structures");
    for (const PackStructure & st : p.structures) {
        yyjson_mut_val * item = yyjson_mut_arr_add_obj(doc, ta);
        yyjson_mut_obj_add_strcpy(doc, item, "name", st.name.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "sections", st.sections.c_str());
    }
    size_t len  = 0;
    char * json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);
    bool   ok   = json && write_file(path, std::string(json, len));
    free(json);
    yyjson_mut_doc_free(doc);
    return ok;
}

// ============================================================================ Réglages du créateur

struct CreatorSettings {
    std::string last_pack;   // dernier pack ouvert (rouvert au lancement)
    std::string author;      // auteur proposé pour un nouveau pack
    std::string audio_dir;   // dernier dossier de chansons
    bool        use_web = true;  // vérifier les genres sur MusicBrainz
};

static fs::path settings_path() { return exe_dir() / L"createur_reglages.json"; }

static CreatorSettings load_settings() {
    CreatorSettings s;
    std::string     text = read_file(settings_path());
    yyjson_doc *    doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return s;
    yyjson_val * r = yyjson_doc_get_root(doc);
    s.last_pack    = jstr(r, "last_pack");
    s.author       = jstr(r, "author");
    s.audio_dir    = jstr(r, "audio_dir");
    yyjson_val * w = yyjson_obj_get(r, "use_web");
    if (yyjson_is_bool(w)) s.use_web = yyjson_get_bool(w);
    yyjson_doc_free(doc);
    return s;
}

static void save_settings(const CreatorSettings & s) {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_strcpy(doc, o, "last_pack", s.last_pack.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "author", s.author.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "audio_dir", s.audio_dir.c_str());
    yyjson_mut_obj_add_bool(doc, o, "use_web", s.use_web);
    size_t len  = 0;
    char * json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);
    if (json) write_file(settings_path(), std::string(json, len));
    free(json);
    yyjson_mut_doc_free(doc);
}

// ============================================================================ Boîtes de dialogue Windows

static HWND g_hwnd = nullptr;

static bool is_audio_file(const fs::path & p) {
    std::string ext = lower(narrow(p.extension().wstring()));
    for (const char * e : { ".mp3", ".wav", ".flac", ".ogg", ".m4a", ".opus", ".aac", ".wma", ".aiff", ".aif" })
        if (ext == e) return true;
    return false;
}

// Choix de plusieurs chansons. Renvoie les chemins complets (vide si annulé).
static std::vector<fs::path> pick_audio_files(const std::string & start_dir) {
    std::vector<wchar_t> buf(64 * 1024, 0);
    std::wstring         dir = widen(start_dir);
    OPENFILENAMEW        ofn{ sizeof(ofn) };
    ofn.hwndOwner       = g_hwnd;
    ofn.lpstrFilter     = L"Fichiers audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.opus;*.aac;*.wma;*.aiff;*.aif\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile       = buf.data();
    ofn.nMaxFile        = (DWORD) buf.size();
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrTitle      = L"Chansons d'inspiration";
    ofn.Flags           = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    std::vector<fs::path> out;
    if (!GetOpenFileNameW(&ofn)) return out;
    // Un seul fichier : chemin complet. Plusieurs : dossier, puis les noms, séparés par des zéros.
    std::wstring first = buf.data();
    const wchar_t * p  = buf.data() + first.size() + 1;
    if (!*p) {
        out.push_back(first);
    } else {
        for (; *p; p += wcslen(p) + 1) out.push_back(fs::path(first) / p);
    }
    return out;
}

// Choix d'un pack : save = vrai pour « Nouveau pack » (fichier à créer), faux pour « Ouvrir ».
static fs::path pick_pack(bool save, const fs::path & start_dir) {
    wchar_t       file[MAX_PATH] = L"";
    std::wstring  dir            = start_dir.wstring();
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner       = g_hwnd;
    ofn.lpstrFilter     = L"Pack de styles (*.yue2pack)\0*.yue2pack\0";
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt     = L"yue2pack";
    ofn.Flags           = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    ofn.lpstrTitle      = save ? L"Nouveau pack de styles" : L"Ouvrir un pack de styles";
    BOOL ok             = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? fs::path(file) : fs::path();
}

// ============================================================================ Genres sur Internet (MusicBrainz)
//
// Le modèle d'analyse n'écoute que le son et confond souvent des genres voisins ; il ne sait pas quel groupe joue.
// On identifie donc le morceau (étiquettes du fichier, sinon son nom et ses dossiers, comme pour les paroles du Studio)
// et on demande à MusicBrainz (base libre, sans compte) les genres votés par ses utilisateurs pour le morceau, son album
// et le groupe. Règles du site : au plus une requête par seconde, avec un nom d'application (WinHttp l'envoie).
// Hors ligne ou morceau inconnu : liste vide, l'analyse seule décide.

struct WebGenre {
    std::string name;       // genre en minuscules (« hard rock »)
    float       score = 0;  // 0 à 1 : part des votes, moyennée sur le morceau, l'album et le groupe
};

struct WebResult {
    std::string           artist, title;  // identité retenue
    std::string           status;         // compte rendu court (« MusicBrainz : 6 genres », « groupe non identifié »…)
    std::vector<WebGenre> genres;         // triés par score décroissant
};

static std::string uri_encode(const std::string & s) {
    static const char * hexd = "0123456789ABCDEF";
    std::string         out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char) c;
        else {
            out += '%';
            out += hexd[c >> 4];
            out += hexd[c & 15];
        }
    }
    return out;
}

// Requête à musicbrainz.org (une par seconde au plus). Renvoie le document JSON (nullptr si échec ; à libérer).
static yyjson_doc * mb_get(const std::string & path) {
    static ULONGLONG last = 0;
    std::string      body;
    int              code = 0;
    for (int attempt = 0; attempt < 3; attempt++) {  // 503 = site surchargé ou trop de requêtes : on réessaie un peu plus tard
        ULONGLONG wait = attempt == 0 ? 1100 : 2500;
        ULONGLONG now  = GetTickCount64();
        if (now - last < wait) Sleep((DWORD) (wait - (now - last)));
        code = https_get(L"musicbrainz.org", widen(path), body);
        last = GetTickCount64();
        if (code != 503) break;
    }
    if (code != 200) return nullptr;
    return yyjson_read(body.data(), body.size(), 0);
}

// Genres « name / count » d'un objet MusicBrainz, normalisés par le plus voté (le plus voté vaut 1).
static std::map<std::string, float> mb_genres(yyjson_val * obj) {
    std::map<std::string, float> out;
    float                        top = 0;
    size_t                       i, n;
    yyjson_val *                 g;
    yyjson_arr_foreach(yyjson_obj_get(obj, "genres"), i, n, g) {
        yyjson_val * c = yyjson_obj_get(g, "count");
        float        v = yyjson_is_num(c) ? (float) yyjson_get_num(c) : 1.f;
        std::string  name = lower(jstr(g, "name"));
        if (name.empty() || v <= 0) continue;
        out[name] = v;
        top       = std::max(top, v);
    }
    for (auto & kv : out) kv.second /= top;
    return out;
}

// Genres d'un objet par son identifiant (« recording », « release-group », « artist »).
static std::map<std::string, float> mb_lookup(const char * kind, const std::string & id) {
    std::map<std::string, float> out;
    if (id.empty()) return out;
    yyjson_doc * doc = mb_get(std::string("/ws/2/") + kind + "/" + id + "?inc=genres&fmt=json");
    if (doc) {
        out = mb_genres(yyjson_doc_get_root(doc));
        yyjson_doc_free(doc);
    }
    return out;
}

// Texte d'une requête de recherche Lucene : guillemets retirés (ils délimitent la valeur).
static std::string mb_quote(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
    return "\"" + s + "\"";
}

// Cache des genres d'un groupe (plusieurs chansons du même groupe : une seule requête). Utilisé par le fil d'analyse seul.
static std::map<std::string, std::pair<std::string, std::map<std::string, float>>> g_artist_cache;  // nom → (id, genres)

static WebResult fetch_web_genres(const fs::path & audio, double seconds, const std::atomic<bool> & cancel) {
    WebResult    r;
    SongIdentity id = guess_song_identity(audio, seconds);
    r.artist        = id.artist;
    r.title         = id.title;
    if (id.artist.empty()) {
        r.status = "groupe non identifié (étiquettes du fichier ou nom « Groupe - Titre »)";
        return r;
    }
    std::string artist_id, recording_id, group_id;
    // 1. le morceau (groupe + titre) : identifiants du morceau, du groupe et de l'album
    if (!id.title.empty() && !cancel) {
        std::string  q   = "recording:" + mb_quote(id.title) + " AND artist:" + mb_quote(id.artist);
        yyjson_doc * doc = mb_get("/ws/2/recording/?query=" + uri_encode(q) + "&fmt=json&limit=5");
        if (!doc) {
            r.status = "MusicBrainz injoignable (hors ligne ?)";
            return r;
        }
        size_t       i, n;
        yyjson_val * rec;
        yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc), "recordings"), i, n, rec) {
            yyjson_val * sc = yyjson_obj_get(rec, "score");
            if (!yyjson_is_num(sc) || yyjson_get_num(sc) < 85) continue;
            recording_id      = jstr(rec, "id");
            yyjson_val * ac   = yyjson_arr_get_first(yyjson_obj_get(rec, "artist-credit"));
            artist_id         = jstr(yyjson_obj_get(ac, "artist"), "id");
            yyjson_val * rel  = yyjson_arr_get_first(yyjson_obj_get(rec, "releases"));
            group_id          = jstr(yyjson_obj_get(rel, "release-group"), "id");
            break;
        }
        yyjson_doc_free(doc);
    }
    // 2. sinon, le groupe seul
    std::string akey = lower(id.artist);
    auto        hit  = g_artist_cache.find(akey);
    if (artist_id.empty() && hit == g_artist_cache.end() && !cancel) {
        yyjson_doc * doc = mb_get("/ws/2/artist/?query=" + uri_encode("artist:" + mb_quote(id.artist)) + "&fmt=json&limit=1");
        if (!doc) {
            r.status = "MusicBrainz injoignable (hors ligne ?)";
            return r;
        }
        yyjson_val * a  = yyjson_arr_get_first(yyjson_obj_get(yyjson_doc_get_root(doc), "artists"));
        yyjson_val * sc = yyjson_obj_get(a, "score");
        if (yyjson_is_num(sc) && yyjson_get_num(sc) >= 90) artist_id = jstr(a, "id");
        yyjson_doc_free(doc);
    }
    // 3. genres votés : morceau, album, groupe (le groupe est gardé en cache)
    std::vector<std::map<std::string, float>> sources;
    std::vector<std::string>                  used;
    if (!cancel) {
        auto m = mb_lookup("recording", recording_id);
        if (!m.empty()) { sources.push_back(m); used.push_back("morceau"); }
    }
    if (!cancel) {
        auto m = mb_lookup("release-group", group_id);
        if (!m.empty()) { sources.push_back(m); used.push_back("album"); }
    }
    if (!cancel) {
        std::map<std::string, float> m;
        if (hit != g_artist_cache.end() && (artist_id.empty() || hit->second.first == artist_id)) m = hit->second.second;
        else if (!artist_id.empty()) {
            m                    = mb_lookup("artist", artist_id);
            g_artist_cache[akey] = { artist_id, m };
        }
        if (!m.empty()) { sources.push_back(m); used.push_back("groupe"); }
    }
    if (sources.empty()) {
        r.status = artist_id.empty() && recording_id.empty() ? "inconnu de MusicBrainz" : "aucun genre voté sur MusicBrainz";
        return r;
    }
    std::map<std::string, float> sum;
    for (const auto & m : sources)
        for (const auto & kv : m) sum[kv.first] += kv.second / sources.size();
    for (const auto & kv : sum)
        if (kv.second >= 0.15f) r.genres.push_back({ kv.first, kv.second });
    std::sort(r.genres.begin(), r.genres.end(), [](const WebGenre & a, const WebGenre & b) { return a.score > b.score; });
    if (r.genres.size() > 8) r.genres.resize(8);
    r.status = "MusicBrainz (" + join_list(used, ", ") + ") : " + std::to_string(r.genres.size()) + " genre(s)";
    return r;
}

// Un genre détecté est-il confirmé par un genre du web ? Égalité, ou l'un contient l'autre en mots entiers
// (« hard rock » confirme « rock » et inversement ; « rock » ne confirme pas « rockabilly »).
static bool genre_matches(const std::string & detected, const std::string & web) {
    std::string a = " " + lower(detected) + " ", b = " " + lower(web) + " ";
    for (std::string * x : { &a, &b })
        for (char & c : *x)
            if (c == '-' || c == '/') c = ' ';
    return a == b || a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

// ============================================================================ Analyse des chansons (en arrière-plan)

// Une chanson d'inspiration et l'état de son analyse.
struct SongItem {
    fs::path          file;
    std::string       name;              // nom du fichier, pour l'affichage
    std::atomic<int>  state{ 0 };        // 0 en attente, 1 en cours, 2 analysée, 3 erreur, 4 annulée
    std::atomic<bool> cancel{ false };
    std::mutex        mutex;             // protège stage et progress
    std::string       stage;
    float             progress = 0;
    // Résultats (écrits par le fil d'analyse avant state = 2, lus ensuite seulement)
    AudioAnalysis           result;
    std::vector<Suggestion> sugg;
    std::string             sections;  // structure détectée « Intro,Verse,… » (vide si moins de 3 sections)
    std::string             error;
    WebResult               web;       // genres votés sur MusicBrainz (vide hors ligne ou morceau inconnu)
};
using SongPtr = std::shared_ptr<SongItem>;

// File d'analyse : un seul fil, une chanson à la fois (la partition utilise la carte graphique).
// Alloué une fois pour toutes (jamais détruit) : le fil peut finir après la fermeture de la fenêtre sans danger.
struct Analyzer {
    std::mutex          mutex;
    std::deque<SongPtr> queue;
    bool                running = false;
    fs::path            models, transcribe, sheetsage, work;
    std::atomic<bool>   use_web{ true };  // case « Vérifier les genres sur MusicBrainz »

    void push(const SongPtr & s) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(s);
        if (!running) {
            running = true;
            std::thread([this] { loop(); }).detach();
        }
    }

    void loop() {
        for (;;) {
            SongPtr s;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (queue.empty()) {
                    running = false;
                    return;
                }
                s = queue.front();
                queue.pop_front();
            }
            if (s->cancel) {
                s->state = 4;
                continue;
            }
            run(*s);
        }
    }

    void run(SongItem & s) {
        s.state       = 1;
        auto progress = [&](const std::string & what, float f) {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.stage    = what;
            s.progress = f;
        };
        // Le Studio génère un morceau : on attend qu'il ait fini (la carte graphique ne suffirait pas aux deux).
        while (other_synth_running() && !s.cancel) {
            progress("wait", 0.f);
            Sleep(2000);
        }
        if (s.cancel) {
            s.state = 4;
            return;
        }
        AudioAnalysis out;
        // Pas de chemins Whisper : pas de paroles, seulement le style et la structure.
        bool ok = analyze_audio(s.file, models, work, transcribe, sheetsage, fs::path(), fs::path(), progress, s.cancel, out);
        if (ok) {
            s.sugg = build_suggestions(out);
            std::vector<std::string> secs = suggested_structure(out, false);
            if (secs.size() >= 3) s.sections = join_list(secs, ",");
            if (use_web && !s.cancel) {
                progress("web", 0.98f);
                s.web = fetch_web_genres(s.file, out.duration, s.cancel);
            }
        } else if (!s.cancel) {
            s.error = out.error == "decode" ? "fichier illisible (ffmpeg introuvable ?)"
                      : out.error == "short" ? "morceau trop court"
                                             : (out.error.empty() ? "analyse impossible" : out.error);
        }
        s.result = std::move(out);
        s.state  = ok ? 2 : (s.cancel ? 4 : 3);
    }
};
static Analyzer * g_analyzer = new Analyzer;

// Étape d'analyse (texte court du moteur) en français.
static const char * stage_text(const std::string & stage) {
    if (stage == "wait") return "en attente : le Studio génère un morceau";
    if (stage == "decode") return "lecture du fichier";
    if (stage == "mel") return "spectrogramme";
    if (stage == "style") return "styles et instruments";
    if (stage == "tags") return "ambiance et voix";
    if (stage == "score") return "partition : tempo, tonalité, structure";
    if (stage == "web") return "genres sur MusicBrainz";
    if (stage == "done") return "terminé";
    return "analyse";
}

// ============================================================================ Direct3D

static ID3D11Device *           g_device    = nullptr;
static ID3D11DeviceContext *    g_context   = nullptr;
static IDXGISwapChain *         g_swapchain = nullptr;
static ID3D11RenderTargetView * g_rtv       = nullptr;
static UINT                     g_resize_w = 0, g_resize_h = 0;
static float                    g_dpi = 1.f;  // échelle d'affichage (DPI du moniteur)
static std::vector<std::wstring> g_dropped;   // fichiers glissés sur la fenêtre

static void create_render_target() {
    ID3D11Texture2D * back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

static void cleanup_render_target() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

static bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[]      = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL       level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                               &g_swapchain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {  // pas de carte compatible : rendu logiciel
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_swapchain, &g_device, &level, &g_context);
    }
    if (hr != S_OK) return false;
    create_render_target();
    return true;
}

static void cleanup_device() {
    cleanup_render_target();
    if (g_swapchain) g_swapchain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                g_resize_w = LOWORD(lparam);
                g_resize_h = HIWORD(lparam);
            }
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO *) lparam)->ptMinTrackSize = { (LONG) (1040 * g_dpi), (LONG) (680 * g_dpi) };
            return 0;
        case WM_DROPFILES: {
            HDROP drop = (HDROP) wparam;
            UINT  n    = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; i++) {
                wchar_t file[MAX_PATH];
                if (DragQueryFileW(drop, i, file, MAX_PATH)) g_dropped.push_back(file);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) return 0;  // la touche Alt n'ouvre pas de menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ============================================================================ Thème (repris du Studio)

struct Palette {
    ImVec4 bg, panel, card, text, muted, line, accent, accent_ink, accent_soft, danger, ok, chip, warn;
    ImVec4 blue, violet, teal, amber, pink, orange, green;
};
static Palette  P;
static ImFont * g_bold   = nullptr;
static ImFont * g_header = nullptr;

static ImVec4 hex(unsigned rgb, float a = 1.f) {
    return ImVec4(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}
static ImVec4 alpha(const ImVec4 & c, float a) { return ImVec4(c.x, c.y, c.z, a); }
// Mélange d'une couleur dans le fond : teinte légère des cartes.
static ImVec4 tint(const ImVec4 & base, const ImVec4 & color, float amount) {
    return ImVec4(base.x + (color.x - base.x) * amount, base.y + (color.y - base.y) * amount, base.z + (color.z - base.z) * amount, 1.f);
}

static bool windows_light_theme() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

// Thème choisi dans le Studio (clé « theme » de music\donnees\studio_reglages.json, ou music\studio_reglages.json avant
// la 1.0.30) ; 0 = Minuit violet, qui suit Windows.
static int studio_theme(const fs::path & music) {
    std::string text = read_file(music / L"config" / L"reglages.json");  // Studio 1.0.41
    if (text.empty()) text = read_file(music / L"donnees" / L"studio_reglages.json");
    if (text.empty()) text = read_file(music / L"studio_reglages.json");
    size_t      p    = text.find("\"theme\"");
    if (p == std::string::npos) return 0;
    p = text.find(':', p);
    if (p == std::string::npos) return 0;
    int t = atoi(text.c_str() + p + 1);
    return t >= 0 && t <= 3 ? t : 0;
}

// Mêmes palettes et mêmes formes que le Studio (main.cpp, apply_theme).
static void apply_theme(int theme, bool light) {
    if (theme == 1) {  // Studio gris
        P = { hex(0x1a1b1e), hex(0x232529), hex(0x2b2d32), hex(0xe4e6ea), hex(0x8e939c), hex(0x383b42),
              hex(0xffb000), hex(0x241a00), hex(0x3a3320), hex(0xff6b5e), hex(0x7ddf64), hex(0x30333a), hex(0xffb000),
              hex(0x60a5fa), hex(0xa78bfa), hex(0x2dd4bf), hex(0xfbbf24), hex(0xf472b6), hex(0xfb923c), hex(0x34d399) };
    } else if (theme == 2) {  // Papier encre
        P = { hex(0xf3f1ee), hex(0xffffff), hex(0xfbfaf8), hex(0x1d1c1a), hex(0x6b6862), hex(0xe0dcd4),
              hex(0x1d4ed8), hex(0xffffff), hex(0xe3eafc), hex(0xc2372f), hex(0x15803d), hex(0xefece6), hex(0xb45309),
              hex(0x2563eb), hex(0x6d5ce7), hex(0x0d9488), hex(0xd97706), hex(0xdb2777), hex(0xea580c), hex(0x059669) };
    } else if (theme == 3) {  // Menthe clair
        P = { hex(0xeef6f3), hex(0xffffff), hex(0xf6fbf9), hex(0x14231e), hex(0x5a7068), hex(0xd5e6df),
              hex(0x0d9488), hex(0xffffff), hex(0xd6f1ec), hex(0xc2372f), hex(0x059669), hex(0xe5f1ed), hex(0xb45309),
              hex(0x2563eb), hex(0x6d5ce7), hex(0x0891b2), hex(0xd97706), hex(0xdb2777), hex(0xea580c), hex(0x15803d) };
    } else if (light) {  // Minuit violet, version claire (Windows en mode clair)
        P = { hex(0xf3f1ee), hex(0xffffff), hex(0xfbfaf8), hex(0x1d1c1a), hex(0x6b6862), hex(0xe3e0da),
              hex(0x5b4bdb), hex(0xffffff), hex(0xece9fd), hex(0xc2372f), hex(0x1f8a4c), hex(0xf0eee9), hex(0xb45309),
              hex(0x2563eb), hex(0x6d5ce7), hex(0x0d9488), hex(0xd97706), hex(0xdb2777), hex(0xea580c), hex(0x059669) };
    } else {  // Minuit violet (sombre)
        P = { hex(0x121215), hex(0x1b1b20), hex(0x222229), hex(0xecebe8), hex(0x9b9891), hex(0x2e2e36),
              hex(0x8f83ff), hex(0x12111a), hex(0x2a2745), hex(0xff6b5e), hex(0x4cc983), hex(0x27272e), hex(0xfbbf24),
              hex(0x60a5fa), hex(0xa78bfa), hex(0x2dd4bf), hex(0xfbbf24), hex(0xf472b6), hex(0xfb923c), hex(0x34d399) };
    }
    float child_r = 12, frame_r = 8, popup_r = 10, pad_x = 10, pad_y = 6, item_y = 7;
    if (theme == 1) { child_r = 4; frame_r = 3; popup_r = 4; pad_x = 8; pad_y = 4; item_y = 5; }
    if (theme == 2) { child_r = 14; frame_r = 9; popup_r = 12; }
    ImGuiStyle & s      = ImGui::GetStyle();
    s                   = ImGuiStyle();
    s.WindowRounding    = 0;
    s.ChildRounding     = child_r;
    s.FrameRounding     = frame_r;
    s.PopupRounding     = popup_r;
    s.GrabRounding      = frame_r;
    s.ScrollbarRounding = frame_r;
    s.WindowPadding     = ImVec2(16, 14);
    s.FramePadding      = ImVec2(pad_x, pad_y);
    s.ItemSpacing       = ImVec2(8, item_y);
    s.CellPadding       = ImVec2(8, 5);
    s.ScrollbarSize     = 12;
    s.ChildBorderSize   = 1;
    s.FrameBorderSize   = 1;
    s.WindowBorderSize  = 0;
    s.PopupBorderSize   = 1;
    s.ScaleAllSizes(g_dpi);

    ImVec4 * c                       = s.Colors;
    c[ImGuiCol_WindowBg]             = P.bg;
    c[ImGuiCol_ChildBg]              = P.panel;
    c[ImGuiCol_PopupBg]              = P.panel;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.45f);
    c[ImGuiCol_Border]               = P.line;
    c[ImGuiCol_Text]                 = P.text;
    c[ImGuiCol_TextDisabled]         = P.muted;
    c[ImGuiCol_FrameBg]              = P.bg;
    c[ImGuiCol_FrameBgHovered]       = P.bg;
    c[ImGuiCol_FrameBgActive]        = P.bg;
    c[ImGuiCol_Button]               = P.chip;
    c[ImGuiCol_ButtonHovered]        = P.accent_soft;
    c[ImGuiCol_ButtonActive]         = P.accent_soft;
    c[ImGuiCol_Header]               = P.chip;
    c[ImGuiCol_HeaderHovered]        = P.accent_soft;
    c[ImGuiCol_HeaderActive]         = P.accent_soft;
    c[ImGuiCol_CheckMark]            = P.accent;
    c[ImGuiCol_SliderGrab]           = P.accent;
    c[ImGuiCol_SliderGrabActive]     = P.accent;
    c[ImGuiCol_PlotHistogram]        = P.accent;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = P.line;
    c[ImGuiCol_ScrollbarGrabHovered] = P.muted;
    c[ImGuiCol_ScrollbarGrabActive]  = P.muted;
    c[ImGuiCol_TextSelectedBg]       = alpha(P.accent, 0.35f);
    c[ImGuiCol_NavCursor]            = P.accent;
    c[ImGuiCol_Separator]            = P.line;
    c[ImGuiCol_TableHeaderBg]        = tint(P.panel, P.accent, 0.10f);
    c[ImGuiCol_TableBorderStrong]    = P.line;
    c[ImGuiCol_TableBorderLight]     = alpha(P.line, 0.6f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = alpha(P.chip, 0.45f);
}

// Polices Windows du Studio (Segoe UI), avec les symboles de Segoe UI Symbol.
static void build_fonts() {
    ImGuiIO &   io    = ImGui::GetIO();
    std::string fonts = "C:\\Windows\\Fonts\\";
    wchar_t     windir[MAX_PATH];
    if (GetWindowsDirectoryW(windir, MAX_PATH)) fonts = narrow(windir) + "\\Fonts\\";
    static const ImWchar text_ranges[]   = { 0x0020, 0x017F, 0x2000, 0x206F, 0x20AC, 0x20AC, 0 };  // latin, ponctuation, €
    static const ImWchar symbol_ranges[] = { 0x2190, 0x21FF, 0x25A0, 0x25FF, 0x2605, 0x2605, 0x266A, 0x266B, 0x2713, 0x2717, 0 };  // flèches, formes, étoile, notes, coches
    auto add = [&](const char * file, float size) -> ImFont * {
        ImFont * f = io.Fonts->AddFontFromFileTTF((fonts + file).c_str(), size, nullptr, text_ranges);
        if (!f) return nullptr;
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF((fonts + "seguisym.ttf").c_str(), size, &merge, symbol_ranges);
        return f;
    };
    float size = std::round(16.f * g_dpi);
    if (!add("segoeui.ttf", size)) io.Fonts->AddFontDefault();
    g_bold   = add("segoeuib.ttf", size);
    g_header = add("segoeuib.ttf", std::round(21.f * g_dpi));
    if (!g_bold) g_bold = io.Fonts->Fonts[0];
    if (!g_header) g_header = io.Fonts->Fonts[0];
}

// ============================================================================ Petits éléments d'interface (comme dans le Studio)

static bool accent_button(const char * text, const ImVec2 & size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, P.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(P.accent, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(P.accent, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text, P.accent_ink);
    bool clicked = ImGui::Button(text, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

static bool soft_button(const char * text, const ImVec4 & color, bool compact = true) {
    ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool clicked = compact ? ImGui::SmallButton(text) : ImGui::Button(text);
    ImGui::PopStyleColor(4);
    return clicked;
}

// Petite croix de suppression (même allure partout).
static bool cross_button(const char * id) {
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(P.danger, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(P.danger, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    bool clicked = ImGui::SmallButton("×");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return clicked;
}

static void chip(const char * text, const ImVec4 & color) {
    ImVec2       ts  = ImGui::CalcTextSize(text);
    ImVec2       pad = ImVec2(8 * g_dpi, 2 * g_dpi);
    ImVec2       p   = ImGui::GetCursorScreenPos();
    ImDrawList * dl  = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ts + pad * 2, ImGui::GetColorU32(alpha(color, 0.16f)), 99);
    dl->AddText(p + pad, ImGui::GetColorU32(color), text);
    ImGui::Dummy(ts + pad * 2);
}

static void tooltip(const char * text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Menu clic droit d'un champ de texte (repris du Studio) : Couper, Copier, Coller, Copier tout, Effacer tout.
// À appeler juste après le champ ; renvoie vrai si le texte a changé.
struct TextMenu {
    ImGuiID id = 0;
    int     a = 0, b = 0;  // sélection au moment du clic (octets UTF-8)
};
static TextMenu g_text_menu;

static bool text_context_menu(std::string & value) {
    ImGuiID id      = ImGui::GetItemID();
    bool    changed = false;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        int                   a  = (int) value.size(), b = a;
        ImGuiInputTextState * st = ImGui::GetInputTextState(id);
        if (st && ImGui::GetActiveID() == id) {
            a = std::clamp(st->GetSelectionStart(), 0, (int) value.size());
            b = std::clamp(st->GetSelectionEnd(), 0, (int) value.size());
            if (a > b) std::swap(a, b);
            if (a == b) a = b = std::clamp(st->GetCursorPos(), 0, (int) value.size());
        }
        g_text_menu = { id, a, b };
        ImGui::OpenPopup("##menu_texte");
    }
    if (g_text_menu.id == id && ImGui::BeginPopup("##menu_texte")) {
        int          a = std::min(g_text_menu.a, (int) value.size()), b = std::min(g_text_menu.b, (int) value.size());
        const char * clip     = ImGui::GetClipboardText();
        bool         has_clip = clip && *clip;
        auto         edit     = [&](const std::string & insert) {
            ImGui::ClearActiveID();  // le champ relira la chaîne modifiée
            value.replace(a, b - a, insert);
            changed = true;
        };
        if (ImGui::MenuItem("Couper", "Ctrl+X", false, a < b)) {
            ImGui::SetClipboardText(value.substr(a, b - a).c_str());
            edit("");
        }
        if (ImGui::MenuItem("Copier", "Ctrl+C", false, a < b)) ImGui::SetClipboardText(value.substr(a, b - a).c_str());
        if (ImGui::MenuItem("Coller", "Ctrl+V", false, has_clip)) edit(clip);
        ImGui::Separator();
        if (ImGui::MenuItem("Copier tout", nullptr, false, !value.empty())) ImGui::SetClipboardText(value.c_str());
        if (ImGui::MenuItem("Effacer tout", nullptr, false, !value.empty())) {
            a = 0;
            b = (int) value.size();
            edit("");
        }
        ImGui::EndPopup();
    }
    return changed;
}

// Carte : fond teinté, bordure et barre colorée à gauche (comme les cartes des Réglages du Studio).
static std::vector<ImVec4> g_card_colors;
static void begin_card(const char * id, const ImVec4 & color) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.card, color, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * g_dpi, 12 * g_dpi));
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    g_card_colors.push_back(color);
}
static void end_card() {
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(5 * g_dpi, ws.y), ImGui::GetColorU32(g_card_colors.back()),
                                              ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
    g_card_colors.pop_back();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

static void card_title(const char * title, const ImVec4 & color, const char * hint = nullptr) {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(color, "%s", title);
    ImGui::PopFont();
    if (hint) {
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", hint);
    }
}

// Message : vert (réussite), rouge (erreur) ou ambre (avertissement).
struct Note {
    std::string text;
    int         kind = 0;  // 0 réussite, 1 erreur, 2 avertissement
    void        draw() const {
        if (text.empty()) return;
        ImVec4 c = kind == 0 ? P.ok : kind == 1 ? P.danger : P.warn;
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    }
};

// Structures types du Studio (mêmes listes que dans le bloc Paroles), pour partir d'une base connue.
struct BuiltinStructure {
    const char * name;
    const char * sections;
};
static const BuiltinStructure BUILTIN_STRUCTURES[] = {
    { "Couplet / refrain (ABAB)", "Intro,Verse,Chorus,Verse,Chorus,Outro" },
    { "Pop avec pont (ABABCB)", "Intro,Verse,Chorus,Verse,Chorus,Bridge,Chorus,Outro" },
    { "Pop avec pré-refrain (ABCABCDC)", "Intro,Verse,Pre-Chorus,Chorus,Verse,Pre-Chorus,Chorus,Bridge,Chorus,Outro" },
    { "Refrain d'ouverture", "Intro,Chorus,Verse,Chorus,Verse,Chorus,Outro" },
    { "Double refrain final", "Intro,Verse,Chorus,Verse,Chorus,Bridge,Chorus,Chorus,Outro" },
    { "Ballade", "Intro,Verse,Verse,Chorus,Verse,Chorus,Bridge,Chorus,Outro" },
    { "Classique AABA (32 mesures)", "Intro,Verse,Verse,Bridge,Verse,Outro" },
    { "Strophique (AAA)", "Intro,Verse,Verse,Verse,Outro" },
    { "Rock avec solo", "Intro,Verse,Chorus,Verse,Chorus,Interlude,Bridge,Chorus,Outro" },
    { "Électro montée / drop", "Intro,Verse,Pre-Chorus,Chorus,Interlude,Pre-Chorus,Chorus,Outro" },
    { "Court (≈ 1 min)", "Intro,Verse,Chorus,Outro" },
};

// ============================================================================ Application

// Balise proposée après fusion des chansons analysées.
struct MergedTag {
    std::string tag, group;   // group : genre, mood, voice, instrument, tempo, key, perso
    int         count = 0;    // nombre de chansons où elle apparaît
    int         chosen = 0;   // nombre de chansons où l'analyse la cochait d'office
    float       score = 0;    // score moyen sur toutes les chansons analysées (0 pour une chanson où elle manque)
    std::string why;          // détail (bulle d'aide)
    bool        on = false;
    std::string cls;          // genre : classe Discogs d'origine (« Rock---Hard Rock »), pour sa famille et son score
    bool        confirmed = false;  // genre confirmé par MusicBrainz (★)
};

// Structure détectée, avec le nombre de chansons qui l'ont.
struct DetectedStructure {
    std::string              sections;
    std::vector<std::string> songs;
};

static int group_rank(const std::string & g) {
    static const char * order[] = { "genre", "web", "mood", "voice", "instrument", "tempo", "key", "perso" };
    for (int i = 0; i < 8; i++) if (g == order[i]) return i;
    return 8;
}
static const char * group_label(const std::string & g) {
    if (g == "genre") return "Genres";
    if (g == "web") return "Genres du web (MusicBrainz)";
    if (g == "mood") return "Ambiances";
    if (g == "voice") return "Voix";
    if (g == "instrument") return "Instruments";
    if (g == "tempo") return "Tempo";
    if (g == "key") return "Tonalité";
    return "Mes balises";
}
static ImVec4 group_color(const std::string & g) {
    if (g == "genre") return P.violet;
    if (g == "web") return P.accent;
    if (g == "mood") return P.pink;
    if (g == "voice") return P.orange;
    if (g == "instrument") return P.blue;
    if (g == "tempo") return P.amber;
    if (g == "key") return P.teal;
    return P.green;
}

struct App {
    // Emplacements
    Paths           paths;
    bool            located = false, models_ok = false, score_ok = false;
    CreatorSettings settings;

    // Chansons
    std::vector<SongPtr> songs;
    std::string          songs_signature;  // change quand une analyse se termine ou qu'une chanson est retirée

    // Balises
    std::vector<MergedTag>      tags;
    std::map<std::string, bool> overrides;    // choix de l'utilisateur (balise en minuscules → cochée ?)
    std::vector<std::string>    custom_tags;  // balises ajoutées à la main
    std::string                 new_tag;
    std::string                 negative;     // style à éviter (facultatif)

    // Structure
    std::vector<DetectedStructure> detected;
    std::string                    structure_edit;          // structure retenue « Intro,Verse,… »
    bool                           structure_touched = false;  // modifiée à la main : on ne la remplace plus
    bool                           include_song = true, include_inst = true;
    bool                           save_structure = false;
    std::string                    structure_name;

    // Pack
    fs::path    pack_path;
    Pack        pack;
    std::string style_name;
    Note        note;         // compte rendu sous « Ajouter au pack »
    Note        pack_note;    // ouverture, création, suppression

    // Fenêtres de confirmation
    bool        open_replace = false;     // style ou structure du même nom déjà dans le pack
    int         pending_delete_style = -1, pending_delete_structure = -1;
    bool        open_delete = false;

    // ------------------------------------------------------------------ Démarrage
    void init() {
        located = Paths::locate(exe_dir(), paths, true);  // dossier music partagé (outil de Herve)
        settings = load_settings();
        if (located) {
            fs::path models           = paths.root / L"modeles";  // Studio 1.0.42 : modeles\analyse, modeles\musique
            g_analyzer->models        = models / L"analyse";
            g_analyzer->sheetsage     = models / L"musique" / L"SheetSage2-Q8_0.gguf";
            g_analyzer->work          = app_temp_dir() / L"createur";  // 1.0.30 : dossier temporaire du programme
            std::error_code ec;
            models_ok = analysis_installed(g_analyzer->models);
            score_ok  = fs::exists(paths.transcribe, ec) && fs::exists(g_analyzer->sheetsage, ec);
            g_analyzer->transcribe = score_ok ? paths.transcribe : fs::path();
        }
        if (settings.author.empty()) settings.author = "Herve";
        g_analyzer->use_web = settings.use_web;
        if (!settings.last_pack.empty()) open_pack(widen(settings.last_pack), true);
    }

    fs::path packs_dir() const {
        fs::path dir = exe_dir() / L"packs";  // 1.0.45 : Admin_tools\Createur_styles\packs (avant : music\packs)
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }

    // ------------------------------------------------------------------ Chansons
    void add_song(const fs::path & file) {
        for (const SongPtr & s : songs)
            if (s->file == file) return;  // déjà dans la liste
        auto s  = std::make_shared<SongItem>();
        s->file = file;
        s->name = narrow(file.filename().wstring());
        songs.push_back(s);
        settings.audio_dir = narrow(file.parent_path().wstring());
        save_settings(settings);
        if (located && models_ok) g_analyzer->push(s);
        else {
            s->error = "modèle d'analyse absent";
            s->state = 3;
        }
    }

    void remove_song(size_t i) {
        songs[i]->cancel = true;  // en attente : sautée ; en cours : interrompue dès que possible
        songs.erase(songs.begin() + (long long) i);
    }

    // Repart de zéro pour un nouveau style (les infos du pack restent).
    void clear_all() {
        for (const SongPtr & s : songs) s->cancel = true;
        songs.clear();
        overrides.clear();
        custom_tags.clear();
        negative.clear();
        structure_edit.clear();
        structure_touched = false;
        save_structure    = false;
        structure_name.clear();
        style_name.clear();
        note = {};
        songs_signature = "?";  // force le recalcul
    }

    bool busy() const {
        for (const SongPtr & s : songs)
            if (s->state <= 1) return true;
        return false;
    }

    // Glisser-déposer : chansons, ou un pack .yue2pack à ouvrir.
    void handle_drops() {
        if (g_dropped.empty()) return;
        std::vector<std::wstring> files;
        files.swap(g_dropped);
        for (const std::wstring & f : files) {
            fs::path p(f);
            if (lower(narrow(p.extension().wstring())) == ".yue2pack") open_pack(p, false);
            else if (is_audio_file(p)) add_song(p);
        }
    }

    // ------------------------------------------------------------------ Fusion des analyses
    //
    // Choix des balises cochées d'office, d'après les scores MOYENS sur toutes les chansons (une chanson où la balise
    // manque compte pour 0) : une erreur isolée sur une chanson ne suffit plus à cocher une balise.
    //  - genres : score de la classe Discogs moyenné ; 3 au plus, au moins 30 % du premier ; un genre d'une autre famille
    //    Discogs que la famille dominante (« Funk / Soul » face à « Rock ») doit atteindre 60 % du premier ;
    //  - ambiances : les 2 plus fortes (moyenne ≥ 0,10) ; instruments : les 4 plus forts (moyenne ≥ 0,25), et les
    //    synthétiseurs / boîte à rythmes doivent atteindre 0,35 quand la guitare électrique domine ;
    //  - voix : un seul genre de voix et un seul registre (les plus forts), rien si la majorité des chansons est instrumentale.
    void select_by_average(const std::vector<const SongItem *> & done, int n) {
        if (n == 0) return;
        auto family = [](const std::string & cls) { size_t p = cls.find("---"); return p == std::string::npos ? cls : cls.substr(0, p); };
        for (MergedTag & t : tags) {
            if (t.group == "genre" && !t.cls.empty()) {  // score brut de la classe dans chaque chanson, même si elle n'y était pas proposée
                float sum = 0;
                for (const SongItem * s : done)
                    for (const TagScore & ts : s->result.styles)
                        if (ts.name == t.cls) sum += ts.score;
                t.score = sum / n;
            } else {
                t.score /= n;
            }
            t.on = false;
        }
        auto ranked = [&](const char * group) {
            std::vector<MergedTag *> v;
            for (MergedTag & t : tags)
                if (t.group == group) v.push_back(&t);
            std::sort(v.begin(), v.end(), [](MergedTag * a, MergedTag * b) { return a->score > b->score; });
            return v;
        };
        auto avg_of = [&](const char * tag) {
            for (MergedTag & t : tags)
                if (lower(t.tag) == tag) return t.score;
            return 0.f;
        };
        // Genres
        std::vector<MergedTag *> g = ranked("genre");
        if (!g.empty()) {
            std::map<std::string, float> fam;
            for (MergedTag * t : g) fam[family(t->cls)] += t->score;
            std::string dom = std::max_element(fam.begin(), fam.end(), [](auto & a, auto & b) { return a.second < b.second; })->first;
            float       top = g[0]->score;
            int         k   = 0;
            for (MergedTag * t : g) {
                bool same = family(t->cls) == dom;
                if (k < 3 && t->score >= std::max(0.03f, top * 0.30f) && (same || t->score >= top * 0.60f)) {
                    t->on = true;
                    k++;
                }
                char buf[96];
                snprintf(buf, sizeof(buf), "\nScore moyen : %.0f %% · famille %s%s", t->score * 100, family(t->cls).c_str(),
                         same ? "" : " (autre que la famille dominante)");
                t->why += buf;
            }
        }
        // Ambiances
        int k = 0;
        for (MergedTag * t : ranked("mood")) t->on = k < 2 && t->score >= 0.10f && ++k;
        // Instruments
        bool  guitar = avg_of("electric guitar") >= 0.30f;
        k = 0;
        for (MergedTag * t : ranked("instrument")) {
            std::string l   = lower(t->tag);
            float       min = (guitar && (l == "analog synth" || l == "synth pads" || l == "drum machine")) ? 0.35f : 0.25f;
            t->on           = k < 4 && t->score >= min && ++k;
        }
        // Voix
        int vocal = 0;
        for (const SongItem * s : done) vocal += s->result.voice >= 0.5f;
        if (vocal * 2 >= n) {
            MergedTag * gender = nullptr, * reg = nullptr;
            for (MergedTag * t : ranked("voice")) {
                std::string l = lower(t->tag);
                if (l == "male vocal" || l == "female vocal") { if (!gender) gender = t; }
                else if (l != "low vocal register" && !reg) reg = t;
            }
            if (gender) gender->on = true;
            if (reg) reg->on = true;
        }
    }

    // Recoupement des genres avec MusicBrainz (si au moins une chanson a des genres votés) :
    // genre détecté et confirmé → ★ et coché ; détecté mais absent du web → décoché (« non confirmé ») ;
    // genre du web que l'analyse n'a pas trouvé → proposé dans « Genres du web » (coché s'il est en tête des votes).
    void apply_web(const std::vector<const SongItem *> & done) {
        if (!g_analyzer->use_web) return;
        struct W {
            std::string name;
            int         songs = 0;
            float       sum = 0;
        };
        std::map<std::string, W> web;
        int                      n_web = 0;
        for (const SongItem * s : done) {
            if (s->web.genres.empty()) continue;
            n_web++;
            for (const WebGenre & g : s->web.genres) {
                W & w = web[g.name];
                w.name = g.name;
                w.songs++;
                w.sum += g.score;
            }
        }
        if (n_web == 0) return;
        std::set<std::string> matched;
        for (MergedTag & t : tags) {
            if (t.group != "genre") continue;
            std::vector<std::string> hits;
            for (auto & kv : web)
                if (genre_matches(t.tag, kv.first)) {
                    hits.push_back(kv.first);
                    matched.insert(kv.first);
                }
            t.confirmed = !hits.empty();
            t.on        = t.confirmed;
            t.why += t.confirmed ? "\n★ Confirmé par MusicBrainz : " + join_list(hits, ", ") : std::string("\nNon confirmé par MusicBrainz : décoché");
        }
        std::vector<W> extra;
        for (auto & kv : web)
            if (!matched.count(kv.first)) extra.push_back(kv.second);
        std::sort(extra.begin(), extra.end(), [](const W & a, const W & b) { return a.sum > b.sum; });
        int k = 0;
        for (const W & w : extra) {
            MergedTag t;
            t.tag       = w.name;
            t.group     = "web";
            t.count     = w.songs;
            t.score     = w.sum / n_web;
            t.confirmed = true;
            t.on        = k < 3 && w.songs * 2 >= n_web && t.score >= 0.30f;
            k += t.on;
            char buf[160];
            snprintf(buf, sizeof(buf), "Genre voté sur MusicBrainz pour %d chanson(s) sur %d, non trouvé par l'analyse (part des votes : %.0f %%)",
                     w.songs, n_web, t.score * 100);
            t.why = buf;
            tags.push_back(t);
        }
    }

    void refresh_merge() {
        std::string sig;
        for (const SongPtr & s : songs)
            if (s->state == 2) sig += std::to_string((uintptr_t) s.get()) + ";";
        sig += "|" + std::to_string(custom_tags.size()) + (g_analyzer->use_web ? "w" : "");
        for (const std::string & c : custom_tags) sig += c + ";";
        if (sig == songs_signature) return;
        songs_signature = sig;

        int                      n = 0;
        std::map<std::string, MergedTag> m;
        std::vector<double>      bpms;
        std::map<std::string, int> keys;
        std::map<std::string, DetectedStructure> structs;
        std::vector<const SongItem *> done;
        for (const SongPtr & s : songs) {
            if (s->state != 2) continue;
            n++;
            done.push_back(s.get());
            for (const Suggestion & sg : s->sugg) {
                if (sg.group == "tempo" || sg.group == "key") continue;  // traités à part ci-dessous
                MergedTag & t = m[lower(sg.tag)];
                if (t.count == 0) {
                    t.tag   = sg.tag;
                    t.group = sg.group;
                }
                t.count++;
                if (sg.selected) t.chosen++;
                t.score += sg.score;
                if (sg.group == "genre" && t.cls.empty()) {  // « Discogs : Rock---Hard Rock (12 %) » → « Rock---Hard Rock »
                    size_t a = sg.why.find(": "), b = sg.why.rfind(" (");
                    if (a != std::string::npos && b != std::string::npos && b > a) t.cls = sg.why.substr(a + 2, b - a - 2);
                }
                if (!sg.why.empty()) t.why += (t.why.empty() ? "" : "\n") + s->name + " : " + sg.why;
            }
            if (s->result.bpm > 0) bpms.push_back(s->result.bpm);
            if (!s->result.key.empty()) keys[s->result.key]++;
            if (!s->sections.empty()) {
                DetectedStructure & d = structs[s->sections];
                d.sections            = s->sections;
                d.songs.push_back(s->name);
            }
        }
        tags.clear();
        for (auto & kv : m) tags.push_back(kv.second);
        select_by_average(done, n);
        apply_web(done);
        // Tempo : médiane des chansons (une valeur isolée très différente ne fausse pas le résultat).
        if (!bpms.empty()) {
            std::sort(bpms.begin(), bpms.end());
            double med = bpms.size() % 2 ? bpms[bpms.size() / 2] : (bpms[bpms.size() / 2 - 1] + bpms[bpms.size() / 2]) / 2;
            int    bpm = std::clamp((int) std::lround(med), 60, 260);
            MergedTag t;
            t.tag   = std::to_string(bpm) + " BPM";
            t.group = "tempo";
            t.count = t.chosen = (int) bpms.size();
            t.score = 1;
            t.why   = "Tempo médian de " + std::to_string(bpms.size()) + " chanson(s) :";
            for (double b : bpms) t.why += " " + std::to_string((int) std::lround(b));
            t.on = t.count * 2 >= n;
            tags.push_back(t);
        }
        // Tonalité : la plus fréquente (les autres sont citées dans la bulle d'aide).
        if (!keys.empty()) {
            auto best = std::max_element(keys.begin(), keys.end(), [](auto & a, auto & b) { return a.second < b.second; });
            MergedTag t;
            t.tag   = best->first + " key";
            t.group = "key";
            t.count = t.chosen = best->second;
            t.score = 1;
            t.why   = "Tonalités trouvées :";
            for (auto & kv : keys) t.why += " " + kv.first + " (" + std::to_string(kv.second) + ")";
            t.on = t.count * 2 >= n && n > 0;
            tags.push_back(t);
        }
        for (const std::string & c : custom_tags) {
            MergedTag t;
            t.tag   = c;
            t.group = "perso";
            t.count = n;
            t.on    = true;
            t.why   = "Balise ajoutée à la main (clic droit : la retirer)";
            tags.push_back(t);
        }
        std::stable_sort(tags.begin(), tags.end(), [](const MergedTag & a, const MergedTag & b) {
            if (group_rank(a.group) != group_rank(b.group)) return group_rank(a.group) < group_rank(b.group);
            if (a.confirmed != b.confirmed) return a.confirmed;
            if (a.on != b.on) return a.on;
            return a.score > b.score;
        });
        for (MergedTag & t : tags) {
            auto it = overrides.find(lower(t.tag));
            if (it != overrides.end()) t.on = it->second;
        }
        // Structures : la plus fréquente en premier
        detected.clear();
        for (auto & kv : structs) detected.push_back(kv.second);
        std::stable_sort(detected.begin(), detected.end(),
                         [](const DetectedStructure & a, const DetectedStructure & b) { return a.songs.size() > b.songs.size(); });
        if (!structure_touched) structure_edit = detected.empty() ? std::string() : detected[0].sections;
    }

    int analysed_count() const {
        int n = 0;
        for (const SongPtr & s : songs) n += s->state == 2;
        return n;
    }

    std::string style_text() const {
        std::vector<std::string> on;
        for (const MergedTag & t : tags)
            if (t.on) on.push_back(t.tag);
        return join_list(on, ", ");
    }

    // ------------------------------------------------------------------ Pack
    void open_pack(const fs::path & path, bool quiet) {
        Pack        p;
        std::string error;
        if (!read_pack(path, p, error)) {
            if (!quiet) pack_note = { narrow(path.filename().wstring()) + " : " + error, 1 };
            return;
        }
        pack      = std::move(p);
        pack_path = path;
        settings.last_pack = narrow(path.wstring());
        save_settings(settings);
        if (!quiet) pack_note = { "Pack ouvert : " + narrow(path.filename().wstring()), 0 };
    }

    void new_pack() {
        fs::path path = pick_pack(true, packs_dir());
        if (path.empty()) return;
        Pack p;
        p.name    = narrow(path.stem().wstring());
        p.author  = settings.author;
        p.version = "1.0";
        if (!write_pack(path, p)) {
            pack_note = { "Impossible d'écrire " + narrow(path.wstring()), 1 };
            return;
        }
        pack      = std::move(p);
        pack_path = path;
        settings.last_pack = narrow(path.wstring());
        save_settings(settings);
        pack_note = { "Nouveau pack créé : " + narrow(path.filename().wstring()), 0 };
    }

    bool save_pack() {
        if (pack_path.empty()) return false;
        if (!write_pack(pack_path, pack)) {
            pack_note = { "Impossible d'enregistrer le pack (fichier en lecture seule ou ouvert ailleurs ?)", 1 };
            return false;
        }
        if (!pack.author.empty() && pack.author != settings.author) {
            settings.author = pack.author;
            save_settings(settings);
        }
        return true;
    }

    int find_style(const std::string & name) const {
        for (size_t i = 0; i < pack.styles.size(); i++)
            if (same_name(pack.styles[i].name, name)) return (int) i;
        return -1;
    }
    int find_structure(const std::string & name) const {
        for (size_t i = 0; i < pack.structures.size(); i++)
            if (same_name(pack.structures[i].name, name)) return (int) i;
        return -1;
    }

    // Vérifie le formulaire ; s'il manque quelque chose, le dit et renvoie faux.
    bool ready_to_add() {
        std::string sections = normalize_sections(structure_edit);
        if (trim_text(style_name).empty()) note = { "Donne un nom au style.", 1 };
        else if (style_text().empty()) note = { "Aucune balise cochée : le style serait vide.", 1 };
        else if ((include_song || include_inst || save_structure) && split_list(sections).size() < 2 && !sections.empty())
            note = { "La structure doit compter au moins deux sections.", 1 };
        else if (save_structure && trim_text(structure_name).empty()) note = { "Donne un nom à la structure à enregistrer.", 1 };
        else if (save_structure && sections.empty()) note = { "Pas de structure à enregistrer.", 1 };
        else return true;
        return false;
    }

    void ask_add() {
        if (!ready_to_add()) return;
        if (pack_path.empty()) {  // pas encore de pack : on en crée un
            new_pack();
            if (pack_path.empty()) return;
        }
        bool conflict = find_style(style_name) >= 0 || (save_structure && find_structure(structure_name) >= 0);
        if (conflict) open_replace = true;
        else do_add();
    }

    void do_add() {
        std::string sections = normalize_sections(structure_edit);
        PackStyle   s;
        s.name  = trim_text(style_name);
        s.style = style_text();
        if (include_song && !sections.empty()) s.structure_song = sections;
        if (include_inst && !sections.empty()) s.structure_inst = instrumental_sections(sections);
        if (!trim_text(negative).empty()) {
            s.has_negative   = true;
            s.negative_style = trim_text(negative);
        }
        int at = find_style(s.name);
        if (at >= 0) pack.styles[at] = s;
        else pack.styles.push_back(s);
        if (save_structure && !sections.empty()) {
            PackStructure st{ trim_text(structure_name), sections };
            int           k = find_structure(st.name);
            if (k >= 0) pack.structures[k] = st;
            else pack.structures.push_back(st);
        }
        if (!save_pack()) {
            note = pack_note;
            return;
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "Style « %s » %s. Le pack contient %d style(s) et %d structure(s).", s.name.c_str(),
                 at >= 0 ? "remplacé" : "ajouté au pack", (int) pack.styles.size(), (int) pack.structures.size());
        note = { buf, 0 };
        if (trim_text(pack.name).empty())
            note.text += " Attention : le pack n'a pas de nom ; à l'import, le Studio rangera ses éléments dans tes styles personnels.";
    }

    // ------------------------------------------------------------------ En-tête
    void draw_header() {
        ImGui::PushFont(g_header);
        ImGui::TextColored(P.accent, "%s", "Créateur de styles");
        ImGui::PopFont();
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4 * g_dpi);
        ImGui::TextColored(P.muted, "%s", "pour Studio Audio AI " STUDIO_VERSION " · réservé à l'éditeur");
        // Pastille du pack ouvert, à droite
        std::string label = pack_path.empty() ? std::string("aucun pack ouvert")
                                              : "♪ " + (pack.name.empty() ? narrow(pack_path.stem().wstring()) : pack.name);
        float w = ImGui::CalcTextSize(label.c_str()).x + 16 * g_dpi;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x);
        chip(label.c_str(), pack_path.empty() ? P.muted : P.green);
        if (!pack_path.empty()) tooltip(narrow(pack_path.wstring()).c_str());

        if (!located) {
            Note{ "Studio introuvable : place ce programme dans le dossier music\\createur (à côté du Studio) pour analyser des chansons.", 1 }.draw();
        } else if (!models_ok) {
            Note{ "Modèle d'analyse absent (models\\analysis) : installe-le depuis le Studio (bouton « Analyser » du morceau d'inspiration).", 1 }.draw();
        } else if (!score_ok) {
            Note{ "Moteur de partition ou modèle SheetSage absent : l'analyse donnera le style, mais ni tempo, ni tonalité, ni structure.", 2 }.draw();
        }
    }

    // ------------------------------------------------------------------ Carte : chansons d'inspiration
    void draw_songs() {
        begin_card("c_songs", P.blue);
        card_title("1. Chansons d'inspiration", P.blue, "une ou plusieurs, même genre de musique");
        if (accent_button("Ajouter des chansons…")) {
            for (const fs::path & f : pick_audio_files(settings.audio_dir)) add_song(f);
        }
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", "ou glisse-les sur la fenêtre");
        if (!songs.empty()) {
            ImGui::SameLine();
            float bw = ImGui::CalcTextSize("Nouveau style").x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - bw);
            if (soft_button("Nouveau style", P.muted, false)) clear_all();
            tooltip("Vide la liste des chansons, les balises et la structure pour créer un autre style (le pack reste ouvert).");
        }
        bool web = settings.use_web;
        if (ImGui::Checkbox("Vérifier les genres sur MusicBrainz", &web)) {
            settings.use_web    = web;
            g_analyzer->use_web = web;
            save_settings(settings);
        }
        tooltip("Le groupe et le titre sont lus dans les étiquettes du fichier (sinon son nom « Groupe - Titre » et ses dossiers). "
                "Les genres votés sur MusicBrainz (morceau, album, groupe) confirment ou écartent les genres détectés. "
                "Vaut pour les chansons analysées ensuite ; hors ligne, l'analyse seule décide.");
        if (songs.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(P.muted, "%s", "L'analyse reprend celle du morceau d'inspiration du Studio, sans les paroles :");
            ImGui::TextColored(P.muted, "%s", "genres, ambiances, voix, instruments, tempo, tonalité et structure.");
        }
        int remove = -1;
        for (size_t i = 0; i < songs.size(); i++) {
            SongItem & s = *songs[i];
            ImGui::PushID((int) i);
            ImGui::Separator();
            int st = s.state;
            ImGui::TextUnformatted(s.name.c_str());
            tooltip(narrow(s.file.wstring()).c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("×").x - ImGui::GetStyle().FramePadding.x * 2);
            if (cross_button("x")) remove = (int) i;
            tooltip("Retirer cette chanson");
            if (st == 0) {
                ImGui::TextColored(P.muted, "%s", "en attente d'analyse");
            } else if (st == 1) {
                std::string stage;
                float       f;
                {
                    std::lock_guard<std::mutex> lock(s.mutex);
                    stage = s.stage;
                    f     = s.progress;
                }
                char buf[128];
                snprintf(buf, sizeof(buf), "%s  %d %%", stage_text(stage), (int) std::lround(f * 100));
                ImGui::ProgressBar(f, ImVec2(-1, 0), buf);
            } else if (st == 2) {
                const AudioAnalysis & a = s.result;
                std::string info;
                if (a.bpm > 0) info += std::to_string((int) std::lround(a.bpm)) + " BPM";
                if (!a.key.empty()) info += (info.empty() ? "" : " · ") + a.key;
                info += (info.empty() ? "" : " · ") + std::string(a.voice >= 0.5f ? "chantée" : "instrumentale");
                if (!s.sections.empty()) info += " · " + std::to_string(split_list(s.sections).size()) + " sections";
                ImGui::TextColored(P.ok, "✓ %s", info.c_str());
                if (!a.score_error.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(P.warn, "%s", "(sans partition)");
                    tooltip(a.score_error.c_str());
                }
                if (!s.web.status.empty()) {  // recherche web : identité retenue et résultat
                    std::string who = s.web.artist.empty() ? std::string() : s.web.artist + (s.web.title.empty() ? "" : " – " + s.web.title) + " · ";
                    ImGui::TextColored(s.web.genres.empty() ? P.warn : P.muted, "Web : %s%s", who.c_str(), s.web.status.c_str());
                    std::string tip;
                    for (const WebGenre & g : s.web.genres) tip += g.name + " (" + std::to_string((int) std::lround(g.score * 100)) + " %)\n";
                    if (!tip.empty()) tooltip(tip.c_str());
                }
            } else if (st == 3) {
                ImGui::TextColored(P.danger, "Erreur : %s", s.error.c_str());
            } else {
                ImGui::TextColored(P.muted, "%s", "annulée");
            }
            ImGui::PopID();
        }
        if (remove >= 0) remove_song((size_t) remove);
        end_card();
    }

    // ------------------------------------------------------------------ Carte : structure
    void draw_structure() {
        begin_card("c_structure", P.teal);
        card_title("3. Structure", P.teal, "sections du morceau, sans paroles");
        if (detected.empty()) {
            ImGui::TextColored(P.muted, "%s", analysed_count() ? "Aucune structure détectée (partition indisponible ou trop peu de sections)."
                                                               : "La structure détectée apparaîtra ici après l'analyse.");
        } else {
            ImGui::TextColored(P.muted, "%s", "Structures détectées (la plus fréquente est proposée) :");
            for (size_t i = 0; i < detected.size(); i++) {
                const DetectedStructure & d = detected[i];
                ImGui::PushID((int) i);
                bool        sel   = normalize_sections(structure_edit) == normalize_sections(d.sections);
                std::string label = arrow_text(d.sections) + "   (" + std::to_string(d.songs.size()) + " chanson" + (d.songs.size() > 1 ? "s" : "") + ")";
                if (ImGui::RadioButton(label.c_str(), sel)) {
                    structure_edit    = d.sections;
                    structure_touched = true;
                }
                tooltip(join_list(d.songs, "\n").c_str());
                ImGui::PopID();
            }
        }
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Structure retenue");
        ImGui::SameLine();
        float combo_w = 190 * g_dpi;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - combo_w - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputTextWithHint("##structure", "Intro,Verse,Chorus,Verse,Chorus,Outro", &structure_edit)) structure_touched = true;
        if (text_context_menu(structure_edit)) structure_touched = true;
        tooltip("Sections séparées par des virgules : Intro, Verse, Pre-Chorus, Chorus, Bridge, Interlude, Outro…");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(combo_w);
        // Largeur de la liste fixée : sinon la fenêtre déroulante s'élargit à chaque image.
        ImGui::SetNextWindowSizeConstraints(ImVec2(combo_w * 1.6f, 0), ImVec2(combo_w * 1.6f, FLT_MAX));
        if (ImGui::BeginCombo("##types", "Structures types…")) {
            for (const BuiltinStructure & b : BUILTIN_STRUCTURES) {
                if (ImGui::Selectable(b.name)) {
                    structure_edit    = b.sections;
                    structure_touched = true;
                }
                tooltip(arrow_text(b.sections).c_str());
            }
            ImGui::EndCombo();
        }
        std::string norm = normalize_sections(structure_edit);
        if (!norm.empty()) ImGui::TextColored(P.teal, "%s", arrow_text(norm).c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted("Inclure dans le style (comme les cases du préréglage) :");
        ImGui::SameLine();
        ImGui::Checkbox("chanson", &include_song);
        ImGui::SameLine();
        ImGui::Checkbox("instrumental", &include_inst);
        tooltip("Pour un instrumental, les balises sont écrites en minuscules et [Interlude] devient [bridge], comme dans le Studio.");
        ImGui::Checkbox("Enregistrer aussi comme structure du pack", &save_structure);
        tooltip("Elle apparaîtra dans la liste des structures du Studio, sous le nom du pack.");
        if (save_structure) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##stname", "nom de la structure", &structure_name);
            text_context_menu(structure_name);
        }
        end_card();
    }

    // ------------------------------------------------------------------ Carte : balises de style
    void draw_tags() {
        begin_card("c_tags", P.violet);
        int n = analysed_count();
        char hint[96];
        snprintf(hint, sizeof(hint), n ? "fusion de %d chanson%s analysée%s" : "cochées : présentes dans au moins la moitié des chansons", n,
                 n > 1 ? "s" : "", n > 1 ? "s" : "");
        card_title("2. Balises de style", P.violet, hint);
        if (tags.empty()) ImGui::TextColored(P.muted, "%s", "Les balises proposées apparaîtront ici après l'analyse.");

        ImGuiStyle & st = ImGui::GetStyle();
        std::string  current_group;
        float        avail = ImGui::GetContentRegionAvail().x, x = 0;
        int          remove_custom = -1;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        for (size_t i = 0; i < tags.size(); i++) {
            MergedTag & t = tags[i];
            ImVec4      c = group_color(t.group);
            if (t.group != current_group) {  // titre de groupe
                current_group = t.group;
                ImGui::PushFont(g_bold);
                ImGui::TextColored(c, "%s", group_label(t.group));
                ImGui::PopFont();
                x = 0;
            }
            std::string text = (t.on ? "✓ " : "") + std::string(t.confirmed && t.group == "genre" ? "★ " : "") + t.tag;
            if (n > 1 && t.group != "perso") text += "  " + std::to_string(t.count) + "/" + std::to_string(n);
            float w = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
            if (x > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
                ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
                x += st.ItemSpacing.x * 0.6f + w;
            } else {
                x = w;
            }
            ImGui::PushID((int) i);
            ImGui::PushStyleColor(ImGuiCol_Button, alpha(c, t.on ? 0.55f : 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(c, t.on ? 0.65f : 0.28f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(c, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Text, t.on ? P.text : alpha(P.text, 0.75f));
            if (ImGui::Button(text.c_str())) {
                t.on                   = !t.on;
                overrides[lower(t.tag)] = t.on;
            }
            ImGui::PopStyleColor(4);
            if (t.group == "perso" && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                for (size_t k = 0; k < custom_tags.size(); k++)
                    if (same_name(custom_tags[k], t.tag)) remove_custom = (int) k;
            }
            if (!t.why.empty()) tooltip(t.why.c_str());
            ImGui::PopID();
        }
        ImGui::PopStyleVar(2);
        if (remove_custom >= 0) {
            overrides.erase(lower(custom_tags[remove_custom]));
            custom_tags.erase(custom_tags.begin() + remove_custom);
        }

        // Balise ajoutée à la main
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        bool enter = ImGui::InputTextWithHint("##newtag", "ajouter une balise (en anglais : « reverb », « 80s »…)", &new_tag,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        text_context_menu(new_tag);
        ImGui::SameLine();
        if ((ImGui::Button("Ajouter") || enter) && !trim_text(new_tag).empty()) {
            for (const std::string & tag : split_list(new_tag)) {
                bool known = false;
                for (MergedTag & t : tags)
                    if (same_name(t.tag, tag)) {
                        t.on = known = true;
                        overrides[lower(t.tag)] = true;
                    }
                if (!known) custom_tags.push_back(tag);
            }
            new_tag.clear();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Style à éviter");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##neg", "facultatif : « autotune, vocoder »… (balises négatives du Studio)", &negative);
        text_context_menu(negative);

        // Aperçu du texte du style
        ImGui::Spacing();
        std::string preview = style_text();
        ImGui::TextColored(P.muted, "%s", "Aperçu du style :");
        ImGui::PushStyleColor(ImGuiCol_Text, preview.empty() ? P.muted : P.text);
        ImGui::TextWrapped("%s", preview.empty() ? "(aucune balise cochée)" : preview.c_str());
        ImGui::PopStyleColor();
        end_card();
    }

    // ------------------------------------------------------------------ Carte : pack
    void draw_pack() {
        begin_card("c_pack", P.green);
        card_title("4. Pack", P.green, "fichier .yue2pack à importer dans les Réglages du Studio");
        if (soft_button("Nouveau pack…", P.green, false)) new_pack();
        ImGui::SameLine();
        if (soft_button("Ouvrir un pack…", P.green, false)) {
            fs::path p = pick_pack(false, pack_path.empty() ? packs_dir() : pack_path.parent_path());
            if (!p.empty()) open_pack(p, false);
        }
        if (!pack_path.empty()) {
            ImGui::SameLine();
            if (soft_button("Montrer le fichier", P.muted, false)) {
                std::wstring args = L"/select," + quote_arg(pack_path.wstring());
                ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            }
        }
        pack_note.draw();

        if (pack_path.empty()) {
            ImGui::TextColored(P.muted, "%s", "Aucun pack ouvert : « Ajouter au pack » proposera d'en créer un (dossier packs du Créateur).");
        } else {
            // Informations du pack : enregistrées dès qu'un champ est quitté après modification
            float label_w = 90 * g_dpi;
            auto  field   = [&](const char * label, const char * id, std::string & value, const char * hint) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine(label_w);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint(id, hint, &value);
                bool done = ImGui::IsItemDeactivatedAfterEdit();
                if (text_context_menu(value) || done) save_pack();
            };
            field("Nom", "##pname", pack.name, "nom affiché dans le Studio (« Rock 80s »)");
            field("Auteur", "##pauthor", pack.author, "");
            field("Version", "##pversion", pack.version, "1.0");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Description");
            ImGui::SameLine(label_w);
            ImGui::InputTextMultiline("##pdesc", &pack.description, ImVec2(-1, ImGui::GetTextLineHeight() * 2.6f));
            bool done = ImGui::IsItemDeactivatedAfterEdit();
            if (text_context_menu(pack.description) || done) save_pack();

            // Contenu du pack
            ImGui::Spacing();
            char head[96];
            snprintf(head, sizeof(head), "Contenu : %d style(s), %d structure(s)", (int) pack.styles.size(), (int) pack.structures.size());
            ImGui::TextColored(P.muted, "%s", head);
            if (ImGui::BeginTable("contents", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 80 * g_dpi);
                ImGui::TableSetupColumn("nom", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 28 * g_dpi);
                for (size_t i = 0; i < pack.styles.size(); i++) {
                    const PackStyle & s = pack.styles[i];
                    ImGui::PushID((int) i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(P.violet, "%s", "style");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s.name.c_str());
                    std::string tip = s.style;
                    if (!s.structure_song.empty()) tip += "\n\nChanson : " + arrow_text(s.structure_song);
                    if (!s.structure_inst.empty()) tip += "\nInstrumental : " + arrow_text(s.structure_inst);
                    if (s.has_negative && !s.negative_style.empty()) tip += "\nÀ éviter : " + s.negative_style;
                    tooltip(tip.c_str());
                    if (!s.structure_song.empty() || !s.structure_inst.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(P.teal, "%s", "+ structure");
                    }
                    ImGui::TableNextColumn();
                    if (cross_button("del")) {
                        pending_delete_style = (int) i;
                        open_delete          = true;
                    }
                    ImGui::PopID();
                }
                for (size_t i = 0; i < pack.structures.size(); i++) {
                    const PackStructure & st = pack.structures[i];
                    ImGui::PushID(10000 + (int) i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(P.teal, "%s", "structure");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(st.name.c_str());
                    tooltip(arrow_text(st.sections).c_str());
                    ImGui::TableNextColumn();
                    if (cross_button("del")) {
                        pending_delete_structure = (int) i;
                        open_delete              = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        // Nom du style et ajout
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(g_bold);
        ImGui::TextUnformatted("Nom du style");
        ImGui::PopFont();
        ImGui::SameLine();
        float bw = ImGui::CalcTextSize("Ajouter au pack").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##sname", "nom affiché dans la liste des préréglages (« Cold wave 80s »)", &style_name);
        text_context_menu(style_name);
        ImGui::SameLine();
        if (accent_button("Ajouter au pack")) ask_add();
        note.draw();
        end_card();
    }

    // ------------------------------------------------------------------ Fenêtres de confirmation
    void draw_modals() {
        if (open_replace) {
            ImGui::OpenPopup("Déjà dans le pack");
            open_replace = false;
        }
        if (open_delete) {
            ImGui::OpenPopup("Retirer du pack ?");
            open_delete = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal("Déjà dans le pack", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            if (find_style(style_name) >= 0) ImGui::TextWrapped("Le pack contient déjà un style « %s ».", trim_text(style_name).c_str());
            if (save_structure && find_structure(structure_name) >= 0)
                ImGui::TextWrapped("Le pack contient déjà une structure « %s ».", trim_text(structure_name).c_str());
            ImGui::TextWrapped("%s", "Le remplacer par la nouvelle version ?");
            ImGui::Spacing();
            if (accent_button("Remplacer")) {
                do_add();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Annuler") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
        if (ImGui::BeginPopupModal("Retirer du pack ?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            bool        is_style = pending_delete_style >= 0 && pending_delete_style < (int) pack.styles.size();
            bool        is_struct = !is_style && pending_delete_structure >= 0 && pending_delete_structure < (int) pack.structures.size();
            std::string name = is_style ? pack.styles[pending_delete_style].name : is_struct ? pack.structures[pending_delete_structure].name : "";
            ImGui::TextWrapped("Retirer %s « %s » du pack ?", is_style ? "le style" : "la structure", name.c_str());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, alpha(P.danger, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, P.danger);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            bool yes = ImGui::Button("Retirer");
            ImGui::PopStyleColor(3);
            if (yes) {
                if (is_style) pack.styles.erase(pack.styles.begin() + pending_delete_style);
                if (is_struct) pack.structures.erase(pack.structures.begin() + pending_delete_structure);
                if (save_pack()) pack_note = { "« " + name + " » retiré du pack.", 0 };
                pending_delete_style = pending_delete_structure = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Annuler") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pending_delete_style = pending_delete_structure = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ Image complète
    void frame() {
        handle_drops();
        refresh_merge();
        ImGuiViewport * vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        draw_header();
        ImGui::Spacing();
        // Deux colonnes, chacune avec sa barre de défilement
        float gap  = 14 * g_dpi;
        float left = std::floor((ImGui::GetContentRegionAvail().x - gap) * 0.46f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("left", ImVec2(left, 0), ImGuiChildFlags_None);
        draw_songs();
        ImGui::Spacing();
        draw_structure();
        ImGui::EndChild();
        ImGui::SameLine(0, gap);
        ImGui::BeginChild("right", ImVec2(0, 0), ImGuiChildFlags_None);
        draw_tags();
        ImGui::Spacing();
        draw_pack();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        draw_modals();
        ImGui::End();
    }
};

// ============================================================================ Point d'entrée

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, instance, LoadIconW(instance, MAKEINTRESOURCEW(1)),
                    LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"CreateurStylesYuE2", nullptr };
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Créateur de styles - Studio Audio AI", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              (int) (1280 * g_dpi), (int) (900 * g_dpi), nullptr, nullptr, instance, nullptr);
    g_hwnd = hwnd;
    if (!create_device(hwnd)) {
        cleanup_device();
        UnregisterClassW(wc.lpszClassName, instance);
        MessageBoxW(nullptr, L"DirectX 11 indisponible : impossible d'ouvrir la fenêtre.", L"Créateur de styles", MB_ICONERROR);
        return 1;
    }
    DragAcceptFiles(hwnd, TRUE);

    App app;
    app.init();

    bool light = windows_light_theme();
    int  theme = studio_theme(app.located ? app.paths.root : exe_dir().parent_path());
    BOOL dark  = theme == 1 || (theme == 0 && !light);  // barre de titre sombre avec les thèmes sombres
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO & io   = ImGui::GetIO();
    io.IniFilename = nullptr;  // rien à mémoriser entre deux lancements
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_theme(theme, light);
    build_fonts();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // Boucle sobre : on ne redessine que sur événement ; pendant une analyse, dix fois par seconde (barre de progression).
    bool done   = false;
    int  frames = 3;
    while (!done) {
        if (frames <= 0) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, app.busy() ? 100 : 1000, QS_ALLINPUT);
            frames = 2;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
            frames = 3;
        }
        if (done) break;
        if (g_resize_w != 0 && g_resize_h != 0) {
            cleanup_render_target();
            g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            create_render_target();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        const float clear[4] = { P.bg.x, P.bg.y, P.bg.z, 1.f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1, 0);
        frames--;
    }

    for (const SongPtr & s : app.songs) s->cancel = true;  // le fil d'analyse s'arrête de lui-même
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
