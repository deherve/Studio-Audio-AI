// Recherche d'une version plus récente du Studio Audio AI (voir update_check.hpp).
#include "update_check.hpp"

#include <cstdio>
#include <vector>

#include "net.hpp"
#include "version.hpp"
#include "yyjson.h"

// « v1.0.12 » -> { 1, 0, 12 }
static std::vector<int> version_parts(const std::string & v) {
    std::vector<int> out;
    size_t           i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    int              n = 0;
    bool             any = false;
    for (; i < v.size(); i++) {
        char c = v[i];
        if (c >= '0' && c <= '9') {
            n   = n * 10 + (c - '0');
            any = true;
        } else if (c == '.') {
            out.push_back(n);
            n   = 0;
            any = false;
        } else {
            break;  // « 1.0.4-beta » : on s'arrête au suffixe
        }
    }
    if (any) out.push_back(n);
    return out;
}

int compare_versions(const std::string & a, const std::string & b) {
    std::vector<int> x = version_parts(a), y = version_parts(b);
    for (size_t i = 0; i < x.size() || i < y.size(); i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

void check_for_update(std::shared_ptr<UpdateInfo> info, std::string manifest_url) {
    if (manifest_url.empty()) {  // aucune adresse de publication pour l'instant
        info->state = 0;
        return;
    }
    info->state = 1;
    std::string body;
    int         code = https_get_url(manifest_url, body);
    yyjson_doc * doc = code == 200 ? yyjson_read(body.data(), body.size(), 0) : nullptr;
    if (!doc) {
        info->state = 4;
        return;
    }
    yyjson_val * root = yyjson_doc_get_root(doc);
    auto         str  = [&](const char * key) -> std::string {
        yyjson_val * v = yyjson_obj_get(root, key);
        return yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string();
    };
    std::string latest = str("tag_name");  // format GitHub
    std::string url    = str("html_url");
    std::string notes  = str("body");
    if (latest.empty()) {  // petit fichier JSON
        latest = str("version");
        url    = str("url");
        notes  = str("notes");
    }
    yyjson_doc_free(doc);
    if (latest.empty()) {
        info->state = 4;
        return;
    }
    if (latest[0] == 'v' || latest[0] == 'V') latest = latest.substr(1);
    if (notes.size() > 600) notes = notes.substr(0, 600) + "…";
    info->latest = latest;
    info->url    = url;
    info->notes  = notes;
    info->state  = compare_versions(latest, STUDIO_VERSION) > 0 ? 3 : 2;  // écrit en dernier : les champs sont prêts
}
