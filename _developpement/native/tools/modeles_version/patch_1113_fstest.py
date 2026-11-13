# 1.1.013 : essai sans écran du cache de présence des fichiers (STUDIO_TEST_FSCACHE=<fichier de sortie>)
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


rep('''            frames      = std::max(frames, 2);  // cartes redessinées avec les fichiers apparus ou disparus
        }''', '''            frames      = std::max(frames, 2);  // cartes redessinées avec les fichiers apparus ou disparus
        }
        // Essai (1.1.013) : « STUDIO_TEST_FSCACHE=<fichier> » crée puis supprime un fichier sous Audios et mesure en combien de
        // temps cached_exists le voit apparaître puis disparaître (marche aussi fenêtre masquée : pas besoin de capture).
        if (fscache_out) {
            const fs::path probe = paths.library / L"essai_cache_1113.tmp";
            const double   now   = ImGui::GetTime();
            const bool     seen  = app.cached_exists(probe);
            if (fscache_step == 0 && now > 6.0) {  // démarrage passé ; le fichier n'existe pas encore (réponse gardée en cache)
                fscache_log += seen ? "ERREUR : vu avant sa creation\\n" : "absent au depart : OK\\n";
                { std::ofstream f(probe, std::ios::binary); f << "essai"; }
                fscache_t0   = now;
                fscache_step = 1;
            } else if (fscache_step == 1 && (seen || now - fscache_t0 > 20)) {
                char line[96];
                snprintf(line, sizeof(line), "%s apres creation en %.1f s\\n", seen ? "vu" : "ERREUR : jamais vu", now - fscache_t0);
                fscache_log += line;
                std::error_code fec;
                fs::remove(probe, fec);
                fscache_t0   = now;
                fscache_step = 2;
            } else if (fscache_step == 2 && (!seen || now - fscache_t0 > 20)) {
                char line[96];
                snprintf(line, sizeof(line), "%s apres suppression en %.1f s\\n", !seen ? "disparu" : "ERREUR : toujours vu", now - fscache_t0);
                fscache_log += line;
                if (FILE * f = _wfopen(fscache_out, L"wb")) {
                    fputs(fscache_log.c_str(), f);
                    fclose(f);
                }
                fscache_out = nullptr;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            frames = std::max(frames, 1);  // la boucle continue de tourner pendant l'essai
        }''')
rep('''    bool   library_dirty = false, fs_dirty = false;
''', '''    bool   library_dirty = false, fs_dirty = false;
    const wchar_t * fscache_out  = _wgetenv(L"STUDIO_TEST_FSCACHE");
    int             fscache_step = 0;
    double          fscache_t0   = 0;
    std::string     fscache_log;
''')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('OK')
