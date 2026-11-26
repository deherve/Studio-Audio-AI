#include "app.hpp"

// ---------------------------------------------------------------------------- Point d'entrée (essais, fenêtre, boucle)
// ---------------------------------------------------------------------------- Point d'entrée

// Construit les polices pour la langue courante : Segoe UI pour le latin ; pour chaque écriture asiatique, la police
// Windows correspondante fusionnée dans la même police. L'écriture de la langue courante reçoit ses caractères
// usuels et tous ceux de son fichier de langue ; les autres écritures ne reçoivent que les noms de langue
// (pour que la liste des langues reste lisible dans toutes les langues).
static ImVector<ImWchar> g_latin_ranges;
static ImVector<ImWchar> g_cjk_ranges[4];
void build_fonts(bool runtime) {
    ImGuiIO & io = ImGui::GetIO();
    wchar_t   windir[MAX_PATH];
    GetWindowsDirectoryW(windir, MAX_PATH);
    std::string       fonts  = narrow(windir) + "\\Fonts\\";
    const std::string script = i18n::current_script();
    io.Fonts->Clear();

    static const ImWchar text_ranges[] = { 0x0020, 0x017F, 0x0370, 0x052F, 0x1E00, 0x1EFF, 0x2010, 0x205E, 0x20A0, 0x20CF, 0x2248, 0x2248, 0 };
    // 1.1.013 : signe moins (2212), étoiles ★ ☆ (2605-2606), note ♪ (266A) et croix ✕ (2715) ajoutés : ils s'affichaient « ? »
    // (étoiles des modèles d'IA, boutons d'annulation, pastilles du style, pochette sans image).
    static const ImWchar symbol_ranges[] = { 0x2190, 0x21FF, 0x2212, 0x2212, 0x23F8, 0x23FA, 0x25A0, 0x25FF, 0x2605, 0x2606, 0x266A, 0x266A, 0x2713, 0x2715, 0 };
    static const ImWchar punct[] = { 0x2010, 0x2027, 0x3000, 0x303F, 0x3040, 0x30FF, 0xFF00, 0xFFEF, 0 };
    struct Asian { const char * script; const char * regular; const char * bold; };
    static const Asian asian[4] = { { "ja", "YuGothM.ttc", "YuGothB.ttc" }, { "ko", "malgun.ttf", "malgunbd.ttf" },
                                    { "zh-Hans", "msyh.ttc", "msyhbd.ttc" }, { "zh-Hant", "msjh.ttc", "msjhbd.ttc" } };
    auto is_base = [](char32_t c) { return c < 0x250 || (c >= 0x370 && c <= 0x52F) || (c >= 0x1E00 && c <= 0x1EFF) || (c >= 0x2000 && c <= 0x206F) || (c >= 0x20A0 && c <= 0x20CF); };
    auto is_sym  = [](char32_t c) { return (c >= 0x2190 && c <= 0x21FF) || (c >= 0x23F8 && c <= 0x23FA) || (c >= 0x25A0 && c <= 0x25FF) || (c >= 0x2713 && c <= 0x2715) || c == 0x2248 || c == 0x2212 || c == 0x2605 || c == 0x2606 || c == 0x266A; };

    ImFontGlyphRangesBuilder latin, cjk[4];
    latin.AddRanges(text_ranges);
    for (int k = 0; k < 4; k++) {
        // noms des langues écrites dans cette écriture
        for (const i18n::Language & l : i18n::languages()) {
            if (l.script != asian[k].script) continue;
            cjk[k].AddText(l.name.c_str());
        }
    }
    for (char32_t c : i18n::glyph_text()) {
        if (c > 0xFFFF || is_sym(c)) continue;
        if (is_base(c)) latin.AddChar((ImWchar) c);
        else for (int k = 0; k < 4; k++) if (script == asian[k].script) cjk[k].AddChar((ImWchar) c);
    }
    for (int k = 0; k < 4; k++) {
        if (script != asian[k].script) continue;
        cjk[k].AddRanges(punct);
        if (script == "ja") cjk[k].AddRanges(io.Fonts->GetGlyphRangesJapanese());
        else if (script == "ko") cjk[k].AddRanges(io.Fonts->GetGlyphRangesKorean());
        else if (script == "zh-Hans") cjk[k].AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        else {
            // caractères traditionnels usuels : les 5 401 du niveau 1 de Big5 (page de codes 950)
            for (int lead = 0xA4; lead <= 0xC6; lead++) {
                for (int trail = 0x40; trail <= 0xFE; trail++) {
                    if (trail > 0x7E && trail < 0xA1) continue;
                    char    bytes[2] = { (char) lead, (char) trail };
                    wchar_t w        = 0;
                    if (MultiByteToWideChar(950, MB_ERR_INVALID_CHARS, bytes, 2, &w, 1) == 1 && w >= 0x2E80) cjk[k].AddChar((ImWchar) w);
                }
            }
        }
    }
    g_latin_ranges.clear();
    latin.BuildRanges(&g_latin_ranges);
    for (int k = 0; k < 4; k++) {
        g_cjk_ranges[k].clear();
        cjk[k].BuildRanges(&g_cjk_ranges[k]);
    }

    auto add = [&](const char * latin_file, bool bold, float size) -> ImFont * {
        ImFont * f = io.Fonts->AddFontFromFileTTF((fonts + latin_file).c_str(), size, nullptr, g_latin_ranges.Data);
        if (!f) return io.Fonts->AddFontDefault();
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF((fonts + "seguisym.ttf").c_str(), size, &merge, symbol_ranges);
        for (int k = 0; k < 4; k++) {
            if (g_cjk_ranges[k].Size <= 1) continue;
            ImFontConfig c;
            c.MergeMode   = true;
            c.OversampleH = c.OversampleV = 1;
            io.Fonts->AddFontFromFileTTF((fonts + (bold ? asian[k].bold : asian[k].regular)).c_str(), size, &c, g_cjk_ranges[k].Data);
        }
        return f;
    };
    float size = std::round(16.f * g_dpi);
    add("segoeui.ttf", false, size);
    g_bold   = add("segoeuib.ttf", true, size);
    g_header = add("segoeuib.ttf", true, std::round(21.f * g_dpi));
    if (!g_bold) g_bold = io.Fonts->Fonts[0];
    if (!g_header) g_header = io.Fonts->Fonts[0];
    if (runtime) {
        io.Fonts->Build();
        ImGui_ImplDX11_InvalidateDeviceObjects();  // la texture des polices est recréée à l'image suivante
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    Paths paths;
    if (!Paths::locate(fs::path(exe).parent_path(), paths)) {
        MessageBoxW(nullptr,
                    L"Impossible de trouver le moteur de synthèse (yue-synth.exe).\n"
                    L"Placez Studio Audio AI.exe dans le dossier du projet (à côté de yue2.cpp\\build\\Release\\).",
                    APP_NAME_W, MB_ICONERROR);
        return 1;
    }
    // Carte graphique : marque (AMD, Nvidia, Intel), nom, mémoire ; moteur CUDA installé ou non.
    gpu_profile().gpu            = detect_gpu();
    gpu_profile().cuda_installed = !paths.cuda_dir.empty();
    // Mode de test (sans fenêtre) : « --analyze fichier sortie.txt » écrit le résultat de l'analyse audio.
    {
        int      argc = 0;
        wchar_t ** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Dossier des modèles : celui du programme (1.0.30 : plus de chemin « models » codé en dur).
        if (argv && argc >= 4 && paths.models_dir.empty()) paths.use_models(paths.root / "modeles");
        fs::path models_dir = paths.models_dir.empty() ? paths.root / "modeles" : paths.models_dir;
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--lang") {  // essai : langue chantée d'un fichier
            float avg = 0;
            int   votes = 0;
            std::string code = detect_audio_language(argv[2], paths.whisper, models_dir / "voix" / whisper_file().name,
                                                     app_temp_dir() / L"langue_test", media_duration(argv[2]), avg, votes);
            FILE * f = _wfopen(argv[3], L"wb");
            if (f) {
                std::string vl;
                int         vw = 0;
                detect_audio_vocals(argv[2], paths.whisper, models_dir / "voix" / whisper_file().name,
                                    app_temp_dir() / L"langue_test", media_duration(argv[2]), vl, vw);
                fprintf(f, "%s avg=%.2f votes=%d | vocals lang=%s words=%d", code.c_str(), avg, votes, vl.c_str(), vw);
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.33) : « --ai-test plan.txt sortie.txt » lit un plan d'album écrit par une IA et vérifie la
        // recherche des mots interdits et des mots fréquents, sans interface ni IA.
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--ai-test") {
            AiAlbumRequest r;
            r.count         = 6;
            r.kind          = 2;
            r.instrumentals = 2;
            r.influence     = "AC/DC";
            r.avoid         = ai_default_avoid_words();
            r.negative      = "vocoder, autotune";
            AiAlbumPlan plan = ai_parse_album_plan(read_text(argv[2]), r);
            FILE *      f    = _wfopen(argv[3], L"wb");
            if (f) {
                fprintf(f, "ok=%d band=%s album=%s sound=%s tracks=%d\n", (int) plan.ok, plan.band.c_str(), plan.album.c_str(), plan.sound.c_str(), (int) plan.tracks.size());
                for (size_t i = 0; i < plan.tracks.size(); i++) {
                    const AiTrackPlan & t = plan.tracks[i];
                    fprintf(f, "%d. [%s] inst=%d bpm=%d mood=%s theme=%s\n", (int) i + 1, t.title.c_str(), (int) t.instrumental, t.bpm, t.mood.c_str(), t.theme.c_str());
                }
                if (plan.ok) fprintf(f, "context 2: %s\n", ai_track_context(plan, 1).c_str());
                fprintf(f, "cover_prompt: %s\n", plan.cover_prompt.c_str());  // 1.0.36
                {   // 1.0.35 : mélangé, « l'IA répartit » → jamais plus de 2 instrumentaux
                    AiAlbumRequest r0 = r;
                    r0.instrumentals  = 0;
                    AiAlbumPlan p0    = ai_parse_album_plan(read_text(argv[2]), r0);
                    int         k     = 0;
                    for (const AiTrackPlan & t : p0.tracks) k += t.instrumental ? 1 : 0;
                    fprintf(f, "mixed auto: %d instrumental(s)\n", k);
                }
                auto show = [&](const char * label, const std::vector<std::string> & v) {
                    std::string s;
                    for (const std::string & w : v) s += (s.empty() ? "" : ", ") + w;
                    fprintf(f, "%s: %s\n", label, s.c_str());
                };
                show("found (Les Rêves de la NUIT, Cœurs perdus)", ai_forbidden_found("Les Rêves de la NUIT\nCœurs perdus sous l'orage", r.avoid));
                show("found (Lumières / lumineux / Starlight)", ai_forbidden_found("Lumières et lumineux, Starlight", r.avoid));
                show("found (Shadows and Echoes)", ai_forbidden_found("Shadows and Echoes", r.avoid));
                // 1.0.34 : nom du groupe / titre de l'album, doublons, styles à éviter
                show("plan forbidden (IA)", ai_plan_forbidden(plan, r));
                {
                    AiAlbumRequest typed = r;
                    typed.band           = plan.band;
                    typed.album          = plan.album;  // tapés par l'utilisateur : jamais vérifiés
                    show("plan forbidden (tapés)", ai_plan_forbidden(plan, typed));
                }
                {
                    int removed = 0;
                    show("dedupe", ai_dedupe_words({ "Rêve", "rêves", "NUIT", "nuit", "cœur", "coeurs", "étoile", "Etoiles", "feu", "feux", "star", "stars", "ombre" }, removed));
                    fprintf(f, "dedupe removed=%d\n", removed);
                }
                // 1.0.37 : titre seul redemandé
                fprintf(f, "parse_title: [%s] [%s] [%s]\n", ai_parse_title("{\"title\": \"Ancient Ruins\"}").c_str(), ai_parse_title("Title: \"Stone Giants\"\n").c_str(),
                        ai_parse_title("<think>hmm</think>\n  Rivers of Iron  ").c_str());
                fprintf(f, "title_prompt: %s\n", ai_title_retry_prompt("Echoes of the Ancients", ai_forbidden_found("Echoes of the Ancients", r.avoid), { "echo", "night" }).c_str());
                // 1.0.37 : instrumental forcé sans voix
                fprintf(f, "force_inst: %s\n", ai_force_instrumental_style("hard rock, male vocals, Raspy Voice, choir, electric guitar, trap, tenor sax, rap, 120 BPM").c_str());
                fprintf(f, "force_inst (déjà bon): %s\n", ai_force_instrumental_style("instrumental, no vocals, ambient").c_str());
                fprintf(f, "inst_negative: %s\n", ai_instrumental_negative("autotune, Vocals").c_str());
                {
                    int         removed = 0;
                    std::string lyr     = ai_clean_lyrics("[Intro]\n[Verse]\nI walk alone tonight\n[Chorus]\nOh oh oh\n[Outro]\n", true, removed);
                    fprintf(f, "inst_lyrics (retirés=%d): %s\n", removed, lyr.c_str());
                }
                fprintf(f, "strip_negative: %s\n", ai_strip_negative("hard rock, Vocoder, electric guitar, autotune, male vocals", "vocoder, AUTOTUNE").c_str());
                show("frequent", ai_frequent_words({ "La route\nLa route brûle sous nos pas, la poussière vole", "Poussière d'acier\nLa route encore, toujours la route",
                                                     "Moteur\nPoussière et route, moteur hurlant", "Autre\nRien à voir ici" }, 3, 20));
                AiRequest q;
                q.language    = "French";
                q.influence   = "AC/DC";
                q.avoid       = { "nuit", "cœur" };
                q.used_titles = { "Route 66", "Poussière d'acier" };
                q.negative    = "vocoder, autotune";
                q.album_context = plan.ok ? ai_track_context(plan, 0) : "";
                fprintf(f, "--- prompt ---\n%s\n", ai_user_prompt(q).c_str());
                fprintf(f, "--- album prompt ---\n%s\n", ai_album_user_prompt(r).c_str());
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--analyze") {
            if (paths.transcriber.empty()) paths.transcriber = models_dir / "musique" / "SheetSage2-Q8_0.gguf";
            AudioAnalysis a;
            std::atomic<bool> cancel{ false };
            bool ok = analyze_audio(argv[2], models_dir / "analyse", app_temp_dir() / L"analyse_test", paths.transcribe,
                                    paths.transcriber, paths.whisper, models_dir / "voix" / whisper_file().name, [](const std::string &, float) {}, cancel, a);
            FILE * f = _wfopen(argv[3], L"wb");
            if (f) {
                auto line = [&](const char * fmt, auto... args) { fprintf(f, fmt, args...); fputc(10, f); };
                line("ok=%d error=%s duration=%.1f", (int) ok, a.error.c_str(), a.duration);
                line("voice=%.2f male=%.2f bright=%.2f tonal=%.2f danceable=%.2f", a.voice, a.male, a.bright, a.tonal, a.danceable);
                line("bpm=%.1f key=%s meter=%s notes=%d low=%d high=%d med=%d", a.bpm, a.key.c_str(), a.meter.c_str(), (int) a.has_vocal_notes,
                     a.vocal_low, a.vocal_high, a.vocal_median);
                for (size_t i = 0; i < a.styles.size() && i < 8; i++) line("style %s %.3f", a.styles[i].name.c_str(), a.styles[i].score);
                for (size_t i = 0; i < a.moods.size() && i < 6; i++) line("mood %s %.3f", a.moods[i].name.c_str(), a.moods[i].score);
                for (size_t i = 0; i < a.instruments.size() && i < 8; i++) line("inst %s %.3f", a.instruments[i].name.c_str(), a.instruments[i].score);
                for (auto & m : a.mood_flags) line("flag %s %.3f", m.first.c_str(), m.second);
                for (auto & sct : a.sections) line("section %s bars=%d sec=%.1f", sct.label.c_str(), sct.bars, sct.seconds);
                for (auto & c : a.chords) line("chord %s", c.c_str());
                for (auto & sg : build_suggestions(a)) line("SUG [%s] %s sel=%d (%s)", sg.group.c_str(), sg.tag.c_str(), (int) sg.selected, sg.why.c_str());
                std::string st;
                for (auto & t : suggested_structure(a, false)) st += "[" + t + "] ";
                line("structure %s", st.c_str());
                line("lyrics_lang=%s err=%s lines=%d", a.lyrics_lang.c_str(), a.lyrics_error.c_str(), (int) a.lyric_lines.size());
                line("%s", suggested_lyrics(a).c_str());
                fclose(f);
            }
            return 0;
        }
    }
    // Langues : dossier « langues » ; celle des réglages, sinon celle du système.
    i18n::init(app_resource_dir(L"langues"));  // ressources\langues (1.0.30), sinon langues
    {
        std::string code;
        migrate_settings(paths.data, paths.config);  // 1.0.41 : avant toute lecture des réglages
        std::string text = read_text(paths.config / "reglages.json");
        if (yyjson_doc * doc = yyjson_read(text.data(), text.size(), 0)) {
            yyjson_val * v = yyjson_obj_get(yyjson_doc_get_root(doc), "ui_language");
            if (yyjson_is_str(v)) code = yyjson_get_str(v);
            yyjson_doc_free(doc);
        }
        if (code.empty()) code = i18n::system_language();
        if (const wchar_t * lang = _wgetenv(L"STUDIO_TEST_LANG")) code = narrow(lang);  // essai : captures de l'aide
        if (!i18n::set_language(code)) i18n::set_language("fr");
    }
    // Premier lancement : télécharger le modèle ou indiquer le dossier qui le contient déjà.
    if (!ensure_models(instance, fs::path(exe).parent_path(), paths)) return 0;
    journal::init(paths.root / "journal", STUDIO_VERSION);  // 1.1.011

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR                    gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, instance, LoadIconW(instance, MAKEINTRESOURCEW(1)),
                    LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"StudioYuE2", nullptr };
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    g_icon = wc.hIcon;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, APP_NAME_W L" " STUDIO_VERSION_W, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              (int) (1360 * g_dpi), (int) (900 * g_dpi), nullptr, nullptr, instance, nullptr);
    if (!create_device(hwnd)) {
        cleanup_device();
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }
    bool light     = windows_light_theme();
    g_system_light = light;
    BOOL dark  = !light;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    DragAcceptFiles(hwnd, TRUE);  // packs .yue2pack glissés sur la fenêtre

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    journal::imgui_install();
    ImGuiIO & io    = ImGui::GetIO();
    io.IniFilename  = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_theme(light, g_dpi);

    build_fonts(false);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    App app;
    app.hwnd          = hwnd;
    app.draft_path    = paths.cache / "brouillon.json";   // 1.0.41 : brouillon dans cache, réglages dans config
    app.settings_path = paths.config / "reglages.json";
    load_draft(app.form, app.draft_path);
    load_settings(app.settings, app.settings_path);
    journal::event("appli", "Réglages chargés : disposition " + std::to_string(app.settings.layout) + ", thème " + std::to_string(app.settings.theme) +
                                ", langue « " + app.settings.ui_language + " »");
    app.load_last_album();  // 1.0.36 : dernier album de « Composer avec l'IA »
    g_theme = app.settings.theme;  // thème choisi (le thème par défaut suit Windows)
    g_collapsed.insert(app.settings.collapsed.begin(), app.settings.collapsed.end());
    apply_theme(light, g_dpi);
    {
        BOOL dark_title = theme_is_dark(g_theme, light);
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_title, sizeof(dark_title));
    }
    app.driver_version = gpu_driver_version();
    if (!app.driver_version.empty()) {
        if (app.settings.gpu_driver.empty()) {
            app.settings.gpu_driver = app.driver_version;  // première utilisation : version de référence
            save_settings(app.settings, app.settings_path);
        } else if (app.settings.gpu_driver != app.driver_version) {
            app.driver_changed = true;
        }
    }
    app.form.language = app.settings.language;
    {   // recherche d'un pilote AMD plus récent, en tâche de fond
        std::string cn = amd_adrenalin_version();
        if (!cn.empty()) std::thread(check_driver_update, app.driver_update, cn).detach();
    }
    app.player.set_volume(app.settings.volume);
    // Profil de calcul d'après les réglages ; le moteur CUDA (cartes Nvidia) se choisit ici, avant de créer la file.
    gpu_profile().compat  = compat_from_string(app.settings.gpu_compat);
    gpu_profile().backend = backend_from_string(app.settings.gpu_backend);
    set_gpu_limit(app.settings.gpu_limit);
    if (gpu_profile().want_cuda()) gpu_profile().cuda_active = paths.use_cuda_engine();
    Studio studio(paths, [hwnd] { PostMessageW(hwnd, WM_APP_NOTIFY, 0, 0); });
    g_studio   = &studio;
    app.studio = &studio;
    app.init_models();
    app.sysinfo = read_system_info(paths.models_dir);  // mémoire vive, processeur, disque des modèles (recommandations)
    app.init_solver();
    app.refresh_album_dirs(studio.snapshot());
    app.migrate_covers();            // 1.0.43 : pochettes rangées dans le dossier de leur album
    if (const wchar_t * g = _wgetenv(L"STUDIO_TEST_GAUGE")) app.settings.gauge_style = _wtoi(g);  // essai : style des compteurs, non enregistré
    set_gpu_limit(app.settings.gpu_limit);
    // Essai (1.0.36) : « --cover-test "Groupe" "Album" "description" sortie.txt » crée la pochette 1:1 de l'album avec le
    // moteur local (models\images de ce programme) et la range comme le ferait la fenêtre Albums, sans interface.
    {
        int        argc = 0;
        wchar_t ** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Essai (1.0.41) : « --config-test sortie.txt » écrit ce qui a été relu de config\ (réglages, styles…).
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--config-test") {
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                const Settings & s = app.settings;
                fprintf(f, "config=%s\ncache=%s\nstyles=%d structures=%d packs=%d\n"
                           "mots_a_eviter=%d derniers_morceaux=%d cles_api=%d pochettes=%d descriptions=%d\nmorceaux=%d theme=%d modele=%s\n",
                        narrow(paths.config.wstring()).c_str(), narrow(paths.cache.wstring()).c_str(), (int) s.presets.size(), (int) s.structures.size(), (int) s.packs.size(),
                        (int) s.ai_avoid.size(), (int) s.ai_recent.size(), (int) s.ai_keys.size(), (int) s.covers.size(),
                        (int) s.cover_prompts.size(), (int) studio.snapshot().size(), s.theme, s.model.c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.43) : « --audios-test sortie.txt [id groupe album piste] » modifie éventuellement un morceau, puis
        // écrit le dossier de chaque morceau et les pochettes ; « --rename-test clé groupe album sortie.txt » renomme un album.
        if (argv && argc >= 3 && (std::wstring(argv[1]) == L"--audios-test" || std::wstring(argv[1]) == L"--rename-test")) {
            bool        rename = std::wstring(argv[1]) == L"--rename-test";
            const wchar_t * out = rename ? (argc >= 6 ? argv[5] : nullptr) : argv[2];
            if (rename && argc >= 6) app.rename_album(narrow(argv[2]), narrow(argv[3]), narrow(argv[4]));
            if (!rename && argc >= 7) {
                for (const Song & s : studio.snapshot())
                    if (s.id == narrow(argv[3])) studio.edit_metadata(s.id, s.title, narrow(argv[4]), narrow(argv[5]), narrow(argv[6]));
            }
            Sleep(1500);  // étiquettes réécrites en arrière-plan
            studio.rescan();
            app.refresh_album_dirs(studio.snapshot());
            if (FILE * f = out ? _wfopen(out, L"wb") : nullptr) {
                for (const Song & s : studio.snapshot())
                    fprintf(f, "%s | %s | %s | %s | %s\n", s.id.c_str(), s.artist.c_str(), s.album.c_str(), s.title.c_str(),
                            narrow(song_folder(s.id).wstring()).c_str());
                for (const auto & [k, v] : app.settings.covers)
                {
                    std::wstring stem;
                    fs::path     dir = app.cover_dir(k, stem);
                    std::string found = app.cover_for(k);
                    fprintf(f, "pochette %s = %s -> %s  [dossier %s]\n", k.c_str(), v.c_str(), found.c_str(), narrow(dir.wstring()).c_str());
                }
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.43) : « --gen-test "Groupe" "Album" "Titre" sortie.txt » génère un instrumental de 20 s par la file du
        // Studio (comme le bouton Créer) et écrit où il a été rangé.
        // Essai (1.0.46) : « --delete-album-test "Groupe" "Album" sortie.txt » met l'album à la corbeille comme « Albums… ».
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--delete-album-test") {
            app.delete_album_artist = narrow(argv[2]);
            app.delete_album_name   = narrow(argv[3]);
            app.delete_album_key    = album_key(app.delete_album_artist, app.delete_album_name);
            fs::path dir            = album_folder(app.delete_album_artist, app.delete_album_name);
            bool     before         = fs::exists(dir);
            app.delete_album();
            studio.rescan();
            if (FILE * f = _wfopen(argv[4], L"wb")) {
                fprintf(f, "dossier=%s avant=%d apres=%d morceaux_restants=%d\n", narrow(dir.wstring()).c_str(), (int) before,
                        (int) fs::exists(dir), (int) studio.snapshot().size());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.47) : « --band-test "Groupe" "Album" sortie.txt » écrit ce que l'IA recevrait (lecture seule).
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--band-test") {
            AiAlbumRequest r;
            r.band  = narrow(argv[2]);
            r.album = narrow(argv[3]);
            app.add_band_history(r);
            if (FILE * f = _wfopen(argv[4], L"wb")) {
                fprintf(f, "complete=%d derniere_piste=%d titres=%d albums=%d\nson=%s\n", (int) r.complete, app.band_album_last_track(r.band, r.album),
                        (int) r.used_titles.size(), (int) r.used_albums.size(), r.sound.c_str());
                for (const std::string & a : r.used_albums) fprintf(f, "album: %s\n", a.c_str());
                fprintf(f, "--- consigne\n%s\n", ai_album_user_prompt(r).c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.48) : « --split-test paroles.txt sortie.txt » écrit les deux parties d'un morceau long.
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--split-test") {
            auto parts = split_long_lyrics(read_text(argv[2]));
            if (FILE * f = _wfopen(argv[3], L"wb")) {
                fprintf(f, "=== partie 1\n%s\n=== partie 2\n%s\n", parts.first.c_str(), parts.second.c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.64) : « --story-test sortie.txt » : réponses de l'IA mal fermées, lues ou refusées comme prévu.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--story-test") {
            const std::string one = "{\"first\": 0, \"last\": 0, \"prompt\": \"a\"}";
            const struct { const char * name; std::string text; size_t want; } cases[] = {
                { "correct", "{\"scenes\": [" + one + "," + one + "]}", 2 },
                { "accolade oubliee", "{\"scenes\": [" + one + "," + one + "]\n", 2 },
                { "coupee dans un texte", "{\"scenes\": [" + one + ",{\"first\": 0, \"last\": 0, \"prompt\": \"b", 2 },
                { "virgule finale", "{\"scenes\": [" + one + ",]}", 1 },
                { "texte autour", "Voici :\n{\"scenes\": [" + one + "]} Bonne chance {", 1 },
                { "aucune scene", "je ne sais pas", 0 },
            };
            int bad = 0;
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                for (const auto & c : cases) {
                    std::vector<SceneSpec> got;
                    clip_chunk_parse(c.text, -1, -1, 10, got);
                    bad += got.size() != c.want;
                    fprintf(f, "%s : %d scene(s), attendu %d\n", c.name, (int) got.size(), (int) c.want);
                }
                fprintf(f, "%s\n", bad ? "ECHEC" : "OK");
                fclose(f);
            }
            return bad;
        }
        // Essai (1.1.018) : « --natural-test sortie.txt » : paroles françaises par l'IA locale, sans puis avec les consignes d'écriture naturelle.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--natural-test") {
            app.local_llm = find_local_llm(app.app_dir());
            AiRequest req    = app.ai_request();
            req.language     = "French";
            req.language_tag = "French";
            req.instrumental = false;
            req.length       = 0;
            req.influence.clear();
            req.theme = "une gare de nuit en hiver";
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<bool> stop{ false };
            std::atomic<int>  stage{ 0 };
            FILE *            f = _wfopen(argv[2], L"wb");
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                for (int natural = 0; natural < 4; natural++) {  // sans, avec, sans, avec (2 tirages chacun)
                    ai_set_natural((natural & 1) != 0);
                    AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(req));
                    AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                    if (f) fprintf(f, "=== naturel=%d ok=%d titre=%s\n%s\n\n", natural & 1, (int) a.ok, s.title.c_str(), s.lyrics.empty() ? a.text.c_str() : s.lyrics.c_str());
                }
            });
            if (f) fclose(f);
            return 0;
        }
        // Essai (1.0.70) : « --lengthen-test sortie.txt » : suite des paroles, d'abord avec une fausse IA, puis avec l'IA locale.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--lengthen-test") {
            AiSong song;
            song.ok = true, song.title = "Essai", song.style = "rock";
            song.lyrics = "[Verse]\n";
            for (int i = 0; i < 30; i++) song.lyrics += "ligne " + std::to_string(i) + "\n";
            song.lyrics += "\n[Outro]";
            std::atomic<bool> stop{ false };
            ai_lengthen_song(song, 80, {}, [](const std::string &, const std::string &) {
                AiCallResult r;
                r.ok   = true;
                r.text = "[Verse]\n";
                static int call = 0;
                for (int i = 0; i < 30; i++) r.text += "suite " + std::to_string(call * 20 + i) + "\n";  // 1.0.73 : 10 lignes recopiées par réponse
                r.text += "[Chorus]\nrefrain un\nrefrain deux\n[Outro]";
                call++;
                return r;
            }, &stop);
            size_t outros = 0;
            for (size_t p = song.lyrics.find("[Outro]"); p != std::string::npos; p = song.lyrics.find("[Outro]", p + 1)) outros++;
            std::set<std::string> seen;
            int                   copies = 0;
            for (const std::string & l : ai_song_lines(song.lyrics)) copies += l.rfind("refrain", 0) != 0 && !seen.insert(l).second;
            std::string           dup = "[Verse]\nUne Ligne.\nune ligne\nautre\n\n[Bridge]\nautre\n\n[Chorus]\nautre\nautre";
            std::set<std::string> k0;
            const int             dropped = ai_drop_repeated_lines(dup, k0);  // 2 retirées : ligne en double et pont vidé
            std::string ch = "[Chorus]\nA\nB\nC\n\n[Chorus]\nA\nB\nC\nD";
            std::set<std::string> k1;
            const int ch_drop = ai_drop_repeated_lines(ch, k1, 2);  // 1 retirée : C repris
            const bool ok = ch_drop == 1 && ai_sung_lines(ch) == 6 && ai_sung_lines(song.lyrics) >= 80 && outros == 1 && song.lyrics.rfind("[Outro]") + 7 == song.lyrics.size() && copies == 0 &&
                            dropped == 2 && dup.find("[Bridge]") == std::string::npos && ai_sung_lines(dup) == 4;
            FILE * f = _wfopen(argv[2], L"wb");
            if (f) fprintf(f, "fausse IA : %d lignes, %d [Outro], %d lignes recopiees, tri %d : %s\n", ai_sung_lines(song.lyrics), (int) outros, copies, dropped, ok ? "OK" : "ECHEC");
            app.local_llm = find_local_llm(app.app_dir());
            AiRequest req = app.ai_request();
            req.length       = 3;
            req.instrumental = false;
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<int> stage{ 0 };
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(req));
                AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                const int    first = ai_sung_lines(s.lyrics);
                ai_lengthen_song(s, AI_LONG_LINES, req.avoid, [&](const std::string & sy, const std::string & u) {
                    AiCallResult r = ask(sy, u);
                    if (f) fprintf(f, "--- suite demandee : ok=%d, %d lignes lues\n%s\n", (int) r.ok, ai_sung_lines(ai_parse_song(r.text, false).lyrics), r.text.substr(0, 900).c_str());
                    return r;
                }, &stop);
                if (f) fprintf(f, "IA locale : %d lignes au depart, %d a la fin, estimation %.0f s\n%s\n", first, ai_sung_lines(s.lyrics), estimate_seconds(s.lyrics, false), s.lyrics.c_str());
            });
            if (f) fclose(f);
            return ok ? 0 : 1;
        }
        // Essai (1.0.73) : « --album-lines-test sortie.txt [n] » : plan et paroles d'un album de n chansons (4 par défaut) par
        // l'IA locale, comme le fil de l'album, sans musique ; compte les lignes de couplet reprises d'un morceau précédent.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--album-lines-test") {
            app.local_llm      = find_local_llm(app.app_dir());
            AiAlbumRequest req = app.ai_album_request();
            req.count          = argc >= 4 ? std::clamp(_wtoi(argv[3]), 2, 10) : 4;
            req.kind           = 0;
            req.band.clear(), req.album.clear(), req.used_titles.clear(), req.used_albums.clear(), req.complete = false;
            AiRequest base = app.ai_request();
            base.title.clear(), base.instrumental = false, base.length = 1;
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<int>  stage{ 0 };
            std::atomic<bool> stop{ false };
            FILE *            f        = _wfopen(argv[2], L"wb");
            int               residual = 0;
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                AiCallResult res  = ask(ai_album_system_prompt(), ai_album_user_prompt(req));
                AiAlbumPlan  plan = res.ok ? ai_parse_album_plan(res.text, req) : AiAlbumPlan{};
                if (f) fprintf(f, "plan : %s, %s / %s, %d pistes\n", plan.ok ? "OK" : "illisible", plan.band.c_str(), plan.album.c_str(), (int) plan.tracks.size());
                std::vector<std::string> titles, album_lines;
                for (int i = 0; plan.ok && i < (int) plan.tracks.size(); i++) {
                    AiRequest r     = base;
                    r.title         = plan.tracks[i].title;
                    if (!plan.tracks[i].theme.empty()) r.theme = plan.tracks[i].theme;
                    r.used_titles   = titles;
                    r.used_lines    = album_lines;
                    r.album_context = ai_track_context(plan, i);
                    AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(r));
                    AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                    if (!s.ok) {
                        if (f) fprintf(f, "piste %d illisible\n", i + 1);
                        continue;
                    }
                    // même tri que le fil de l'album ; « avant » = lignes de couplet déjà chantées dans l'album
                    std::set<std::string> before = ai_known_lines(album_lines), probe = before;
                    std::string           copy   = s.lyrics;
                    const int             all    = ai_drop_repeated_lines(copy, probe);
                    std::set<std::string> self;
                    std::string           alone = s.lyrics;
                    const int             own   = ai_drop_repeated_lines(alone, self);  // recopiées dans le morceau lui-même
                    std::set<std::string> known = ai_known_lines(album_lines);
                    ai_drop_repeated_lines(s.lyrics, known);
                    std::set<std::string> again = ai_known_lines(album_lines);
                    std::string           check = s.lyrics;
                    residual += ai_drop_repeated_lines(check, again);
                    if (f) fprintf(f, "\n=== piste %d : %s : %d lignes, reprises de l'album %d, recopiees dans le morceau %d, apres tri %d lignes\n%s\n", i + 1,
                                   r.title.c_str(), ai_sung_lines(copy) + all, all - own, own, ai_sung_lines(s.lyrics), s.lyrics.c_str());
                    titles.push_back(r.title);
                    for (const std::string & l : ai_song_lines(s.lyrics))
                        if (std::find(album_lines.begin(), album_lines.end(), l) == album_lines.end()) album_lines.push_back(l);
                }
            });
            if (f) fprintf(f, "\nreste apres tri : %d : %s\n", residual, residual == 0 ? "OK" : "ECHEC"), fclose(f);
            return residual == 0 ? 0 : 1;
        }
        // Essai (1.0.48) : « --long-test sortie.txt » : instrumental court en 3 parties (40 s puis les suites), assemblées.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--long-test") {
            Song s;
            s.title             = std::string("Essai long") + tr(" (partie 1)");
            s.artist            = "Groupe Essai";
            s.style             = "instrumental, synthwave, analog synth, driving bass, 100 BPM";
            s.lyrics            = "[intro]\n[verse]";
            s.long_rest         = std::string("[chorus]") + LONG_PART_MARK + "[verse]\n[outro]";
            s.instrumental      = true;
            s.instrumental_lora = false;
            s.cot               = "off";
            s.duration          = 40;
            s.kind              = "new";
            if (argc >= 4) {  // 1.0.70 : vraie chanson en 3 parties : « --long-test sortie.txt paroles.txt »
                auto a = split_long_lyrics(read_text(argv[3]), 3);
                auto b = split_long_lyrics(a.second);
                s.lyrics           = a.first;
                s.long_rest        = b.first + LONG_PART_MARK + b.second;
                s.style            = "English, alternative rock, male vocals, driving drums, electric guitar, 125 BPM";
                s.instrumental     = false;
                s.cot              = "off";
                s.auto_duration    = true;
                s.duration         = 360;
                s.expected_seconds = estimate_seconds(s.lyrics, false);
            }
            if (const char * c = getenv("STUDIO_TEST_COT")) s.cot = c;  // 1.0.90 : parties enchaînées avec partition
            std::string id1 = studio.submit(s, false), id2;
            auto        t0  = std::chrono::steady_clock::now();
            Song        last;
            for (;;) {
                Sleep(1000);
                for (const Song & x : studio.snapshot()) {
                    if (x.kind == "extend" && x.artist == s.artist && x.long_rest.empty() && x.created >= s.created) id2 = x.id;
                    if (!id2.empty() && x.id == id2) last = x;
                }
                if (last.status == "done" || last.status == "error" || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2400)) break;
            }
            Sleep(500);
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                double p1 = 0;
                for (const Song & x : studio.snapshot()) if (x.id == id1) p1 = x.audio_seconds;
                fprintf(f, "partie1=%.1f s  assemble=%.1f s  statut=%s %s  secondes=%.0f\ndossier=%s\n", p1, last.audio_seconds, last.status.c_str(),
                        last.message.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                        narrow(song_folder(id2).wstring()).c_str());
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        // Essai (1.1.007) : « --dup-test sortie.txt » : recherche de doublons sur toute la bibliothèque (empreintes calculées si
        // besoin), temps et rapport recopiés ; STUDIO_TEST_DUP_ALL = tous les couples avec leurs scores (réglage des seuils).
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--dup-test") {
            auto t0    = std::chrono::steady_clock::now();
            auto job   = app.new_job("doublons");
            auto found = std::make_shared<std::vector<DupPair>>();
            const fs::path rep = app_temp_dir() / L"doublons_test.txt";
            start_duplicates(job, studio.snapshot(), found, rep, app.analysis_dir());
            while (job->state == 0) Sleep(300);
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                fprintf(f, "etat=%d secondes=%.1f couples=%d\n%s\n", (int) job->state, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                        (int) found->size(), read_text(rep).c_str());
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        // Essai (1.1.006) : « --redo-test "Titre" <n> sortie.txt » : refait le morceau à partir de sa section n (1 = la 2e section
        // chantée) ; cale d'abord les paroles (paroles.lrc) s'il le faut ; compte rendu : sections, paroles données, résultat.
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--redo-test") {
            Song found;
            for (const Song & x : studio.snapshot())
                if (x.status == "done" && x.title == narrow(argv[2])) found = x;
            FILE * f = _wfopen(argv[4], L"wb");
            if (!f) return 1;
            if (found.id.empty()) { fprintf(f, "morceau introuvable\n"); fclose(f); return 1; }
            if (App::redo_sections(found).empty() && !found.instrumental) {
                auto job = std::make_shared<Job>();
                start_lrc(job, { found }, studio.paths().whisper, studio.paths().models_dir / "voix" / whisper_file().name);
                while (job->state == 0) Sleep(500);
            }
            const auto secs = App::redo_sections(found);
            for (size_t i = 0; i < secs.size(); i++) fprintf(f, "section %d : %s  %.1f s  (ligne %d)\n", (int) i, secs[i].name.c_str(), secs[i].start, secs[i].line);
            const int n = _wtoi(argv[3]);
            if (n < 1 || n >= (int) secs.size()) { fprintf(f, "section %d absente\n", n); fclose(f); return 1; }
            const double      from   = App::redo_cut(secs[n].start);
            // Paroles complètes : le moteur se repère dans les paroles d'après le début gardé (essai du 8/11 : avec les seules sections
            // refaites, il n'a pas chanté le Bridge). STUDIO_TEST_REDO_PART = l'ancien essai, sections à partir de la reprise seulement.
            const std::string lyrics = getenv("STUDIO_TEST_REDO_PART") ? App::redo_lyrics_from(App::redo_lyrics_text(found), secs[n].line) : App::redo_lyrics_text(found);
            fprintf(f, "reprise a %.1f s\n--- paroles donnees ---\n%s\n---\n", from, lyrics.c_str());
            fflush(f);
            const std::string id = studio.submit_redo(found, from, lyrics);
            auto t0 = std::chrono::steady_clock::now();
            Song done;
            for (;;) {
                Sleep(1000);
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status == "done" || done.status == "error" || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(900)) break;
            }
            Sleep(500);
            fprintf(f, "statut=%s message=%s secondes=%.0f duree_audio=%.1f (origine %.1f)\ndossier=%s\n", done.status.c_str(), done.message.c_str(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), done.audio_seconds, found.audio_seconds,
                    narrow(song_folder(id).wstring()).c_str());
            fclose(f);
            studio.shutdown();
            return 0;
        }
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--eta-test") {  // 1.1.015
            FILE * f = _wfopen(argv[2], L"wb");
            double sum_err = 0, sum_abs = 0, sum_real = 0;
            int    n = 0;
            if (f) fprintf(f, "titre ; reel_s ; estime_s ; ecart_%%\n");
            for (const Song & x : studio.snapshot()) {
                if (x.status != "done" || x.started <= 0 || x.finished <= x.started || x.kind == "extend" || x.kind == "rerender") continue;
                const double real = x.finished - x.started, est = studio.eta_for(x);
                if (est <= 0 || real > 3600) continue;
                sum_err += (est - real) / real;
                sum_abs += std::fabs(est - real) / real;
                sum_real += real;
                n++;
                if (f && n <= 40) fprintf(f, "%s ; %.0f ; %.0f ; %+.0f\n", x.title.c_str(), real, est, 100.0 * (est - real) / real);
            }
            if (f) {
                fprintf(f, "morceaux=%d ecart_moyen=%+.1f%% ecart_absolu_moyen=%.1f%% temps_reel_total=%.0fs\n", n, n ? 100 * sum_err / n : 0, n ? 100 * sum_abs / n : 0, sum_real);
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--gen-test") {
            Song s;
            s.title        = narrow(argv[4]);
            s.artist       = narrow(argv[2]);
            s.album        = narrow(argv[3]);
            s.track        = "1";
            s.style        = "instrumental, synthwave, analog synth, driving bass, 100 BPM";
            s.lyrics       = "[intro]\n[interlude]\n[outro]";
            s.instrumental = true;
            s.instrumental_lora = false;
            s.cot          = "off";
            if (const char * c = getenv("STUDIO_TEST_COT")) {  // 1.0.87 : chanson avec partition (full / melody)
                s.cot          = c;
                s.style        = "English, indie rock, electric guitar, male vocal, 100 BPM";
                s.lyrics       = "[Verse]\nWalking down the empty road tonight\nCity lights are fading out of sight\n\n[Chorus]\nHold on, hold on, the morning comes\nHold on, hold on, we are the ones";
                s.instrumental = false;
            }
            s.duration     = getenv("STUDIO_TEST_COT") ? 40 : 20;
            s.kind         = "new";
            std::string id = studio.submit(s, false);
            auto        t0 = std::chrono::steady_clock::now();
            Song        done;
            for (;;) {
                Sleep(1000);
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status == "done" || done.status == "error" ||
                    std::chrono::steady_clock::now() - t0 > std::chrono::seconds(600)) break;
            }
            Sleep(500);
            if (FILE * f = _wfopen(argv[5], L"wb")) {
                fprintf(f, "statut=%s message=%s secondes=%.0f duree_audio=%.1f\ndossier=%s\n", done.status.c_str(), done.message.c_str(),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), done.audio_seconds,
                        narrow(song_folder(id).wstring()).c_str());
                std::error_code ec;
                for (const auto & e : fs::directory_iterator(song_folder(id), ec))
                    fprintf(f, "  %s %llu\n", narrow(e.path().filename().wstring()).c_str(), (unsigned long long) (e.is_regular_file() ? e.file_size() : 0));
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--abc-song") {  // 1.0.93 : chanson générée avec une partition importée
            // « --abc-song partition.abc paroles.txt "style" sortie.txt » : le moteur suit la partition donnée (comme « Générer
            // avec cette partition ») ; compte rendu : statut, durée, dossier.
            Song s;
            s.abc          = read_text(argv[2]);
            s.lyrics       = read_text(argv[3]);
            s.style        = narrow(argv[4]);
            s.title        = "Essai partition importee";
            s.artist       = "Groupe Essai";
            s.cot          = "full";
            s.auto_duration = true;
            s.duration     = 360;
            s.instrumental_lora = false;
            auto        t0 = std::chrono::steady_clock::now();
            std::string id = studio.submit(s, false);
            Song        done;
            for (;;) {
                Sleep(2000);
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status == "done" || done.status == "error" || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(900)) break;
            }
            Sleep(500);
            if (FILE * out = _wfopen(argv[5], L"wb")) {
                fprintf(out, "statut=%s message=%s duree=%.1f secondes=%.0f\ndossier=%s\n", done.status.c_str(), done.message.c_str(), done.audio_seconds,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), narrow(song_folder(id).wstring()).c_str());
                fclose(out);
            }
            studio.shutdown();
            return 0;
        }
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--plan-song") {  // 1.0.88 : partition d'abord, d'après un morceau
            Song model;
            for (const Song & x : studio.snapshot())
                if (x.status == "done" && x.title == narrow(argv[2])) model = x;
            Song s;
            s.title        = model.title + " (partition)";
            s.artist       = model.artist;
            s.album        = model.album;
            s.track        = model.track;
            s.style        = model.style;
            s.lyrics       = model.lyrics;
            s.instrumental = model.instrumental;
            s.instrumental_lora = model.instrumental_lora;
            s.adapters     = model.adapters;
            s.negative_style = model.negative_style;
            s.negative_scale = model.negative_scale;
            s.model        = model.model;
            s.solver       = model.solver;
            s.cot          = "full";
            s.auto_duration = true;
            s.duration     = 360;
            s.expected_seconds = model.audio_seconds;
            std::string error;
            auto        t0  = std::chrono::steady_clock::now();
            std::string abc = model.id.empty() ? std::string() : studio.plan_score(s, error);
            const double plan_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            s.abc = abc;
            std::string id = abc.empty() ? std::string() : studio.submit(s, false);
            Song done;
            while (!id.empty()) {
                Sleep(2000);
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status == "done" || done.status == "error" || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(1500)) break;
            }
            Sleep(500);
            ScoreInfo a = parse_score(abc);
            if (FILE * out = _wfopen(argv[3], L"wb")) {
                fprintf(out, "modele=%s trouve=%d\nplan=%.0f s erreur=%s\npartition : K=%s Q=%d M=%s\nstatut=%s message=%s duree=%.1f total=%.0f s\ndossier=%s\n",
                        narrow(argv[2]).c_str(), (int) !model.id.empty(), plan_s, error.c_str(), a.key.c_str(), a.tempo, a.meter.c_str(),
                        done.status.c_str(), done.message.c_str(), done.audio_seconds,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), id.empty() ? "" : narrow(song_folder(id).wstring()).c_str());
                for (const auto & [sec, ch] : a.sections) fprintf(out, "  %s : %s\n", sec.c_str(), ch.c_str());
                fclose(out);
            }
            studio.shutdown();
            return 0;
        }
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--plan-test") {  // 1.0.88
            Song s;
            s.title        = "Essai partition retouchee";
            s.artist       = "Groupe Essai";
            s.album        = "Album Essai";
            s.track        = "2";
            s.style        = "English, indie rock, electric guitar, male vocal, 100 BPM";
            s.lyrics       = "[Verse]\nWalking down the empty road tonight\nCity lights are fading out of sight\n\n[Chorus]\nHold on, hold on, the morning comes\nHold on, hold on, we are the ones";
            s.cot          = "full";
            s.duration     = 40;
            s.instrumental_lora = false;
            std::string error;
            auto        t0  = std::chrono::steady_clock::now();
            std::string abc = studio.plan_score(s, error);
            const double plan_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ScoreInfo a = parse_score(abc);
            std::string    abc2 = abc_set_tempo(abc_transpose(abc, 2), 120);
            ScoreInfo b = parse_score(abc2);
            s.abc = abc2;
            std::string id = abc.empty() ? std::string() : studio.submit(s, false);
            Song done;
            while (!id.empty()) {
                Sleep(1000);
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status == "done" || done.status == "error" || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(600)) break;
            }
            Sleep(500);
            if (FILE * out = _wfopen(argv[2], L"wb")) {
                fprintf(out, "plan=%.0f s erreur=%s\nprepare : K=%s Q=%d M=%s sections=%d\nretouche : K=%s Q=%d\nstatut=%s message=%s duree=%.1f\ndossier=%s\n",
                        plan_s, error.c_str(), a.key.c_str(), a.tempo, a.meter.c_str(), (int) a.sections.size(), b.key.c_str(), b.tempo,
                        done.status.c_str(), done.message.c_str(), done.audio_seconds, id.empty() ? "" : narrow(song_folder(id).wstring()).c_str());
                for (const auto & [sec, ch] : a.sections) fprintf(out, "  avant %s : %s\n", sec.c_str(), ch.c_str());
                for (const auto & [sec, ch] : b.sections) fprintf(out, "  apres %s : %s\n", sec.c_str(), ch.c_str());
                fclose(out);
            }
            studio.shutdown();
            return 0;
        }
        // Essai (1.0.52) : « --clip-test "Titre" image.png 1080p|tiktok|ig sortie.mp4 » crée le clip animé d'un morceau de la
        // bibliothèque (sans l'interface) et écrit le compte rendu dans sortie.mp4.txt.
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--clip-test") {
            Song found;
            for (const Song & x : studio.snapshot())
                if (x.status == "done" && x.title == narrow(argv[2])) found = x;
            std::wstring fmt = argv[4];  // « all » : les 3 formats, argv[5] = préfixe des sorties (essai du montage en parallèle, 1.0.58)
            ClipTask     t;
            t.song          = found;
            t.image         = argv[3];
            t.output        = argv[5];
            t.width         = fmt == L"tiktok" ? 1080 : fmt == L"ig" ? 1080 : 1920;
            t.height        = fmt == L"tiktok" || fmt == L"short" ? 1920 : fmt == L"ig" ? 1080 : 1080;
            if (fmt == L"short") t.width = 1080, t.short_seconds = _wgetenv(L"STUDIO_TEST_SHORT_SECONDS") ? _wtoi(_wgetenv(L"STUDIO_TEST_SHORT_SECONDS")) : 30;  // 1.0.74 : Short de 30 s sur le refrain
            t.karaoke       = _wgetenv(L"STUDIO_TEST_KARAOKE") != nullptr;  // 1.0.98
            t.end_pos       = _wgetenv(L"STUDIO_TEST_END_POS") ? 1 : 0;      // 1.0.98 : texte de fin en bas
            t.end_box       = _wgetenv(L"STUDIO_TEST_END_BOX") != nullptr;   // 1.0.98 : bandeau sombre
            // 1.0.98 : texte de fin (sinon vide = rien dessiné) : STUDIO_TEST_END_TEXT, ou « Abonnez-vous » si END_POS / END_BOX est défini
            if (const wchar_t * et = _wgetenv(L"STUDIO_TEST_END_TEXT")) t.text_end = narrow(et);
            else if (_wgetenv(L"STUDIO_TEST_END_POS") || t.end_box) t.text_end = "Abonnez-vous";
            t.whisper_exe   = studio.paths().whisper;
            t.whisper_model = studio.paths().models_dir / "voix" / whisper_file().name;
            if (argc >= 7) {  // 6e paramètre : type de rendu (id de clip_styles) = scénario et images par scène
                SceneEngine eng;
                std::error_code ec3;
                eng.llm        = find_local_llm(app.app_dir());  // en ligne de commande, l'interface n'a pas encore cherché le moteur
                eng.llm_model  = app.ai_models_dir() / widen(app.settings.ai_local_model);
                auto size3     = fs::file_size(eng.llm_model, ec3);
                eng.llm_layers = eng.llm.vulkan ? ai_gpu_layers(ec3 ? 0 : (unsigned long long) size3, gpu_profile().gpu.vram) : 0;
                eng.sd_cli      = find_sd_cli(app.app_dir());
                eng.image_root  = app.image_models_dir();
                eng.image_model = &local_image_model(image_model_ready(app.image_models_dir(), local_image_model("klein")) ? "klein" : app.settings.ai_image_local);
                if (const wchar_t * im = _wgetenv(L"STUDIO_TEST_IMAGE_MODEL")) eng.image_model = &local_image_model(narrow(im));  // essai : autre moteur
                eng.offload     = App::image_offload(*eng.image_model);
                if (const wchar_t * n = _wgetenv(L"STUDIO_TEST_CLIP_IMAGES")) eng.scene_count = _wtoi(n);  // essai : nombre d'images voulu
                eng.same_seed   = _wgetenv(L"STUDIO_TEST_SAME_SEED") != nullptr;  // 1.0.98 : graine commune
                for (const ClipStyle & cs : clip_styles())
                    if (narrow(argv[6]) == cs.id) { eng.style_id = cs.id; eng.style_prompt = cs.prompt; }
                t.make_scenes = [eng](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out, std::string & note) {
                    return make_clip_scenes(eng, j, sg, lines, seconds, out, note);
                };
                t.make_scenario = [eng](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out, std::string & note) {
                    return make_clip_scenes(eng, j, sg, lines, seconds, out, note, true);
                };
            }
            auto job = std::make_shared<Job>();
            auto t0  = std::chrono::steady_clock::now();
            // Essai : images existantes (STUDIO_TEST_SCENES_DIR), lues sans toucher au scénario du morceau
            if (const wchar_t * dir = _wgetenv(L"STUDIO_TEST_SCENES_DIR")) {
                fs::path scene_dir = dir;
                t.make_scenes = [scene_dir](Job &, const Song &, const std::vector<ClipLine> &, double seconds, ClipScenes & out, std::string &) {
                    std::error_code ec;
                    for (const auto & e : fs::directory_iterator(scene_dir, ec))
                        if (e.path().filename().wstring().rfind(L"scene_", 0) == 0) out.images.push_back(e.path());
                    std::sort(out.images.begin(), out.images.end());
                    for (size_t k = 0; k < out.images.size(); k++) out.starts.push_back(seconds * k / out.images.size());
                    return !out.images.empty();
                };
            }
            std::vector<ClipTask> test_tasks = { t };
            if (fmt == L"all") {
                ClipTask tt = t, ig = t;
                t.output = std::wstring(argv[5]) + L"_yt.mp4";
                tt.output = std::wstring(argv[5]) + L"_tt.mp4";
                tt.width  = 1080;
                tt.height = 1920;
                ig.output = std::wstring(argv[5]) + L"_ig.mp4";
                ig.width  = ig.height = 1080;
                test_tasks = { t, tt, ig };
            }
            // Essai : un second morceau dans la même création (modèle d'images gardé chargé de l'un à l'autre)
            if (const wchar_t * second = _wgetenv(L"STUDIO_TEST_CLIP_SONG2")) {
                ClipTask t2 = t;
                for (const Song & x : studio.snapshot())
                    if (x.status == "done" && x.title == narrow(second)) t2.song = x;
                t2.output = std::wstring(argv[5]) + L"_2.mp4";
                test_tasks.push_back(t2);
            }
            start_clips(job, studio.paths(), test_tasks);
            std::thread pause_t;
            if (const wchar_t * pz = _wgetenv(L"STUDIO_TEST_PAUSE")) {  // 1.1.017 : « a,b » : pause apres a s, pendant b s
                int a = 0, b = 0;
                swscanf(pz, L"%d,%d", &a, &b);
                pause_t = std::thread([job, a, b] { Sleep(a * 1000); job->set_paused(true); Sleep(b * 1000); job->set_paused(false); });
            }
            while (job->state == 0) Sleep(500);
            if (pause_t.joinable()) pause_t.join();
            std::wstring report = std::wstring(argv[5]) + L".txt";
            if (FILE * f = _wfopen(report.c_str(), L"wb")) {
                std::lock_guard<std::mutex> lock(job->mutex);
                fprintf(f, "trouve=%d etat=%d secondes=%.0f\n%s\n", (int) !found.id.empty(), (int) job->state,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), job->message.c_str());
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        // Essai (1.0.57) : descriptions d'un album par l'IA locale, sans interface
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--desc-test") {
            std::vector<Song> all = studio.snapshot(), songs;
            std::string       key = album_key(narrow(argv[2]), narrow(argv[3])), err;
            for (const Song & s : album_songs(all, key, false))
                if (s.long_rest.empty()) songs.push_back(with_full_lyrics(s, all));
            LocalLlm llm;
            fs::path model;
            int      layers = 0;
            bool     ok     = app.local_text_ready(llm, model, layers);
            std::atomic<bool> cancel{ false };
            std::atomic<int>  written{ 0 }, stage{ 0 };
            AiCallResult      r = ok ? ai_local_session(llm, model, layers, &stage, &cancel, [&](const AiAsk & ask) {
                write_descriptions(songs, i18n::current(), ask, [](const std::string &) {}, cancel, written, err);
            }) : AiCallResult{};
            FILE * f = _wfopen(argv[4], L"wb");
            if (f) {
                fprintf(f, "morceaux=%d ready=%d ok=%d ecrits=%d erreur=%s %s\n", (int) songs.size(), (int) ok, (int) r.ok, written.load(), err.c_str(), r.error.c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.97) : outils audio sur les morceaux terminés, sans interface : --tools-test sortie.txt [dossier d'export]
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--tools-test") {
            std::vector<Song> all = studio.snapshot(), songs;
            for (const Song & s : all)
                if (s.status == "done" && s.long_rest.empty()) songs.push_back(s);
            FILE * f = _wfopen(argv[2], L"wb");
            auto   wait = [&](const std::shared_ptr<Job> & j, const char * what) {
                while (j->state == 0) Sleep(300);
                std::lock_guard<std::mutex> lock(j->mutex);
                if (f) fprintf(f, "%s : etat=%d %s\n", what, (int) j->state, j->message.c_str());
            };
            if (!songs.empty()) {
                std::vector<Song> one{ songs[0] };
                auto j1 = app.new_job("master");
                start_master(j1, one, -14, -1, true);
                wait(j1, "master");
                const fs::path m = song_folder(songs[0].id) / L"master";
                std::error_code ec;
                if (fs::exists(m, ec)) for (auto & e : fs::directory_iterator(m, ec)) if (f) fprintf(f, "  fichier %s %lld octets\n", narrow(e.path().filename().wstring()).c_str(), (long long) fs::file_size(e.path(), ec));
                auto j2 = app.new_job("lrc");
                std::vector<Song> sung; for (const Song & x : songs) if (!x.instrumental) { sung.push_back(x); break; }
                start_lrc(j2, sung, studio.paths().whisper, studio.paths().models_dir / "voix" / whisper_file().name);
                wait(j2, "lrc");
                auto j3 = app.new_job("doublons");
                auto found = std::make_shared<std::vector<DupPair>>();
                start_duplicates(j3, songs, found, app_temp_dir() / L"doublons_test.txt", app.analysis_dir());
                wait(j3, "doublons");
                if (f) fprintf(f, "  couples=%d\n", (int) found->size());
                const Song & s0 = songs[0];
                auto j4 = app.new_job("livret");
                start_booklet(j4, songs, s0.artist, s0.album.empty() ? "Test" : s0.album, fs::path(), app_temp_dir() / L"livret_test");
                wait(j4, "livret");
                auto j5 = app.new_job("export");
                start_daw_export(j5, one, argc >= 4 ? fs::path(argv[3]) : app_temp_dir() / L"export_test");
                wait(j5, "export");
                fs::path dx, dm;
                if (app.stems_engine(dx, dm)) {
                    auto j6 = app.new_job("stems");
                    start_stems(j6, one, dx, dm);
                    wait(j6, "stems");
                }
                std::vector<std::string> problems;
                const int best = best_trial(songs.size() > 3 ? std::vector<Song>(songs.begin(), songs.begin() + 3) : songs, problems);
                if (f) fprintf(f, "meilleur=%d problemes=%s\n", best, problems.empty() ? "" : problems[0].c_str());
            }
            if (f) fclose(f);
            return 0;
        }
        // Essais (1.0.98), sans interface : --help-test "question" sortie | --disk-test sortie | --key-test sortie | --update-test zip sha256 sortie
        // Essai (1.0.100), sans interface : --gpu-test "question" sortie : une question à l'aide (IA locale) prend la carte, puis un
        // morceau de 20 s est mis en file aussitôt : il doit attendre son tour (jeton carte graphique) et ne démarrer qu'après la réponse.
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--gpu-test") {
            const auto t0   = std::chrono::steady_clock::now();
            auto       secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
            app.help_q = narrow(argv[2]);
            app.start_help_question();
            while (gpu_holder().empty() && app.help_job && !app.help_job->finished) Sleep(20);
            const std::string first_holder = gpu_holder();
            Song s;
            s.title = "Essai jeton carte";
            s.artist = "Essai";
            s.album = "Jeton";
            s.track = "1";
            s.style = "instrumental, synthwave, analog synth, driving bass, 100 BPM";
            s.lyrics = "[intro]\n[interlude]\n[outro]";
            s.instrumental = true;
            s.cot = "off";
            s.duration = 20;
            s.kind = "new";
            const std::string id = studio.submit(s, false);
            std::string waited;
            double      help_done = -1, started = -1;
            Song        done;
            while (secs() < 900) {
                Sleep(100);
                if (help_done < 0 && app.help_job && app.help_job->finished) help_done = secs();
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status != "running" && waited.empty() && !done.stage.empty()) waited = done.stage;
                if (done.status == "running" && started < 0) started = secs();
                if (done.status == "done" || done.status == "error") break;
            }
            app.poll_help();
            if (FILE * f = _wfopen(argv[3], L"wb")) {
                fprintf(f, "carte prise d'abord par : %s\nmessage du morceau en attente : %s\naide finie a %.1f s (reponse %d caracteres, erreur=%s)\n"
                           "morceau lance a %.1f s, statut=%s a %.1f s\nordre correct : %s\n",
                        first_holder.c_str(), waited.c_str(), help_done, (int) app.help_answer.size(), app.help_error.c_str(), started,
                        done.status.c_str(), secs(), help_done >= 0 && started >= help_done - 0.5 ? "oui" : "NON");  // 0,5 s : la carte est rendue juste avant que l'aide se dise finie
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--help-test") {
            app.help_q = narrow(argv[2]);
            app.start_help_question();
            while (app.help_job && !app.help_job->finished) Sleep(300);
            app.poll_help();
            FILE * f = _wfopen(argv[3], L"wb");
            if (f) {
                fprintf(f, "rubriques=%d erreur=%s\nhits:", (int) app.help_sections.size(), app.help_error.c_str());
                for (int h : app.help_hits) fprintf(f, " [%s]", app.help_sections[h].title.c_str());
                fprintf(f, "\nreponse:\n%s\n", app.help_answer.c_str());
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--disk-test") {
            app.start_disk_scan();
            while (!app.disk_scan->finished) Sleep(200);
            FILE * f = _wfopen(argv[2], L"wb");
            if (f) {
                for (const auto & it : app.disk_scan->items) fprintf(f, "%s : %s (%d chemin(s), supprimable=%d)\n", it.label.c_str(), App::fmt_bytes(it.bytes).c_str(), (int) it.paths.size(), (int) it.deletable);
                const fs::path probe = studio.paths().cache / L"temp" / L"essai_corbeille.txt";
                std::error_code ec;
                fs::create_directories(probe.parent_path(), ec);
                std::ofstream(probe) << "essai";
                std::string err;
                const bool ok = recycle_to_bin({ probe }, err);
                fprintf(f, "corbeille ok=%d existe_encore=%d erreur=%s\n", (int) ok, (int) fs::exists(probe, ec), err.c_str());
                std::string err2;
                const bool refused = !recycle_to_bin({ fs::path(L"C:\\Windows\\Temp\\hors_programme.txt") }, err2) ? true : false;
                fprintf(f, "hors programme refuse=%d (fichier absent : ignore)\n", (int) refused);
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--stemscore-test") {  // 1.0.97 : partition des stems du premier morceau qui en a
            for (const Song & s : studio.snapshot())
                if (s.status == "done" && app.song_has_stems(s)) {
                    fs::remove(song_folder(s.id) / L"score_stems.abc");
                    app.start_stem_score(s);
                    break;
                }
            while (app.stem_score_job && !app.stem_score_job->finished) Sleep(500);
            FILE * f = _wfopen(argv[2], L"wb");
            if (f) {
                if (app.stem_score_job) fprintf(f, "erreur=%s\nnotes=%s\n%s\n", app.stem_score_job->error.c_str(), app.stem_score_job->notes.c_str(), app.stem_score_job->abc.c_str());
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--sf-test") {  // 1.0.97 : --sf-test banque.sf2 entree.mid sortie.wav
            std::ifstream mf(argv[3], std::ios::binary);
            const std::string midi((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
            std::string err;
            const auto  t0 = std::chrono::steady_clock::now();
            const bool  ok = render_midi_wav(midi, argv[2], argv[4], err);
            std::error_code ec;
            FILE * f = _wfopen((std::wstring(argv[4]) + L".txt").c_str(), L"wb");
            if (f) {
                fprintf(f, "ok=%d erreur=%s secondes=%.1f taille=%lld\n", (int) ok, err.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), ok ? (long long) fs::file_size(argv[4], ec) : 0LL);
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--key-test") {
            std::vector<Song> songs;
            for (const Song & s : studio.snapshot()) if (s.status == "done" && s.long_rest.empty()) songs.push_back(s);
            app.start_key_check(songs, "essai");
            while (app.key_job && !app.key_job->finished) Sleep(500);
            FILE * f = _wfopen(argv[2], L"wb");
            if (f) {
                std::string rep;
                if (app.key_job) { std::lock_guard<std::mutex> lock(app.key_job->mutex); rep = app.key_job->report; }
                std::string det;
                if (app.key_job) { std::lock_guard<std::mutex> lock(app.key_job->mutex); det = app.key_job->details; }
                fprintf(f, "morceaux=%d rapport:\n%s\ndetail:\n%s\n", (int) songs.size(), rep.c_str(), det.c_str());
                for (const Song & s : songs) {
                    std::ifstream sf(song_folder(s.id) / L"score.abc", std::ios::binary);
                    const ScoreInfo w = parse_score(std::string((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>()));
                    fprintf(f, "%s : score %s %d BPM ; style %d BPM\n", s.title.c_str(), w.key.c_str(), w.tempo, App::style_bpm(s.style));
                }
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--update-test") {
            auto info        = std::make_shared<UpdateInfo>();
            info->zip_url    = "file:///" + narrow(fs::absolute(argv[2]).wstring());
            info->zip_name   = narrow(fs::path(argv[2]).filename().wstring());
            info->zip_digest = "sha256:" + narrow(argv[3]);
            std::error_code ec;
            info->zip_size   = fs::file_size(argv[2], ec);
            app.start_update_download(info);
            while (app.update_job->state == 0) Sleep(300);
            FILE * f = _wfopen(argv[4], L"wb");
            std::string err;
            bool        launched = false;
            if (app.update_job->state == 1) launched = app.launch_update_install(app.update_job->extracted, "essai", err);
            if (f) {
                fprintf(f, "etat=%d erreur=%s extrait=%s lance=%d %s\n", (int) app.update_job->state, app.update_job->error.c_str(), app.update_job->extracted.c_str(), (int) launched, err.c_str());
                fclose(f);
            }
            studio.shutdown();
            return 0;  // le script attend la fin de ce processus, installe, puis rouvre le Studio
        }
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--cover-test") {
            std::string key = album_key(narrow(argv[2]), narrow(argv[3]));
            fs::path    png = app_temp_dir() / L"pochettes" / L"cover_test.png";
            std::error_code ec;
            fs::create_directories(png.parent_path(), ec);
            auto         t0 = std::chrono::steady_clock::now();
            // 1.0.50 : 7e paramètre facultatif = modèle local (« klein », « zimage », « moss »), sinon celui des réglages
            const LocalImageModel & m = local_image_model(argc >= 7 ? narrow(argv[6]) : app.settings.ai_image_local);
            AiCallResult r  = ai_image_local(find_sd_cli(app.app_dir()), app.image_models_dir(), m, narrow(argv[4]), 768, 768, png, nullptr,
                                             App::image_offload(m));
            double       s  = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            bool         installed = r.ok && app.install_cover(key, png);
            FILE *       f  = _wfopen(argv[5], L"wb");
            if (f) {
                fprintf(f, "ok=%d secondes=%.1f erreur=%s %s\nclé=%s\npochette=%s\n", (int) r.ok, s, r.error.c_str(), r.error_arg.c_str(), key.c_str(),
                        installed ? app.settings.covers[key].c_str() : "(non rangée)");
                fclose(f);
            }
            fs::remove(png, ec);
            return 0;
        }
    }
    if (const wchar_t * test_cover = _wgetenv(L"STUDIO_TEST_COVER")) {  // essais automatisés : fichier de reprise présélectionné
        app.form.cover         = test_cover;
        app.form.cover_seconds = media_duration(app.form.cover);
    }
    app.start_update_check();  // modèles et LoRA installés : y a-t-il une version plus récente ?
    app.start_app_update_check();  // Studio Audio AI : version plus récente publiée ? (rien si aucune adresse)
    app.startup_checks();          // 1.0.81 : sauvegarde du jour et fichiers manquants
    int       targc = 0;  // essai : compte rendu de la vérification (« --startup-test sortie.txt »)
    wchar_t ** targv = CommandLineToArgvW(GetCommandLineW(), &targc);
    if (targv && targc >= 3 && std::wstring(targv[1]) == L"--startup-test") {
        if (FILE * f = _wfopen(targv[2], L"wb")) {
            fprintf(f, "sauvegarde=%s nombre=%d manquants=%d\n", app.last_backup.c_str(), app.backup_count, (int) app.startup_issues.size());
            for (const std::string & s : app.startup_issues) fprintf(f, "%s\n", s.c_str());
            fclose(f);
        }
        return 0;
    }
    studio.set_fade_seconds(app.settings.fade_seconds);
    // Jauges de mémoire vidéo : la carte de calcul détectée (sur un portable, la carte Nvidia/AMD et non la puce
    // intégrée qui affiche la fenêtre) ; à défaut, celle de la fenêtre.
    if (gpu_profile().gpu.found) app.gpu_monitor.start(gpu_profile().gpu.luid, gpu_profile().gpu.vram);
    else app.gpu_monitor.start(g_adapter_luid, g_adapter_vram);

    // Boucle sobre : on ne redessine que sur événement, ou périodiquement pendant une génération/lecture.
    bool   done        = false;
    int    frames      = 3;
    double last_rescan = 0;
    bool   library_dirty = false, fs_dirty = false;
    const wchar_t * fscache_out  = _wgetenv(L"STUDIO_TEST_FSCACHE");
    int             fscache_step = 0;
    double          fscache_t0   = 0;
    std::string     fscache_log;
    double last_fs_gen   = -10;
    HANDLE library_watch = FindFirstChangeNotificationW(paths.library.c_str(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                                                         FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (library_watch != INVALID_HANDLE_VALUE) {
        app.fs_watch_root = paths.library.native();
        if (!app.fs_watch_root.empty() && app.fs_watch_root.back() != L'\\') app.fs_watch_root += L'\\';
    }
    // Banc de mesure (1.1.013) : « STUDIO_TEST_BENCH=<fichier> » dessine 60 images d'échauffement puis 300 images mesurées
    // (calcul de l'image seulement, sans l'attente de l'écran), mesure aussi rescan() et snapshot(), écrit le fichier et quitte.
    const wchar_t *     bench_out = _wgetenv(L"STUDIO_TEST_BENCH");
    int                 bench_n   = 0;
    std::vector<double> bench_ms;
    while (!done) {
        if (bench_out) frames = 3;
        if (frames <= 0) {
            bool  busy    = app.player.playing() || app.any_active;  // compte à rebours et progression
            DWORD timeout = busy ? 250 : 1000;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout, QS_ALLINPUT);
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

        // Commandes de l'icône de notification
        switch (g_tray_cmd) {
            case TRAY_OPEN:
                ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
                SetForegroundWindow(hwnd);
                break;
            case TRAY_RESUME_ALL: studio.resume_all(); break;
            case TRAY_HOLD_ALL: studio.hold_all(); break;
            case TRAY_QUIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
            case TRAY_READD: tray_remove(); break;  // recréée à l'image suivante
        }
        if (g_tray_cmd != TRAY_NONE) frames = 3;
        g_tray_cmd = TRAY_NONE;

        // 1.1.013 : la bibliothèque n'est relue que si Windows signale un changement sous Audios (au plus toutes les 2 s),
        // et de toute façon toutes les 30 s ; avant : relecture complète toutes les 2 s, même au repos (≈ 17 ms à chaque fois
        // pour 148 morceaux). Sans surveillance possible (dossier réseau…), on garde la relecture toutes les 2 s.
        if (library_watch != INVALID_HANDLE_VALUE && WaitForSingleObject(library_watch, 0) == WAIT_OBJECT_0) {
            library_dirty = fs_dirty = true;
            FindNextChangeNotification(library_watch);
        }
        if (fs_dirty && now_seconds() - last_fs_gen >= 3.0) {  // pendant une génération, le dossier change sans arrêt : 3 s au moins
            app.fs_gen++;
            fs_dirty    = false;
            last_fs_gen = now_seconds();
            frames      = std::max(frames, 2);  // cartes redessinées avec les fichiers apparus ou disparus
        }
        // Essai (1.1.013) : « STUDIO_TEST_FSCACHE=<fichier> » crée puis supprime un fichier sous Audios et mesure en combien de
        // temps cached_exists le voit apparaître puis disparaître (marche aussi fenêtre masquée : pas besoin de capture).
        if (fscache_out) {
            const fs::path probe = paths.library / L"essai_cache_1113.tmp";
            const double   now   = now_seconds();
            const bool     seen  = app.cached_exists(probe);
            if (fscache_step == 0 && fscache_t0 == 0) fscache_t0 = now;
            if (fscache_step == 0 && now - fscache_t0 > 6.0) {  // démarrage passé ; le fichier n'existe pas encore (réponse gardée en cache)
                fscache_log += seen ? "ERREUR : vu avant sa creation\n" : "absent au depart : OK\n";
                { std::ofstream f(probe, std::ios::binary); f << "essai"; }
                fscache_t0   = now;
                fscache_step = 1;
            } else if (fscache_step == 1 && (seen || now - fscache_t0 > 20)) {
                char line[96];
                snprintf(line, sizeof(line), "%s apres creation en %.1f s\n", seen ? "vu" : "ERREUR : jamais vu", now - fscache_t0);
                fscache_log += line;
                std::error_code fec;
                fs::remove(probe, fec);
                fscache_t0   = now;
                fscache_step = 2;
            } else if (fscache_step == 2 && (!seen || now - fscache_t0 > 20)) {
                char line[96];
                snprintf(line, sizeof(line), "%s apres suppression en %.1f s\n", !seen ? "disparu" : "ERREUR : toujours vu", now - fscache_t0);
                fscache_log += line;
                if (FILE * f = _wfopen(fscache_out, L"wb")) {
                    fputs(fscache_log.c_str(), f);
                    fclose(f);
                }
                fscache_out = nullptr;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            frames = std::max(frames, 1);  // la boucle continue de tourner pendant l'essai
        }
        const double since_rescan = now_seconds() - last_rescan;  // horloge réelle : avance aussi fenêtre masquée
        if (since_rescan > 2.0 && (library_dirty || library_watch == INVALID_HANDLE_VALUE || since_rescan > 30.0)) {
            studio.rescan();  // morceaux ajoutés, modifiés ou retirés en dehors du Studio
            last_rescan   = now_seconds();
            library_dirty = false;
        }
        if (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            // Fenêtre masquée : la pastille continue d'être tenue à jour.
            app.update_tray(studio.snapshot());
            app.update_games();
            frames = 0;
            continue;
        }
        g_occluded = false;
        if (g_resize_w != 0 && g_resize_h != 0) {
            cleanup_render_target();
            g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            create_render_target();
        }

        if (!app.pending_language.empty()) {
            app.apply_language();
        }
        const auto bench_t0 = std::chrono::steady_clock::now();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        journal::imgui_frame_end();
        ImGui::Render();
        if (bench_out && ++bench_n > 60) {
            bench_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bench_t0).count());
            if (bench_ms.size() >= 300) {
                std::sort(bench_ms.begin(), bench_ms.end());
                double sum = 0;
                for (double v : bench_ms) sum += v;
                auto timed = [](int n, auto && fn) {
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int i = 0; i < n; i++) fn();
                    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
                };
                const double rescan_ms = timed(10, [&] { studio.rescan(); });
                size_t       count     = 0;
                const double snap_ms   = timed(200, [&] { count = studio.snapshot().size(); });
                if (FILE * f = _wfopen(bench_out, L"wb")) {
                    fprintf(f, "morceaux=%d image_moy_ms=%.3f image_mediane_ms=%.3f image_p95_ms=%.3f image_max_ms=%.3f rescan_ms=%.3f snapshot_ms=%.4f\n",
                            (int) count, sum / bench_ms.size(), bench_ms[bench_ms.size() / 2], bench_ms[bench_ms.size() * 95 / 100], bench_ms.back(),
                            rescan_ms, snap_ms);
                    fclose(f);
                }
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                bench_out = nullptr;
            }
        }
        const float clear[4] = { P.bg.x, P.bg.y, P.bg.z, 1.f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_occluded = g_swapchain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
        frames--;
        // ImGui étale les clics rapides (appui + relâchement) sur plusieurs images : on continue
        // tant que sa file d'événements n'est pas vide, sinon un clic s'appliquerait plus tard, ailleurs.
        if (!ImGui::GetCurrentContext()->InputEventsQueue.empty()) frames = std::max(frames, 2);
    }

    if (library_watch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(library_watch);
    if (app.form.dirty) save_draft(app.form, app.draft_path);
    for (auto & j : app.jobs) j->stop();
    app.jobs.clear();
    app.player.close();
    tray_remove();
    g_studio = nullptr;
    studio.shutdown();  // la génération en cours devient « interrompue », relançable

    release_thumbs();
    journal::event("appli", "Arrêt du Studio.");
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    CoUninitialize();
    return 0;
}
