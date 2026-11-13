# 1.1.013 : révision de studio.cpp (corrections et optimisation de rescan)
import sys
p = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src\studio.cpp'
s = open(p, encoding='utf-8', newline='').read()
NL = '\r\n'


def rep(a, b, count=1):
    global s
    a = a.replace('\n', NL)
    b = b.replace('\n', NL)
    if s.count(a) != count:
        print('ERREUR', s.count(a), a[:80])
        sys.exit(1)
    s = s.replace(a, b)


# (a) écriture atomique : ne jamais remplacer le bon fichier par un fichier incomplet (disque plein, erreur d'écriture)
rep('''    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data, (std::streamsize) size);
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}''', '''    bool written = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data, (std::streamsize) size);
        out.flush();
        written = out.good();
    }
    // 1.1.013 : écriture incomplète (disque plein…) : l'ancien fichier est gardé tel quel, le brouillon est retiré.
    if (written) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(tmp.c_str());
}''')

# (b) libellés d'étape non traduits (les autres endroits passent déjà par tr())
rep('''    song.stage    = held ? "En pause" : "En attente";''', '''    song.stage    = held ? tr("En pause") : tr("En attente");''')
rep('''            s.stage = held ? "En pause" : "En attente";''', '''            s.stage = held ? tr("En pause") : tr("En attente");''')
rep('''                s.stage = closed ? "En attente (jeu en cours)" : "En attente";''',
    '''                s.stage = closed ? tr("En attente (jeu en cours)") : tr("En attente");''')

# (f) fichier source d'une reprise : chemin UTF-8 (un nom accentué était lu dans la page de codes de Windows)
rep('''            fs::path src = song.source_file;''', '''            fs::path src = widen(song.source_file);  // 1.1.013 : UTF-8 (noms accentués)''')

# (c) rescan : un seul appel système par morceau, et recherche du dossier sans double boucle
rep('''        fs::path meta = it->path() / "meta.json";
        if (fs::exists(meta, ec)) {
            metas.emplace_back(meta, fs::last_write_time(meta, ec));
            it.disable_recursion_pending();  // un morceau ne contient pas d'autre morceau
        }
    }
    bool                        changed = false;
    std::map<std::string, bool> seen;
    for (auto & [meta, mtime] : metas) {
        // identifiant : celui de meta.json (dossier nommé par le titre), mémorisé par dossier pour les scans suivants
        std::string id;
        {
            std::lock_guard<std::mutex> lock(g_folders_m);
            for (const auto & [k, v] : g_folders)
                if (v == meta.parent_path()) {
                    id = k;
                    break;
                }
        }
        if (id.empty()) {''', '''        fs::path meta = it->path() / "meta.json";
        // 1.1.013 : présence et date en un seul appel (avant : fs::exists + fs::last_write_time, deux ouvertures par morceau,
        // toutes les 2 secondes). Même valeur que fs::last_write_time (100 ns depuis 1601), comparée à mtimes_.
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(meta.c_str(), GetFileExInfoStandard, &fa) && !(fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const long long ticks = ((long long) fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
            metas.emplace_back(meta, fs::file_time_type(fs::file_time_type::duration(ticks)));
            it.disable_recursion_pending();  // un morceau ne contient pas d'autre morceau
        }
    }
    bool                        changed = false;
    std::map<std::string, bool> seen;
    // 1.1.013 : dossier -> identifiant, construit une fois (avant : toute la liste parcourue pour chaque morceau)
    std::unordered_map<std::wstring, std::string> by_dir;
    {
        std::lock_guard<std::mutex> lock(g_folders_m);
        by_dir.reserve(g_folders.size());
        for (const auto & [k, v] : g_folders) by_dir.emplace(v.native(), k);
    }
    for (auto & [meta, mtime] : metas) {
        // identifiant : celui de meta.json (dossier nommé par le titre), mémorisé par dossier pour les scans suivants
        std::string id;
        if (auto found = by_dir.find(meta.parent_path().native()); found != by_dir.end()) id = found->second;
        if (id.empty()) {''')
rep('#include <sstream>\n', '#include <sstream>\n#include <unordered_map>\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('OK')
