# 1.1.017 : journal des erreurs ffmpeg, reprise des videos deja faites, pause (ffmpeg suspendu, carte graphique rendue).
# Fichiers CRLF : lus en texte, remplaces, ecrits avec CRLF. Chaque remplacement doit trouver exactement le nombre d'occurrences voulu.
import io, sys
SRC = r"C:\Users\deher\Downloads\dev\music\_developpement\native\src" + "\\"

def load(name):
    with io.open(SRC + name, "r", encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")

def save(name, text):
    with io.open(SRC + name, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace("\n", "\r\n"))

def rep(text, old, new, count=1):
    n = text.count(old)
    if n != count:
        sys.exit("ERREUR : %d occurrence(s) au lieu de %d pour : %r" % (n, count, old[:90]))
    return text.replace(old, new)

# ------------------------------------------------------------------ media.hpp
h = load("media.hpp")
h = rep(h, "    bool               announced = false;",
"""    // 1.1.017 : pause (clips et videos) : le processus en cours est suspendu et le modele d'images decharge (carte graphique rendue).
    std::atomic<bool>   paused{ false };
    bool                pausable = false;
    std::atomic<void *> suspended{ nullptr };
    std::mutex          pause_m;
    std::function<void()> on_pause;  // sous mutex : decharge le modele d'images
    std::string         ffmpeg_err;  // sous mutex : dernieres lignes d'erreur de ffmpeg (pour le journal et le message d'echec)
    void set_paused(bool p);
    void set_proc(void * h);   // processus courant (ffmpeg, Whisper) : suspendu tout de suite si le travail est en pause
    void wait_if_paused();     // bloque tant que le travail est en pause (ou annule)
    bool               announced = false;""")
h = rep(h, "    bool                              failed = false;",
        "    std::mutex                        m;  // 1.1.017 : tenu pendant une image ; une pause ne decharge le serveur que s'il est libre\n    bool                              failed = false;")
h = rep(h, "struct ClipTask : VideoTask {\n",
        "struct ClipTask : VideoTask {\n    std::shared_ptr<ImageServerSlot> images;  // 1.1.017 : serveur d'images a decharger pendant une pause\n")
save("media.hpp", h)

# ------------------------------------------------------------------ media.cpp
c = load("media.cpp")
c = rep(c, "void Job::stop() {\n    cancel = true;\n",
"""static void nt_proc(void * h, bool suspend) {  // 1.1.017
    using NtProc = LONG(NTAPI *)(HANDLE);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto    f  = nt ? (NtProc) GetProcAddress(nt, suspend ? "NtSuspendProcess" : "NtResumeProcess") : nullptr;
    if (f) f((HANDLE) h);
}

void Job::set_proc(void * h) {
    std::lock_guard<std::mutex> lock(pause_m);
    proc = h;
    if (!h) {
        suspended = nullptr;
        return;
    }
    if (paused && suspended.load() != h) {
        nt_proc(h, true);
        suspended = h;
    }
}

void Job::set_paused(bool p) {
    std::function<void()> release;
    {
        std::lock_guard<std::mutex> lock(pause_m);
        if (paused.exchange(p) == p) return;
        if (p) {
            if (void * h = proc.load()) {
                nt_proc(h, true);
                suspended = h;
            }
            std::lock_guard<std::mutex> lk(mutex);
            release = on_pause;
        } else if (void * h = suspended.exchange(nullptr)) {
            nt_proc(h, false);
        }
    }
    journal::event("travail", name + (p ? " : pause" : " : reprise"));
    if (release) release();
}

void Job::wait_if_paused() {
    while (paused && !cancel) Sleep(200);
}

void Job::stop() {
    cancel = true;
    paused = false;
""")

# ffmpeg() : erreurs gardees (le flux d'erreurs partait dans le vide quand on lisait la progression sur la sortie normale)
old_ff = c[c.index("// ffmpeg avec suivi du processus"):c.index("static std::string two(int n)")]
new_ff = '''// ffmpeg avec suivi du processus (pour l'annulation). Renvoie le code de sortie.
// 1.1.017 : la progression (-progress pipe:1) passe sur le flux d'erreurs (pipe:2) : les deux arrivent par le meme tuyau, la
// progression va a on_line et le reste (le message d'erreur de ffmpeg) est garde dans job.ffmpeg_err, ecrit au journal en cas d'echec.
static bool ffmpeg_progress_line(const std::string & l) {
    static const char * keys[] = { "frame=", "fps=", "stream_", "bitrate=", "total_size=", "out_time_us=", "out_time_ms=", "out_time=",
                                   "dup_frames=", "drop_frames=", "speed=", "progress=" };
    for (const char * k : keys)
        if (l.rfind(k, 0) == 0) return true;
    return false;
}

static int ffmpeg(Job & job, const std::wstring & args, const std::function<void(const std::string &)> & on_line = nullptr,
                  bool read_stdout = false) {
    { std::lock_guard<std::mutex> lock(job.mutex); job.ffmpeg_err.clear(); }
    std::wstring       a    = args;
    const std::wstring from = L"-progress pipe:1";
    bool               on_stderr = false;
    if (read_stdout) {
        const size_t p = a.find(from);
        if (p != std::wstring::npos) {
            a.replace(p, from.size(), L"-progress pipe:2");
            on_stderr = true;
        }
    }
    auto handler = [&](const std::string & l) {
        const bool progress = on_stderr && ffmpeg_progress_line(l);
        if (!progress) {
            std::lock_guard<std::mutex> lock(job.mutex);
            job.ffmpeg_err += l + "\\n";
            if (job.ffmpeg_err.size() > 1500) job.ffmpeg_err.erase(0, job.ffmpeg_err.size() - 1500);
        }
        if (on_line && (progress || !on_stderr)) on_line(l);
    };
    return run_command(ffmpeg_exe() + L" -v error -y " + a, handler, [&job](void * h) { job.set_proc(h); }, on_stderr ? false : read_stdout);
}

// Message d'echec d'une video : code de ffmpeg et, quand il en a donne un, sa raison (aussi ecrite au journal).
static std::string ffmpeg_fail(Job & j, int code, const fs::path & file) {
    std::string text = trf("échec ffmpeg (%d) : %s", code, narrow(file.filename().wstring()).c_str());
    std::string tail;
    { std::lock_guard<std::mutex> lock(j.mutex); tail = j.ffmpeg_err; }
    while (!tail.empty() && (tail.back() == '\\n' || tail.back() == ' ')) tail.pop_back();
    if (!tail.empty()) {
        for (size_t p = 0; (p = tail.find('\\n', p)) != std::string::npos; p += 5) tail.replace(p, 1, "\\n    ");
        text += "\\n    " + tail;
        journal::error("ffmpeg", text);
    }
    return text;
}

'''
c = c.replace(old_ff, new_ff)
c = rep(c, 'errors += trf("échec ffmpeg (%d) : %s", (int) code, narrow(t.output.filename().wstring()).c_str()) + "\\n";',
           'errors += ffmpeg_fail(j, (int) code, t.output) + "\\n";', 2)
c = rep(c, "run_command(cmd, nullptr, [&j](void * h) { j.proc = h; });\n                        std::ifstream f(base.wstring()",
           "run_command(cmd, nullptr, [&j](void * h) { j.set_proc(h); });\n                        std::ifstream f(base.wstring()")
# points d'arret
c = rep(c, "            const VideoTask & t = tasks[i];\n", "            j.wait_if_paused();\n            if (j.cancel) break;\n            const VideoTask & t = tasks[i];\n")
c = rep(c, "            const ClipTask & t = tasks[i];\n", "            j.wait_if_paused();\n            if (j.cancel) break;\n            const ClipTask & t = tasks[i];\n")
c = rep(c, "            if (j.cancel || !t.make_scenario || !planned.insert(t.song.id).second) continue;",
           "            j.wait_if_paused();\n            if (j.cancel || !t.make_scenario || !planned.insert(t.song.id).second) continue;")
c = rep(c, "void start_videos(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<VideoTask> tasks) {\n",
           "void start_videos(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<VideoTask> tasks) {\n    job->pausable = true;  // 1.1.017\n")
c = rep(c, "void start_clips(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<ClipTask> tasks) {\n    (void) paths;\n",
"""void start_clips(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<ClipTask> tasks) {
    (void) paths;
    job->pausable = true;  // 1.1.017 : pause = ffmpeg suspendu + modele d'images decharge (carte graphique rendue)
    {
        std::vector<std::shared_ptr<ImageServerSlot>> slots;
        for (const ClipTask & t : tasks)
            if (t.images && std::find(slots.begin(), slots.end(), t.images) == slots.end()) slots.push_back(t.images);
        if (!slots.empty()) {
            std::lock_guard<std::mutex> lk(job->mutex);
            job->on_pause = [slots]() {
                for (auto & s : slots)
                    if (s->m.try_lock()) {  // occupe (image en cours) : le travail le decharge lui-meme a l'image suivante
                        s->server.reset();
                        s->m.unlock();
                    }
            };
        }
    }
""")
c = rep(c, "        tasks.clear();  // 1.0.100",
           "        { std::lock_guard<std::mutex> lk(j.mutex); j.on_pause = nullptr; }  // 1.1.017 : lache aussi les serveurs d'images\n        tasks.clear();  // 1.0.100")
# images de scene : pause entre deux images
c = rep(c, '''            say(trf("Images de « %s » : %d sur %d…", s.title.c_str(), (int) k + 1, (int) sb.scenes.size()));''',
'''            if (j.paused) {  // 1.1.017 : pause : serveur d'images arrete (carte graphique rendue), repris a la reprise
                { std::lock_guard<std::mutex> lk(eng.images->m); eng.images->server.reset(); }
                say(tr("En pause : carte graphique libérée."));
                j.wait_if_paused();
                if (j.cancel) return false;
            }
            std::unique_lock<std::mutex> slot_lock(eng.images->m);  // une pause ne decharge pas le serveur pendant une image
            say(trf("Images de « %s » : %d sur %d…", s.title.c_str(), (int) k + 1, (int) sb.scenes.size()));''')
save("media.cpp", c)

# ------------------------------------------------------------------ app.hpp
a = load("app.hpp")
a = rep(a, "    bool              video_redo = false;",
        "    bool              video_skip_done = true;  // 1.1.017 : reprise : une video deja creee n'est pas refaite (sans effet si « Refaire » est coche)\n    bool              video_redo = false;")
save("app.hpp", a)

# ------------------------------------------------------------------ app_media.cpp
m = load("app_media.cpp")
m = rep(m, "    video_redo = false;\n    auto clip_task = [&](const VideoTask & t) {",
"""    const bool skip_done = skip_existing_clips || (video_skip_done && !video_redo);  // 1.1.017
    int        skipped   = 0;
    auto       skip_note = [&]() { return skipped ? std::string(" ") + trf("%d vidéo(s) déjà faite(s) : ignorée(s).", skipped) : std::string(); };
    video_redo = false;
    auto clip_task = [&](const VideoTask & t) {""")
m = rep(m, "        if (with_scenes) {\n            c.make_scenes = [scene_engine]",
           "        if (with_scenes) {\n            c.images = scene_engine.images;  // 1.1.017 : dechargé par la pause\n            c.make_scenes = [scene_engine]")
m = rep(m, "            if (clip && skip_existing_clips) {\n                std::error_code ec5;\n                if (fs::exists(t.output, ec5)) continue;\n            }",
           "            if (skip_done) {\n                std::error_code ec5;\n                if (fs::exists(t.output, ec5)) {\n                    skipped++;\n                    continue;\n                }\n            }")
m = rep(m, "            if (skip_existing_clips && fs::exists(t.output, ec7)) continue;",
           "            if (skip_done && fs::exists(t.output, ec7)) {\n                skipped++;\n                continue;\n            }")
m = rep(m, 'notify(trf("%d titre(s) : création des clips en cours.", (int) video_songs.size()));',
           'notify(trf("%d titre(s) : création des clips en cours.", (int) video_songs.size()) + skip_note());')
m = rep(m, "    if (tasks.empty()) {\n        if (clip) notify(",
           "    if (tasks.empty() && skipped > 0) {\n        notify(tr(\"Toutes les vidéos demandées existent déjà (décochez « Reprendre » pour les refaire).\"));\n        return;\n    }\n    if (tasks.empty()) {\n        if (clip) notify(")
m = rep(m, 'notify(trf("%d titre(s) : création des vidéos en cours.", job ? (int) video_songs.size() : 0));',
           'notify(trf("%d titre(s) : création des vidéos en cours.", job ? (int) video_songs.size() : 0) + skip_note());')
m = rep(m, '        ImGui::Checkbox(tr("Refaire le scénario et les images',
'''        ImGui::Checkbox(tr("Reprendre : ne pas refaire les vidéos déjà créées"), &video_skip_done);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Après une interruption, seules les vidéos qui manquent sont créées. Sans effet si « Refaire le scénario et les images » est coché."));
        ImGui::Checkbox(tr("Refaire le scénario et les images''')
m = rep(m, "            progress_bar(j.progress, ImVec2(-FLT_MIN, 0));\n            if (danger_button(tr(\"Annuler\"))) j.stop();",
"""            progress_bar(j.progress, ImVec2(-FLT_MIN, 0));
            if (j.pausable) {  // 1.1.017 : pause = ffmpeg suspendu, modèle d'images déchargé (carte graphique rendue)
                const bool pz = j.paused;
                if (pz) muted_wrapped(tr("En pause : le travail reprendra au clic sur « Reprendre »."));
                if (soft_button(pz ? tr("Reprendre") : tr("Pause"), P.teal)) j.set_paused(!pz);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Suspend la création et rend la carte graphique (à la fin de l'image en cours). La création reprend là où elle s'est arrêtée."));
                ImGui::SameLine();
            }
            if (danger_button(tr("Annuler"))) j.stop();""")
save("app_media.cpp", m)

# ------------------------------------------------------------------ main.cpp (essai)
mm = load("main.cpp")
mm = rep(mm, "            start_clips(job, studio.paths(), test_tasks);\n            while (job->state == 0) Sleep(500);",
"""            start_clips(job, studio.paths(), test_tasks);
            std::thread pause_t;
            if (const wchar_t * pz = _wgetenv(L"STUDIO_TEST_PAUSE")) {  // 1.1.017 : « a,b » : pause apres a s, pendant b s
                int a = 0, b = 0;
                swscanf(pz, L"%d,%d", &a, &b);
                pause_t = std::thread([job, a, b] { Sleep(a * 1000); job->set_paused(true); Sleep(b * 1000); job->set_paused(false); });
            }
            while (job->state == 0) Sleep(500);
            if (pause_t.joinable()) pause_t.join();""")
save("main.cpp", mm)

# ------------------------------------------------------------------ version
v = load("version.hpp")
v = rep(v, "1.1.016", "1.1.017", 2)
save("version.hpp", v)
print("OK")
