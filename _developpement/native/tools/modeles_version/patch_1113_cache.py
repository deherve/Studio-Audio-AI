# 1.1.013 : cache « le fichier existe-t-il ? » invalidé par les changements réels du dossier Audios (au lieu de 3 secondes)
import sys
p = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src\main.cpp'
s = open(p, encoding='utf-8', newline='').read()
NL = '\r\n'


def rep(a, b):
    global s
    a = a.replace('\n', NL)
    b = b.replace('\n', NL)
    if s.count(a) != 1:
        print('ERREUR', s.count(a), a[:80])
        sys.exit(1)
    s = s.replace(a, b)


rep('''    mutable std::map<std::wstring, std::pair<bool, double>> exists_cache;
    bool cached_exists(const fs::path & p) const {
        double now = ImGui::GetTime();
        auto   it  = exists_cache.find(p.native());
        if (it != exists_cache.end() && now - it->second.second < 3.0) return it->second.first;
        std::error_code ec;
        bool            e = fs::exists(p, ec);
        if (exists_cache.size() > 4096) exists_cache.clear();
        exists_cache[p.native()] = { e, now };
        return e;
    }
''', '''    // 1.1.013 : sous Audios, la réponse reste valable tant que Windows ne signale aucun changement dans ce dossier (fs_gen,
    // avancé par la boucle principale, au plus toutes les 3 s) et 60 s au plus ; avant, tout était revérifié toutes les 3 s :
    // ≈ 1 800 accès disque d'un coup pour 148 morceaux, soit une image saccadée toutes les 3 s dans la bibliothèque.
    // Ailleurs (ou sans surveillance possible) : 3 secondes, comme avant.
    unsigned     fs_gen = 0;        // avancé quand le dossier Audios a changé
    std::wstring fs_watch_root;     // dossier surveillé, en minuscules, avec « \\ » final ; vide = pas de surveillance
    struct FsEntry { int value; double at; unsigned gen; bool watched; };
    bool fs_entry_valid(const FsEntry & e, double now) const {
        return e.watched ? e.gen == fs_gen && now - e.at < 60.0 : now - e.at < 3.0;
    }
    bool fs_watched(const std::wstring & path) const {
        return !fs_watch_root.empty() && path.size() > fs_watch_root.size() && _wcsnicmp(path.c_str(), fs_watch_root.c_str(), fs_watch_root.size()) == 0;
    }
    mutable std::map<std::wstring, FsEntry> exists_cache;
    bool cached_exists(const fs::path & p) const {
        double now = ImGui::GetTime();
        auto   it  = exists_cache.find(p.native());
        if (it != exists_cache.end() && fs_entry_valid(it->second, now)) return it->second.value != 0;
        std::error_code ec;
        bool            e = fs::exists(p, ec);
        if (exists_cache.size() > 4096) exists_cache.clear();
        exists_cache[p.native()] = { e ? 1 : 0, now, fs_gen, fs_watched(p.native()) };
        return e;
    }
''')
rep('''    mutable std::map<std::wstring, std::pair<int, double>> video_cache;
    int video_count(const fs::path & dir) const {
        double now = ImGui::GetTime();
        auto   it  = video_cache.find(dir.native());
        if (it != video_cache.end() && now - it->second.second < 3.0) return it->second.first;''', '''    mutable std::map<std::wstring, FsEntry> video_cache;
    int video_count(const fs::path & dir) const {
        double now = ImGui::GetTime();
        auto   it  = video_cache.find(dir.native());
        if (it != video_cache.end() && fs_entry_valid(it->second, now)) return it->second.value;''')
rep('''        video_cache[dir.native()] = { n, now };''', '''        video_cache[dir.native()] = { n, now, fs_gen, fs_watched(dir.native() + L"\\\\") };''')

# boucle principale : génération du cache avancée au plus toutes les 3 s
rep('''        if (library_watch != INVALID_HANDLE_VALUE && WaitForSingleObject(library_watch, 0) == WAIT_OBJECT_0) {
            library_dirty = true;
            FindNextChangeNotification(library_watch);
        }''', '''        if (library_watch != INVALID_HANDLE_VALUE && WaitForSingleObject(library_watch, 0) == WAIT_OBJECT_0) {
            library_dirty = fs_dirty = true;
            FindNextChangeNotification(library_watch);
        }
        if (fs_dirty && ImGui::GetTime() - last_fs_gen >= 3.0) {  // pendant une génération, le dossier change sans arrêt : 3 s au moins
            app.fs_gen++;
            fs_dirty    = false;
            last_fs_gen = ImGui::GetTime();
            frames      = std::max(frames, 2);  // cartes redessinées avec les fichiers apparus ou disparus
        }''')
rep('''    bool   library_dirty = false;
''', '''    bool   library_dirty = false, fs_dirty = false;
    double last_fs_gen   = -10;
''')
rep('''    // Banc de mesure (1.1.013)''', '''    if (library_watch != INVALID_HANDLE_VALUE) {
        app.fs_watch_root = paths.library.native();
        if (!app.fs_watch_root.empty() && app.fs_watch_root.back() != L'\\\\') app.fs_watch_root += L'\\\\';
    }
    // Banc de mesure (1.1.013)''')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('OK')
