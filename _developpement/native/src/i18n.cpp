#include "i18n.hpp"
#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "yyjson.h"

namespace fs = std::filesystem;

namespace i18n {
namespace {

std::mutex                                    g_mutex;
std::vector<Language>                         g_languages;
std::string                                   g_current = "fr";
std::unordered_map<std::string, std::string>  g_strings;   // langue courante
std::unordered_map<std::string, std::string>  g_fallback;   // anglais, quand la langue courante n'a pas la clé
std::unordered_map<std::string_view, const std::string *> g_view;  // recherche sans allocation (clés = celles de g_strings / g_fallback)
std::u32string                                g_glyphs;

std::u32string to_u32(const std::string & s) {
    std::u32string out;
    size_t         i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char) s[i];
        char32_t      cp;
        int           n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { i++; continue; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

// Lit « strings » d'un fichier de langue ; renvoie faux si le fichier est illisible.
bool load_strings(const fs::path & file, std::unordered_map<std::string, std::string> & out) {
    std::string data = read_text(file);
    if (data.empty()) return false;
    yyjson_doc * doc = yyjson_read(data.data(), data.size(), 0);
    if (!doc) return false;
    yyjson_val * strings = yyjson_obj_get(yyjson_doc_get_root(doc), "strings");
    size_t       idx, max;
    yyjson_val * k, * v;
    if (yyjson_is_obj(strings)) {
        yyjson_obj_foreach(strings, idx, max, k, v) {
            if (yyjson_is_str(v) && yyjson_get_len(v) > 0) out[yyjson_get_str(k)] = yyjson_get_str(v);
        }
    }
    yyjson_doc_free(doc);
    return true;
}

const Language * find(const std::string & code) {
    for (const Language & l : g_languages)
        if (l.code == code) return &l;
    return nullptr;
}

}  // namespace

void init(const fs::path & dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_languages.clear();
    std::error_code ec;
    for (const auto & e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        std::string stem = e.path().stem().string();
        if (stem.empty() || stem[0] == '_') continue;  // « _modele » : gabarit pour de nouvelles langues
        std::string data = read_text(e.path());
        yyjson_doc * doc = yyjson_read(data.data(), data.size(), 0);
        if (!doc) continue;
        yyjson_val * meta = yyjson_obj_get(yyjson_doc_get_root(doc), "meta");
        Language     l;
        l.code   = stem;
        l.file   = e.path();
        l.name   = yyjson_is_str(yyjson_obj_get(meta, "name")) ? yyjson_get_str(yyjson_obj_get(meta, "name")) : stem;
        l.script = yyjson_is_str(yyjson_obj_get(meta, "script")) ? yyjson_get_str(yyjson_obj_get(meta, "script")) : "latin";
        yyjson_doc_free(doc);
        g_languages.push_back(l);
    }
    // ordre fixe : français, anglais, puis par code
    std::sort(g_languages.begin(), g_languages.end(), [](const Language & a, const Language & b) {
        auto rank = [](const std::string & c) { return c == "fr" ? 0 : c == "en" ? 1 : 2; };
        if (rank(a.code) != rank(b.code)) return rank(a.code) < rank(b.code);
        return a.code < b.code;
    });
}

const std::vector<Language> & languages() { return g_languages; }

std::string system_language() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
    std::wstring w = name;  // « fr-FR », « zh-HK », « zh-CN »…
    std::string  tag;
    for (wchar_t c : w) tag.push_back((char) c);
    std::string lang = tag.substr(0, tag.find('-'));
    std::string region = tag.find('-') == std::string::npos ? "" : tag.substr(tag.find('-') + 1);
    std::vector<std::string> wanted;
    if (lang == "zh") {
        // Hong Kong, Macao, Taïwan : chinois traditionnel (cantonais) ; le reste : mandarin simplifié
        if (region == "HK" || region == "MO") wanted = { "yue", "zh-Hant", "zh-Hans" };
        else if (region.find("Hant") != std::string::npos || region == "TW") wanted = { "zh-Hant", "yue", "zh-Hans" };
        else wanted = { "zh-Hans", "zh" };
    } else {
        wanted = { lang };
    }
    for (const std::string & w2 : wanted)
        if (find(w2)) return w2;
    if (find("en")) return "en";
    return "fr";
}

bool set_language(const std::string & code) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Language * l = find(code);
    if (!l) return false;
    g_strings.clear();
    g_fallback.clear();
    g_view.clear();
    g_glyphs.clear();
    g_current = code;
    load_strings(l->file, g_strings);  // le français aussi : son fichier permet d'ajuster les textes d'origine
    if (code != "en" && code != "fr") {
        if (const Language * en = find("en")) load_strings(en->file, g_fallback);
    }
    for (const auto & [k, v] : g_fallback) g_view[k] = &v;
    for (const auto & [k, v] : g_strings) g_view[k] = &v;  // la langue courante l'emporte sur l'anglais
    std::string all;
    for (const auto & [k, v] : g_strings) all += v;
    g_glyphs = to_u32(all);
    return true;
}

const std::string & current() { return g_current; }

std::string current_script() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Language * l = find(g_current);
    return l ? l->script : "latin";
}

const std::u32string & glyph_text() { return g_glyphs; }

static const std::string * lookup_locked(std::string_view key) {
    auto it = g_view.find(key);
    return it == g_view.end() ? nullptr : it->second;
}

const char * tr(const char * key) {
    if (!key || !*key) return key;
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string * v = lookup_locked(key);
    return v ? v->c_str() : key;
}

std::string tr(const std::string & key) {
    if (key.empty()) return key;
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string * v = lookup_locked(key);
    return v ? *v : key;
}

std::vector<const char *> tr_list(const char * const * items, int count) {
    std::vector<const char *> out;
    for (int i = 0; i < count; i++) out.push_back(tr(items[i]));
    return out;
}

std::string trf(const char * key, ...) {
    const char * format = tr(key);
    va_list      args, copy;
    va_start(args, key);
    va_copy(copy, args);
    // « %2$s » : marques positionnelles (ordre des mots différent selon la langue)
    const bool  positional = std::strchr(format, '$') != nullptr;
    int         n          = positional ? _vscprintf_p(format, copy) : vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string out;
    if (n > 0) {
        out.resize((size_t) n + 1);
        if (positional) _vsprintf_p(out.data(), out.size(), format, args);
        else vsnprintf(out.data(), out.size(), format, args);
        out.resize((size_t) n);
    }
    va_end(args);
    return out;
}

}  // namespace i18n
