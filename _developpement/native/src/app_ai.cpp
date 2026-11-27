// Composer avec l'IA : morceau, anti-répétition, styles, Réglages › Styles (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

bool                     App::plan_busy() const { return plan_job && !plan_job->done; }

void App::start_plan() {
    Form &       f      = form;
    std::string  lyrics = trim(f.instrumental ? f.lyrics_inst : f.lyrics_song);
    Song         s;
    s.title             = trim(f.title);
    s.style             = join_style(split_style(f.style));
    s.lyrics            = lyrics;
    s.instrumental      = f.instrumental;
    s.instrumental_lora = false;  // yue-plan ne prend pas de LoRA
    s.cot               = score_mode == 1 ? "melody" : "full";
    s.lm_seed           = f.seed < 0 ? -1 : f.seed;
    s.duration          = f.auto_duration ? 360 : f.duration;
    s.model             = settings.model;
    plan_job            = std::make_unique<PlanJob>();
    plan_job->started   = ImGui::GetTime();
    plan_job->label     = trim(f.title);
    PlanJob * j         = plan_job.get();
    Studio *  st        = studio;
    j->thread           = std::thread([j, st, s]() {
        j->abc  = st->plan_score(s, j->error);
        j->done = true;
    });
}

void App::poll_plan() {
    if (!plan_job || !plan_job->done) return;
    if (plan_job->thread.joinable()) plan_job->thread.join();
    if (!plan_job->abc.empty()) {
        score_abc = score_original = plan_job->abc;
        score_source               = plan_job->label;
        score_tempo_edit           = parse_score(score_abc).tempo;
        notify(plan_job->recognized ? tr("Partition reconnue : choisissez vos paroles et votre style dans « Nouveau morceau », retouchez-la si besoin, puis générez.")
                                    : tr("Partition prête : vérifiez-la, retouchez-la si besoin, puis générez le morceau."));
    } else {
        notify(plan_job->error.empty() ? std::string(tr("La préparation de la partition a échoué.")) : plan_job->error);
    }
    plan_job.reset();
}

// 1.0.89 : partition reconnue dans un enregistrement (fichier audio ou morceau de la bibliothèque) par SheetSage2
void App::start_recognize(const fs::path & audio, const std::string & label) {
    if (audio.empty() || plan_busy()) return;
    plan_job             = std::make_unique<PlanJob>();
    plan_job->started    = ImGui::GetTime();
    plan_job->label      = label;
    plan_job->recognized = true;
    const double secs    = media_duration(audio);
    plan_job->expected   = std::max(20.0, secs * 0.35);  // ≈ 1 min pour 3 min de musique (mesuré : 60 s pour 187 s)
    PlanJob *  j         = plan_job.get();
    Studio *   st        = studio;
    const bool melody    = score_mode == 1;
    j->thread            = std::thread([j, st, audio, melody]() {
        j->abc  = st->transcribe_score(audio, melody, j->error);
        j->done = true;
    });
    form_tab_request = 2;
}

fs::path App::ask_audio_for_score() {
    wchar_t       file[MAX_PATH] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner       = hwnd;
    std::wstring filter = file_filter(wtr("Fichiers audio"), L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.opus");
    std::wstring title  = wtr("Morceau dont reconnaître la partition");
    ofn.lpstrFilter     = filter.c_str();
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = title.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
}

void App::open_abc_file() {
    wchar_t       file[MAX_PATH] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner       = hwnd;
    std::wstring filter = file_filter(wtr("Partitions ABC"), L"*.abc;*.txt");
    std::wstring title  = wtr("Ouvrir une partition");
    ofn.lpstrFilter     = filter.c_str();
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = title.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    std::ifstream in(fs::path(file), std::ios::binary);
    std::string   text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("K:") == std::string::npos) {
        notify(tr("Ce fichier ne ressemble pas à une partition ABC (pas de ligne « K: »)."));
        return;
    }
    score_abc = score_original = text;
    score_source               = narrow(fs::path(file).filename().wstring());
    score_tempo_edit           = parse_score(text).tempo;
}

// Actions › « Utiliser cette partition… » : formulaire rempli comme « Modifier et renvoyer », partition du morceau dans l'onglet
void App::use_song_score(const Song & s) {
    const ScoreInfo & sc = score_info(s);
    if (!sc.present) return;
    reuse(s);
    score_abc = score_original = sc.text;
    score_source               = display_title(s.title);
    score_tempo_edit           = sc.tempo;
    score_mode                 = s.cot == "melody" ? 1 : 0;
    form_tab_request           = 2;
}

void App::draw_score_tab() {
    const float field_x = 150 * g_dpi;
    colored_wrapped(P.muted, tr("Le modèle écrit d'abord la partition du morceau (tonalité, tempo, accords, mélodie), puis compose le son en la suivant. Ici, vous pouvez préparer cette partition seule (≈ 1 min), la vérifier, la transposer ou changer son tempo, puis générer le morceau avec elle. Le titre, le style et les paroles sont ceux de l'onglet « Nouveau morceau »."));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    begin_card(P.violet, "score_source");
    Form &            f      = form;
    const std::string lyrics = trim(f.instrumental ? f.lyrics_inst : f.lyrics_song);
    ImGui::TextColored(P.muted, "%s", tr("Morceau"));
    ImGui::SameLine(field_x);
    ImGui::TextWrapped("%s", trim(f.title).empty() ? tr("(sans titre : à renseigner dans « Nouveau morceau »)") : trim(f.title).c_str());
    ImGui::TextColored(P.muted, "%s", tr("Style"));
    ImGui::SameLine(field_x);
    ImGui::TextWrapped("%s", trim(f.style).empty() ? tr("(vide)") : trim(f.style).c_str());
    ImGui::TextColored(P.muted, "%s", tr("Partition"));
    ImGui::SameLine(field_x);
    ImGui::RadioButton(tr("Complète (mélodie + accords)"), &score_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton(tr("Mélodie seule"), &score_mode, 1);
    const bool busy = plan_busy() || studio->local_active() || ai_local_busy();
    const bool ready = !trim(f.title).empty() && !trim(f.style).empty() && (f.instrumental || !lyrics.empty()) && f.cover.empty();
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::BeginDisabled(busy || !ready);
    if (accent_button(score_abc.empty() ? tr("Préparer la partition") : tr("Préparer une autre partition"), ImVec2(0, 0), &P.violet)) start_plan();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", !f.cover.empty() ? tr("Une reprise a déjà sa partition (celle du fichier d'origine).")
                                : !ready         ? tr("Renseignez d'abord le titre, le style et les paroles dans « Nouveau morceau ».")
                                : busy           ? tr("Indisponible pendant une génération ou une demande à l'IA locale.")
                                                 : tr("Le modèle écrit la partition seule, sans le son (≈ 1 min). Les LoRA ne servent qu'à la génération du morceau."));
    // 1.0.89 : ou partir de la partition d'un morceau existant (mélodie et accords reconnus par SheetSage2)
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    ImGui::TextColored(P.muted, "%s", tr("Ou partir d'un morceau existant :"));
    const bool can_rec = studio->can_transcribe() && !busy;
    ImGui::BeginDisabled(!can_rec);
    if (soft_button(tr("Reconnaître la partition d'un fichier audio…"), P.teal)) {
        fs::path a = ask_audio_for_score();
        if (!a.empty()) start_recognize(a, narrow(a.stem().wstring()));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", !studio->can_transcribe() ? tr("Modèles de transcription absents (SheetSage2 et MERT dans modeles\\musique).")
                                : busy                    ? tr("Indisponible pendant une génération ou une demande à l'IA locale.")
                                                          : tr("SheetSage2 écoute le morceau et écrit sa partition (mélodie, et accords si « Complète » est choisi), ≈ 1 min pour 3 min de musique. Vous la retouchez, puis vous générez avec vos paroles et votre style : comme une reprise, mais la partition est visible et modifiable."));
    ImGui::SameLine();
    ImGui::BeginDisabled(plan_busy());
    if (soft_button(tr("Ouvrir une partition (.abc)…"), P.blue)) open_abc_file();
    ImGui::EndDisabled();
    if (soft_button(tr("Importer MusicXML ou MIDI (Atelier partition)…"), P.violet))  // 1.0.93
        open_atelier(std::string(), tr("Atelier partition"), std::string(), "partition", fs::path());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Ouvre l'Atelier partition vide : « Importer… » lit un fichier MusicXML, MIDI ou ABC, l'affiche en notes, puis « Utiliser dans le Studio » le place ici."));
    if (plan_busy()) {
        const float t = (float) (ImGui::GetTime() - plan_job->started);
        ImGui::SameLine();
        progress_bar(std::min(0.95f, t / (float) plan_job->expected), ImVec2(-FLT_MIN, 0),
                     trf(plan_job->recognized ? tr("Reconnaissance de la partition… %d s") : tr("Écriture de la partition… %d s"), (int) t).c_str());
    }
    end_card();
    if (score_abc.empty()) return;

    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    begin_card(P.accent, "score_edit");
    const ScoreInfo sc = parse_score(score_abc);
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, "%s", trf("Partition de « %s »", score_source.c_str()).c_str());
    ImGui::PopFont();
    ImGui::TextColored(P.text, "%s", trf("Tonalité : %s (%s)", key_name(sc.key).c_str(), sc.key.c_str()).c_str());
    ImGui::SameLine(0, 20 * g_dpi);
    ImGui::TextColored(P.text, "%s", trf("Tempo : %d BPM", sc.tempo).c_str());
    ImGui::SameLine(0, 20 * g_dpi);
    ImGui::TextColored(P.text, "%s", trf("Mesure : %s", sc.meter.c_str()).c_str());
    // retouches
    ImGui::TextColored(P.muted, "%s", tr("Transposer"));
    ImGui::SameLine(field_x);
    if (ImGui::SmallButton(tr("- 1 demi-ton"))) score_abc = abc_transpose(score_abc, -1);
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("+ 1 demi-ton"))) score_abc = abc_transpose(score_abc, 1);
    ImGui::SameLine();
    ImGui::TextColored(P.muted, "%s", tr("(notes et accords)"));
    ImGui::TextColored(P.muted, "%s", tr("Tempo"));
    ImGui::SameLine(field_x);
    ImGui::SetNextItemWidth(130 * g_dpi);
    ImGui::InputInt("##score_tempo", &score_tempo_edit, 1, 5);
    score_tempo_edit = std::clamp(score_tempo_edit, 40, 260);
    ImGui::SameLine();
    ImGui::BeginDisabled(score_tempo_edit == sc.tempo);
    if (ImGui::SmallButton(tr("Appliquer le tempo"))) {
        score_abc = abc_set_tempo(score_abc, score_tempo_edit);
        // le « NN BPM » du style suit, pour que style et partition disent la même chose
        std::vector<std::string> tags = split_style(form.style);
        bool                     found = false;
        for (std::string & tag : tags)
            if (tag.size() > 4 && lower(tag).find(" bpm") != std::string::npos && isdigit((unsigned char) tag[0])) {
                tag   = std::to_string(score_tempo_edit) + " BPM";
                found = true;
            }
        if (found) form.style = join_style(tags);
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::SameLine(field_x);
    ImGui::BeginDisabled(score_abc == score_original);
    if (ImGui::SmallButton(tr("Revenir à la partition d'origine"))) {
        score_abc        = score_original;
        score_tempo_edit = parse_score(score_abc).tempo;
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (ImGui::BeginTable("score_tab_sections", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Section"), ImGuiTableColumnFlags_WidthFixed, 140 * g_dpi);
        ImGui::TableSetupColumn(tr("Accords"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto & [name, chords] : sc.sections) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.empty() ? tr("(morceau entier)") : name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", chords.c_str());
        }
        ImGui::EndTable();
    }
    if (accent_button(tr("Voir en notes…"), ImVec2(0, 0), &P.violet))  // 1.0.91 : Atelier partition
        open_atelier(score_abc, score_source, trim(form.artist), score_source, fs::path(),
                     trim(form.instrumental ? std::string() : form.lyrics_song));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ouvre l'Atelier partition : la partition en notes, à écouter, imprimer ou enregistrer en PDF."));
    if (ImGui::CollapsingHeader(tr("Modifier le texte de la partition (ABC, pour les connaisseurs)"))) {
        tx::InputTextMultiline("##score_text", &score_abc, ImVec2(-FLT_MIN, 260 * g_dpi));
    }
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    const bool can_generate = ready && !trim(score_abc).empty();
    ImGui::BeginDisabled(!can_generate);
    if (accent_button(tr("Générer avec cette partition"), ImVec2(0, 0), &P.accent)) {
        submit_abc = score_abc;
        const int keep_cot = f.cot;
        f.cot = score_mode == 1 ? 1 : 0;
        submit(false);
        f.cot = keep_cot;
        submit_abc.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Ajouter en pause"))) {
        submit_abc = score_abc;
        const int keep_cot = f.cot;
        f.cot = score_mode == 1 ? 1 : 0;
        submit(true);
        f.cot = keep_cot;
        submit_abc.clear();
    }
    ImGui::EndDisabled();
    if (!can_generate && !trim(score_abc).empty()) {  // 1.0.94 : dire pourquoi les boutons sont grisés
        if (!f.cover.empty())
            colored_wrapped(P.warn, tr("« Nouveau morceau » est en mode Reprise : repassez en mode Chanson ou Instrumental pour créer un morceau avec cette partition."));
        else {
            std::string missing;
            auto add = [&](bool absent, const char * what) { if (absent) missing += (missing.empty() ? "" : ", ") + std::string(what); };
            add(trim(f.title).empty(), tr("le titre"));
            add(trim(f.style).empty(), tr("le style"));
            add(!f.instrumental && lyrics.empty(), tr("les paroles"));
            colored_wrapped(P.warn, trf("Pour générer, renseignez dans « Nouveau morceau » : %s.", missing.c_str()).c_str());
        }
    }
    muted_wrapped(tr("Le morceau suit la partition donnée (tonalité, tempo, accords, mélodie) ; le style, les paroles, les LoRA et les réglages sont ceux de « Nouveau morceau »."));
    end_card();
}

void App::draw_form() {
    // Deux onglets (1.0.26) : le formulaire habituel et « Composer avec l'IA », qui le remplit.
    // Les deux onglets doivent être déclarés à chaque image AVANT de fermer la barre : ImGui n'affiche que les
    // onglets déclarés (1.0.27 : en 1.0.26, la barre était fermée dès le premier onglet, le second n'apparaissait pas).
    int shown = 0;  // 0 Nouveau morceau, 1 Composer avec l'IA, 2 Partition (1.0.88)
    // 1.1.008 : suivis des travaux de fond déplacés dans update_jobs (appelé à chaque image, quelle que soit la page)
    if (ImGui::BeginTabBar("##form_tabs")) {
        bool to_form = form_tab_request == 0, to_ai = form_tab_request == 1, to_score = form_tab_request == 2;
        form_tab_request = -1;
        if (ImGui::BeginTabItem(tr("Nouveau morceau"), nullptr, to_form ? ImGuiTabItemFlags_SetSelected : 0)) {
            shown = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Composer avec l'IA"), nullptr, to_ai ? ImGuiTabItemFlags_SetSelected : 0)) {
            shown = 1;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Partition"), nullptr, to_score ? ImGuiTabItemFlags_SetSelected : 0)) {
            shown = 2;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    form_shown = shown;  // 1.1.003 : entrée du menu de la disposition B
    if (shown == 2) {
        ImGui::BeginChild("##score_body", ImVec2(0, 0), 0);
        draw_score_tab();
        ImGui::EndChild();
    } else if (shown == 1) {
        ImGui::BeginChild("##ai_body", ImVec2(0, 0), 0);
        draw_ai_tab();
        ImGui::EndChild();
    } else if (settings.layout >= 1) {
        // Dispositions A et B (1.1.002) : « Générer » reste en bas du panneau ; seul le formulaire défile au-dessus.
        // Hauteur du pied mesurée à l'image précédente (bouton + éventuelle ligne « À renseigner… »).
        static float foot_h = 60 * g_dpi;
        ImGui::BeginChild("##form_body", ImVec2(0, -foot_h), 0);
        draw_form_body();
        ImGui::EndChild();
        const float y0 = ImGui::GetCursorPosY();
        ImGui::Separator();
        draw_generate_buttons();
        foot_h = ImGui::GetCursorPosY() - y0 + ImGui::GetStyle().ItemSpacing.y;
    } else {
        ImGui::BeginChild("##form_body", ImVec2(0, 0), 0);
        draw_form_body();
        ImGui::EndChild();
    }
}

void App::draw_form_body() {
    if (scroll_form_top) {
        ImGui::SetScrollY(0);
        scroll_form_top = false;
    }
    draw_form_toolbar();
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    draw_song_section();
    draw_style_section();
    draw_lyrics_section();
    draw_lora_section();
    draw_generation_section();
    if (g_collapsed_dirty) {
        g_collapsed_dirty = false;
        settings.collapsed.assign(g_collapsed.begin(), g_collapsed.end());
        save_settings(settings, settings_path);
    }
}

fs::path App::ai_models_dir() const { return studio->paths().models_dir / "ia"; }

// Cible du téléchargement de llama.cpp Vulkan : toujours outils\llama-vulkan (1.0.42).
fs::path App::llama_dir() const { return app_dir() / "outils" / "llama-vulkan"; }

void App::refresh_local_llm(bool force) {
    if (!force && ImGui::GetTime() - local_llm_scan_at < 5) return;
    local_llm         = find_local_llm(app_dir());
    local_llm_scan_at = ImGui::GetTime();
}

// Modèles GGUF présents dans models\ia (catalogue ou déposés à la main).
std::vector<std::string> App::ai_local_files() const {
    std::vector<std::string> out;
    std::error_code          ec;
    for (const auto & e : fs::directory_iterator(ai_models_dir(), ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".gguf" && e.file_size(ec) > 100ull * 1048576) out.push_back(narrow(e.path().filename().wstring()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool App::ai_model_installed(const AiModelFile & m) const {
    std::error_code ec;
    auto            n = fs::file_size(ai_models_dir() / widen(m.file), ec);
    return !ec && n >= m.bytes;
}

std::string App::ai_model_name(const std::string & file) const {
    for (const AiModelFile & m : ai_model_catalog()) if (file == m.file) return m.name;
    return file;
}

AiFit App::ai_fit(unsigned long long bytes) const {
    // 1.0.28 : la carte graphique est toujours utilisée quand il y en a une (demande de Herve).
    return ai_model_fit(bytes, gpu_profile().gpu.vram, sysinfo.ram_total, gpu_profile().gpu.vram > 0);
}

// Moteur choisi dans Réglages › IA et prêt à servir ? why : raison sinon. label : nom affiché.
bool App::ai_engine_ready(std::string & why, std::string & label) {
    const std::string & e = settings.ai_engine;
    if (e == "local") {
        refresh_local_llm();
        std::error_code ec;
        label = trf("IA locale · %s", settings.ai_local_model.empty() ? "?" : ai_model_name(settings.ai_local_model).c_str());
        if (!local_llm.ready()) why = tr("llama.cpp est introuvable : voir Réglages › IA.");
        else if (gpu_profile().gpu.vram > 0 && !local_llm.vulkan)  // 1.0.28 : pas d'IA locale sur le processeur seul
            why = tr("Il manque llama.cpp pour la carte graphique : sans lui, l'IA locale tournerait sur le processeur (très lent).");
        else if (settings.ai_local_model.empty() || !fs::exists(ai_models_dir() / widen(settings.ai_local_model), ec))
            why = tr("Aucun modèle IA local choisi : téléchargez-en un dans Réglages › IA.");
        else if (studio->local_active() || (cover_job && cover_job->local))
            why = tr("L'IA locale ne travaille qu'au repos : attendez la fin de la génération en cours.");
        return why.empty();
    }
    if (e == "server") {
        label = trf("Serveur local · %s", settings.ai_server_name.empty() ? settings.ai_server_url.c_str() : settings.ai_server_name.c_str());
        if (settings.ai_server_url.empty()) why = tr("Aucun serveur local connecté : voir Réglages › IA.");
        else if (settings.ai_server_model.empty()) why = tr("Choisissez le modèle du serveur local dans Réglages › IA.");
        return why.empty();
    }
    const AiService * s = find_ai_service(e);
    if (!s) {
        label = tr("Aucun moteur");
        why   = tr("Choisissez un moteur d'IA dans Réglages › IA.");
        return false;
    }
    label = tr(s->name);
    if (settings.ai_keys[e].empty() && e != "custom") why = trf("Clé d'API de %s manquante : voir Réglages › IA.", tr(s->name));
    else if (e == "custom" && settings.ai_custom_url.empty()) why = tr("Adresse du service manquante : voir Réglages › IA.");
    else if (ai_service_model(e).empty()) why = tr("Choisissez un modèle dans Réglages › IA.");
    return why.empty();
}

std::string App::ai_service_model(const std::string & id) {
    auto it = settings.ai_models.find(id);
    if (it != settings.ai_models.end() && !it->second.empty()) return it->second;
    const AiService * s = find_ai_service(id);
    return s ? s->model : "";
}

// Point d'accès d'un service en ligne ou du serveur local choisi.
AiEndpoint App::ai_endpoint(const std::string & id) {
    AiEndpoint ep;
    if (id == "server") {
        ep.base_url = settings.ai_server_url;
        ep.model    = settings.ai_server_model;
        return ep;
    }
    const AiService * s = find_ai_service(id);
    if (!s) return ep;
    ep.kind     = s->kind;
    ep.base_url = id == "custom" ? settings.ai_custom_url : s->base_url;
    ep.key      = ai_unprotect_key(settings.ai_keys[id]);
    ep.model    = ai_service_model(id);
    return ep;
}

// Demande construite à partir du formulaire de l'onglet.
AiRequest App::ai_request() const {
    AiRequest r;
    int       li   = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
    r.language     = LANGUAGES[li].tag[0] ? LANGUAGES[li].tag : "English";
    r.language_tag = LANGUAGES[li].tag;
    r.influence    = trim(settings.ai_influence);
    r.theme        = trim(settings.ai_theme);
    r.mood         = trim(settings.ai_mood);
    r.title        = trim(settings.ai_title);
    r.extra        = trim(settings.ai_extra);
    r.instrumental = settings.ai_instrumental;
    r.length       = settings.ai_length;
    r.avoid        = ai_avoid_words();
    ai_set_natural(settings.ai_natural);  // 1.1.018
    r.negative     = join_style(split_style(settings.ai_negative));  // 1.0.34
    return r;
}

// ---- Anti-répétition (1.0.33)
// IA locale (texte) ou pochettes locales en cours : carte graphique occupée, pas de génération de morceau (1.0.36).
bool App::ai_local_busy() const { return (ai_job && ai_job->local) || (ai_album && ai_album->local) || (cover_job && cover_job->local) || desc_job; }

// Mots évités automatiquement : présents dans au moins 3 des derniers morceaux écrits par l'IA.
std::vector<std::string> App::ai_auto_words() const {
    if (!settings.ai_auto_avoid || settings.ai_recent.size() < 3) return {};
    return ai_frequent_words(settings.ai_recent, 3, 20);
}

// Liste envoyée à l'IA : celle de Réglages › IA, puis les mots automatiques (sans doublon).
std::vector<std::string> App::ai_avoid_words() const {
    std::vector<std::string> out;
    std::set<std::string>    seen;
    auto add = [&](const std::string & w) {
        std::string t = trim(w);
        if (!t.empty() && seen.insert(fold(t)).second) out.push_back(t);
    };
    for (const std::string & w : settings.ai_avoid) add(w);
    for (const std::string & w : ai_auto_words()) add(w);
    return out;
}

// Retient un morceau écrit par l'IA (titre + paroles) pour l'évitement automatique ; 15 derniers.
void App::remember_ai_song(const AiSong & s, bool instrumental) {
    if (instrumental || !s.ok) return;
    settings.ai_recent.push_back(s.title + "\n" + s.lyrics);
    while (settings.ai_recent.size() > 15) settings.ai_recent.erase(settings.ai_recent.begin());
    save_settings(settings, settings_path);
}

// Phrase affichée sous le formulaire : ce qui sera demandé, en clair.
std::string App::ai_summary() const {
    const AiRequest r    = ai_request();
    int             li   = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
    std::string     lang = tr(LANGUAGES[li].label);
    std::string     s    = r.instrumental ? std::string(tr("Créer un morceau instrumental"))
                                          : trf("Créer un morceau en %s", lang.c_str());
    if (!r.influence.empty()) s += trf(" inspiré par le groupe ou chanteur %s", r.influence.c_str());
    if (!r.theme.empty()) s += trf(" sur le thème %s", r.theme.c_str());
    return s + ".";
}

// Phrase affichée en mode album (1.0.33).
std::string App::ai_album_summary() const {
    const AiAlbumRequest r    = ai_album_request();
    int                  li   = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
    std::string          lang = tr(LANGUAGES[li].label);
    // 1.0.37 : vrais pluriels (« 1 morceau / 10 morceaux », « 1 instrumental / 2 instrumentaux »)
    std::string s;
    if (r.kind == 1) s = r.count == 1 ? std::string(tr("Créer un album instrumental d'un morceau")) : trf("Créer un album instrumental de %d morceaux", r.count);
    else s = r.count == 1 ? trf("Créer un album d'un morceau en %s", lang.c_str()) : trf("Créer un album de %d morceaux en %s", r.count, lang.c_str());
    if (r.kind == 2) s += r.instrumentals == 1 ? std::string(tr(", dont 1 instrumental"))
                        : r.instrumentals > 1  ? trf(", dont %d instrumentaux", r.instrumentals)
                                               : std::string(tr(", avec quelques instrumentaux"));
    if (!r.band.empty()) s += trf(" pour le groupe %s", r.band.c_str());
    if (!r.influence.empty()) s += trf(" inspiré par le groupe ou chanteur %s", r.influence.c_str());
    if (!r.theme.empty()) s += trf(" sur le thème %s", r.theme.c_str());
    return s + ".";
}

void App::start_ai_compose() {
    std::string why, label;
    if (!ai_engine_ready(why, label)) {
        notify(why);
        return;
    }
    if (settings.ai_engine == "local" && other_synth_running()) {  // moteur de synthèse lancé par ailleurs (essai, autre Studio)
        notify(tr("L'IA locale ne travaille qu'au repos : attendez la fin de la génération en cours."));
        return;
    }
    auto job          = std::make_shared<AiJob>();
    job->instrumental = settings.ai_instrumental;
    job->local        = settings.ai_engine == "local";
    job->started      = ImGui::GetTime();
    ai_job            = job;
    const AiRequest req    = ai_request();
    std::string     system = ai_system_prompt(req.instrumental), user = ai_user_prompt(req);
    AiEndpoint      ep     = job->local ? AiEndpoint{} : ai_endpoint(settings.ai_engine);
    LocalLlm        llm    = local_llm;
    fs::path        model  = ai_models_dir() / widen(settings.ai_local_model);
    std::error_code ec;
    auto            size   = fs::file_size(model, ec);
    // Carte graphique forcée (1.0.28) : tout le modèle sur la carte s'il y tient, sinon le plus possible.
    int             layers = llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
    std::vector<std::string> avoid = req.avoid;
    const bool very_long = req.length == 3 && !req.instrumental;
    const bool polish    = settings.ai_polish && !req.instrumental;  // 1.1.021
    const std::string lang = req.language;
    std::thread([job, system, user, ep, llm, model, layers, avoid, very_long, polish, lang] {
        auto ask = [&](const std::string & u) {
            return job->local ? ai_local_chat(llm, model, layers, system, u, &job->stage, &job->cancel) : ai_chat(ep, system, u, 180000);
        };
        job->result = ask(user);
        if (job->result.ok) {
            job->song = ai_parse_song(job->result.text, job->instrumental);
            // Réponse illisible : une seule nouvelle demande, avec un rappel du format.
            if (!job->song.ok && !job->cancel) {
                AiCallResult again = ask(user + "\n\n" + ai_retry_prompt());
                if (again.ok) {
                    AiSong s2 = ai_parse_song(again.text, job->instrumental);
                    if (s2.ok || !job->song.ok) {
                        job->result = again;
                        job->song   = s2;
                    }
                }
            }
            // Mots interdits dans le titre ou les paroles (1.0.33) : une seule nouvelle demande ; gardée si elle
            // est lisible et contient moins de mots interdits.
            // Instrumental : seul le titre est vérifié (1.0.34 : « Whispers in the Shadows » passait).
            std::vector<std::string> found = job->song.ok ? ai_forbidden_found(job->instrumental ? job->song.title : job->song.title + "\n" + job->song.lyrics, avoid)
                                                          : std::vector<std::string>{};
            if (!found.empty() && !job->cancel) {
                AiCallResult again = ask(user + "\n\n" + ai_avoid_retry_prompt(found));
                if (again.ok) {
                    AiSong s2 = ai_parse_song(again.text, job->instrumental);
                    if (s2.ok && ai_forbidden_found(job->instrumental ? s2.title : s2.title + "\n" + s2.lyrics, avoid).size() < found.size()) {
                        job->result = again;
                        job->song   = s2;
                    }
                }
            }
            // 1.0.37 : titre encore fautif → un titre seul est redemandé
            std::vector<std::string> bad_title = job->song.ok ? ai_forbidden_found(job->song.title, avoid) : std::vector<std::string>{};
            if (!bad_title.empty() && !job->cancel) {
                AiCallResult b  = ask(user + "\n\n" + ai_title_retry_prompt(job->song.title, bad_title, avoid));
                std::string  nt = b.ok ? ai_parse_title(b.text) : std::string();
                if (!nt.empty() && ai_forbidden_found(nt, avoid).empty()) job->song.title = nt;
            }
            if (job->song.ok && !job->instrumental) {  // 1.0.73 : couplets recopiés retirés
                std::set<std::string> known;
                ai_drop_repeated_lines(job->song.lyrics, known);
            }
            if (polish && job->song.ok && !job->cancel)  // 1.1.021 : relecture avant la suite d'un morceau très long
                ai_polish_song(job->song, lang, avoid, [&](const std::string & sy, const std::string & u) {
                    return job->local ? ai_local_chat(llm, model, layers, sy, u, &job->stage, &job->cancel) : ai_chat(ep, sy, u, 180000);
                }, &job->cancel);
            if (very_long)
                ai_lengthen_song(job->song, AI_LONG_LINES, avoid, [&](const std::string & sy, const std::string & u) {
                    return job->local ? ai_local_chat(llm, model, layers, sy, u, &job->stage, &job->cancel) : ai_chat(ep, sy, u, 180000);
                }, &job->cancel);
        }
        job->state = 1;
    }).detach();
}

// Fin d'une demande : la réponse brute et le morceau lu s'affichent dans l'onglet.
void App::poll_ai_job() {
    if (!ai_job || ai_job->state != 1) return;
    auto job = ai_job;
    ai_job.reset();
    if (!job->result.ok) {
        if (!job->cancel) notify(trf(tr(job->result.error.c_str()), job->result.error_arg.c_str()));
        return;
    }
    ai_song              = job->song;
    ai_song_instrumental = job->instrumental;
    remember_ai_song(ai_song, job->instrumental);
    // Réponse présentée en texte lisible (1.0.28) ; brute (JSON) seulement si elle n'a pas pu être lue.
    ai_response          = ai_song.ok ? ai_song_text(ai_song, job->instrumental) : job->result.text;
    // Rangé d'office dans « Nouveau morceau », sauf s'il contient déjà des paroles écrites dans le même mode
    // (chanson ou instrumental) : on demande alors avant de les remplacer (1.0.28 : un titre seul ne bloque plus).
    if (ai_song.ok && !form_has_content(job->instrumental)) send_ai_song();  // annonce « … est dans Nouveau morceau »
    else if (ai_song.ok) {
        ai_confirm_send  = true;
        form_tab_request = 1;  // la question s'affiche dans l'onglet « Composer avec l'IA »
    } else notify(tr("Réponse de l'IA illisible : copiez les éléments utiles à la main depuis le champ « Réponse de l'IA »."));
}

// Range le morceau dans « Nouveau morceau » (titre, style avec la langue, paroles) et y bascule.
// Titre, style (avec la langue et le profil d'influence connu) et paroles d'un morceau écrit par l'IA dans le
// formulaire (« Envoyer dans Nouveau morceau », et chaque morceau d'un album, 1.0.33).
// Style d'un morceau écrit par l'IA tel qu'il part dans « Nouveau morceau » (sans la langue) : complété par le
// profil d'influence connu, sans les balises des styles à éviter (1.0.34).
std::string App::ai_song_style(const std::string & raw) const {
    std::string style = raw;
    // Profil d'influence connu (Réglages : liste des groupes et chanteurs) : ses balises complètent le style.
    if (settings.ai_profile) {
        std::string want = fold(trim(settings.ai_influence));
        for (const Influence & inf : INFLUENCES) {
            if (!want.empty() && fold(inf.name) == want) style = merge_style(style, split_style(influence_style(inf)));
        }
    }
    return ai_strip_negative(style, join_style(split_style(settings.ai_negative)));
}

void App::fill_form_with(Form & f, const AiSong & s, bool inst) {
    set_mode(f, inst);
    if (!s.title.empty()) f.title = s.title;
    std::string style = ai_song_style(s.style);
    // Styles à éviter de l'onglet (1.0.34) : appliqués au « Style à éviter » du morceau ; vides = celui du formulaire.
    if (!trim(settings.ai_negative).empty()) f.negative_style = join_style(split_style(settings.ai_negative));
    // 1.0.37 : instrumental sans voix quoi qu'ait écrit l'IA (style sans balise vocale + « instrumental, no vocals »,
    // voix ajoutées au style à éviter)
    if (inst) {
        style            = ai_force_instrumental_style(style);
        f.negative_style = ai_instrumental_negative(f.negative_style);
    }
    int li = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
    f.style    = set_language(style, inst ? "" : LANGUAGES[li].tag);
    f.language = inst ? f.language : li;
    (inst ? f.lyrics_inst : f.lyrics_song) = s.lyrics;
    // 1.0.49 : la durée suit les paroles écrites (sinon une durée fixe du formulaire, souvent 3 min, coupe le morceau) ;
    // « Très longue » : morceau en 2 parties enchaînées (découpage des paroles de la 1.0.48 à l'envoi).
    f.auto_duration = true;
    f.long_track    = settings.ai_length == 3;
    sync_lora_triggers();  // les LoRA cochées gardent leur mot déclencheur
}

void App::send_ai_song() {
    Form & f = form;
    fill_form_with(f, ai_song, ai_song_instrumental);
    f.cursor = -1;
    f.dirty  = true;
    ImGui::ClearActiveID();
    scroll_form_top  = true;
    form_tab_request = 0;
    notify(trf("« %s » est dans Nouveau morceau.", ai_song.title.empty() ? tr("Sans titre") : ai_song.title.c_str()));
}

void App::open_ai_style_save(const std::string & style, const std::string & name) {
    ai_style_text      = style;
    ai_style_name      = trim(name);
    ai_style_conflict  = false;
    ai_style_from_song = false;
    ai_style_open      = true;
}

void App::open_song_style_save(const Song & s) {
    open_ai_style_save(s.style, s.artist.empty() ? display_title(s.title) : s.artist);
    ai_style_from_song = true;
    ai_style_song_loras.clear();
    for (const auto & [name, scale] : s.adapters) ai_style_song_loras.push_back({ name, (float) scale, true });
    ai_style_song_negative = s.negative_style;
    ai_style_song_scale    = (float) s.negative_scale;
}

std::string App::ai_style_negative() const {
    return join_style(split_style(ai_style_from_song ? ai_style_song_negative : settings.ai_negative));
}

std::vector<LoraUse> App::ai_style_loras() const {
    std::vector<LoraUse> chosen;
    for (const LoraUse & u : ai_style_from_song ? ai_style_song_loras : form.loras) {
        if (u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE) chosen.push_back(u);
    }
    return chosen;
}

bool App::personal_preset_exists(const std::string & name) const {
    for (const Preset & p : settings.presets) if (p.pack.empty() && fold(p.name) == fold(name)) return true;
    return false;
}

void App::store_ai_style(bool replace) {
    Preset np;
    np.name  = trim(ai_style_name);
    np.style = ai_style_text;
    if (ai_style_with_loras) np.loras = ai_style_loras();
    std::string negative = ai_style_negative();
    if (ai_style_with_negative && !negative.empty()) {
        np.has_negative   = true;
        np.negative_style = negative;
        np.negative_scale = ai_style_from_song ? ai_style_song_scale : form.negative_scale;
    }
    if (replace) {
        for (Preset & p : settings.presets) {
            if (p.pack.empty() && fold(p.name) == fold(np.name)) {
                np.name           = p.name;
                np.structure_song = p.structure_song;  // la structure enregistrée avant est gardée
                np.structure_inst = p.structure_inst;
                p                 = np;
                replace           = false;  // remplacé
                break;
            }
        }
        if (replace) settings.presets.push_back(np);
    } else {
        std::string base = np.name;
        for (int n = 2; personal_preset_exists(np.name); n++) np.name = base + " (" + std::to_string(n) + ")";
        settings.presets.push_back(np);
    }
    save_settings(settings, settings_path);
    notify(trf("Style « %s » enregistré dans vos styles.", np.name.c_str()));
}

// 1.1.011 : « Sauvegarder le style de ce groupe » : le style du morceau lancé est enregistré sous le nom du groupe,
// seulement si aucun de vos styles ne porte déjà ce nom (un style existant n'est jamais remplacé en silence).
void App::auto_group_style(const std::string & group, const std::string & style) {
    if (personal_preset_exists(group)) return;
    Preset np;
    np.name  = group;
    np.style = style;
    for (const LoraUse & u : form.loras)
        if (u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE) np.loras.push_back(u);
    const std::string neg = join_style(split_style(form.negative_style));
    if (!neg.empty()) {
        np.has_negative   = true;
        np.negative_style = neg;
        np.negative_scale = form.negative_scale;
    }
    settings.presets.push_back(np);
    save_settings(settings, settings_path);
    journal::event("style", "Style du groupe « " + group + " » enregistré : " + style.substr(0, 200));
    notify(trf("Style « %s » enregistré dans vos styles.", group.c_str()));
}

void App::draw_group_style_checkbox() {
    if (ImGui::Checkbox(tr("Sauvegarder le style de ce groupe"), &settings.save_group_style)) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("À chaque morceau lancé, le style est enregistré dans vos styles sous le nom du groupe (s'il n'existe pas déjà), pour reproduire le même groupe plus tard."));
}

// Albums › « Style perso… » : style personnalisé au nom du groupe, avec les balises communes à au moins la moitié de ses morceaux.
void App::open_group_style(const std::vector<Song> & all, const std::string & artist) {
    std::vector<const Song *> mine;
    for (const Song & s : all) if (s.status == "done" && fold(s.artist) == fold(artist)) mine.push_back(&s);
    if (mine.empty()) return;
    std::map<std::string, int> count;
    std::vector<std::string>   order;
    for (const Song * s : mine) {
        std::set<std::string> seen;
        for (const std::string & t : split_tags(s->style)) {
            const std::string k = lower(t);
            if (!seen.insert(k).second) continue;
            if (!count.count(k)) order.push_back(t);
            count[k]++;
        }
    }
    std::vector<std::string> keep;
    for (const std::string & t : order) if (count[lower(t)] * 2 >= (int) mine.size()) keep.push_back(t);
    const std::string style = keep.empty() ? mine[0]->style : join_style(keep);
    open_song_style_save(*mine[0]);
    ai_style_text = style;
    ai_style_name = artist;
}

bool App::tag_in(const std::vector<std::string> & v, const std::string & t) {
    for (const std::string & x : v) if (lower(x) == lower(t)) return true;
    return false;
}

void App::tag_erase(std::vector<std::string> & v, const std::string & t) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string & x) { return lower(x) == lower(t); }), v.end());
}

void App::style_tags_changed() {
    tags_dirty = true;
    save_settings(settings, settings_path);
}

bool App::style_tags_customized() const {
    for (int gi = 0; gi < 5; gi++) if (!settings.tag_added[gi].empty() || !settings.tag_hidden[gi].empty()) return true;
    return false;
}

// Une pastille de la liste : renvoie vrai au clic.
bool App::style_tag_chip(const std::string & text, const ImVec4 & color, float avail, float & x, bool first, int id) {
    ImGuiStyle & st = ImGui::GetStyle();
    float        w  = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
    if (!first && x + st.ItemSpacing.x * 0.6f + w <= avail) {
        ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
        x += st.ItemSpacing.x * 0.6f + w;
    } else {
        x = w;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushID(id);
    const bool clicked = ImGui::SmallButton(text.c_str());
    ImGui::PopID();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return clicked;
}

void App::draw_style_tags_settings() {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, "%s", tr("Balises de style"));
    ImGui::PopFont();
    muted_wrapped(tr("Balises proposées dans le bloc Style de « Nouveau morceau ». La croix retire une balise de la liste (un style déjà écrit la garde) ; le champ en ajoute de nouvelles, en anglais comme le modèle (plusieurs séparées par des virgules). Les balises ajoutées sont précédées d'un point."));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    for (int gi = 0; gi < 5; gi++) {
        const ChipGroup & g     = STYLE_GROUPS[gi];
        const ImVec4      color = category_color(g.color);
        begin_card(color, ("c_style_tags" + std::to_string(gi)).c_str());
        const std::vector<const char *> items = group_items(g);
        ImGui::PushFont(g_bold);
        ImGui::TextColored(color, "%s", tr(g.name));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", trf("%d balises", (int) items.size()).c_str());
        ImGui::PushID(gi);
        std::string remove;
        {
            float avail = ImGui::GetContentRegionAvail().x, x = 0;
            for (size_t i = 0; i < items.size(); i++) {
                const bool custom = tag_in(settings.tag_added[gi], items[i]);
                if (style_tag_chip(std::string(custom ? "• " : "") + items[i] + "  ×", color, avail, x, i == 0, (int) i)) remove = items[i];
            }
        }
        if (!remove.empty()) {
            if (tag_in(settings.tag_added[gi], remove)) tag_erase(settings.tag_added[gi], remove);
            else settings.tag_hidden[gi].push_back(remove);
            journal::action("Balise retirée (" + std::string(g.name) + ") : " + remove);
            style_tags_changed();
        }
        ImGui::Dummy(ImVec2(0, 3 * g_dpi));
        const float add_w = ImGui::CalcTextSize(tr("Ajouter")).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::min(340 * g_dpi, ImGui::GetContentRegionAvail().x - add_w));
        const bool enter = tx::InputTextWithHint("##tag_new", tr("Nouvelle balise (en anglais, ex. : dream pop)"), &style_tag_new[gi], ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trim(style_tag_new[gi]).empty());
        if ((ImGui::Button(tr("Ajouter")) || enter) && !trim(style_tag_new[gi]).empty()) {
            for (const std::string & t : split_tags(style_tag_new[gi])) {
                if (tag_in(settings.tag_hidden[gi], t)) {
                    tag_erase(settings.tag_hidden[gi], t);  // balise par défaut retirée avant : elle revient
                } else {
                    bool present = tag_in(settings.tag_added[gi], t);
                    for (const char * e : items) present = present || lower(e) == lower(t);
                    if (!present) settings.tag_added[gi].push_back(t);
                }
                journal::action("Balise ajoutée (" + std::string(g.name) + ") : " + t);
            }
            style_tag_new[gi].clear();
            style_tags_changed();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(settings.tag_added[gi].empty() && settings.tag_hidden[gi].empty());
        if (ImGui::Button(tr("Rétablir ce groupe"))) {
            settings.tag_added[gi].clear();
            settings.tag_hidden[gi].clear();
            style_tags_changed();
        }
        ImGui::EndDisabled();
        if (!settings.tag_hidden[gi].empty()) {  // balises retirées : un clic les remet
            ImGui::TextColored(P.muted, "%s", tr("Balises retirées (cliquez pour les remettre) :"));
            std::string back;
            float       avail = ImGui::GetContentRegionAvail().x, x = 0;
            for (size_t i = 0; i < settings.tag_hidden[gi].size(); i++)
                if (style_tag_chip("+ " + settings.tag_hidden[gi][i], P.muted, avail, x, i == 0, 1000 + (int) i)) back = settings.tag_hidden[gi][i];
            if (!back.empty()) {
                tag_erase(settings.tag_hidden[gi], back);
                style_tags_changed();
            }
        }
        ImGui::PopID();
        end_card();
    }
    ImGui::BeginDisabled(!style_tags_customized());
    if (ImGui::Button(tr("Tout réinitialiser"))) ImGui::OpenPopup("##tags_reset_all");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##tags_reset_all")) {
        ImGui::PushTextWrapPos(380 * g_dpi);
        ImGui::TextWrapped("%s", tr("Remettre toutes les balises par défaut ? Vos balises ajoutées sont supprimées et les balises retirées reviennent."));
        ImGui::PopTextWrapPos();
        if (accent_button(tr("Réinitialiser"), ImVec2(0, 0), &P.danger)) {
            for (int gi = 0; gi < 5; gi++) {
                settings.tag_added[gi].clear();
                settings.tag_hidden[gi].clear();
            }
            style_tags_changed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::draw_ai_style_modal() {
    const char * title = tr("Enregistrer comme style personnalisé");
    if (ai_style_open) {
        ImGui::OpenPopup(title);
        ai_style_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(480 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) return;
    muted_wrapped(ai_style_text);
    if (!ai_style_conflict) {
        std::vector<LoraUse> chosen = ai_style_loras();
        if (!chosen.empty()) {
            std::string names;
            for (const LoraUse & u : chosen) names += (names.empty() ? "" : ", ") + lora_label(u.file);
            ImGui::Checkbox((ai_style_from_song ? trf("Inclure les LoRA du morceau et leur force (%s)", names.c_str())
                                                : trf("Inclure les LoRA cochées et leur force (%s)", names.c_str())).c_str(),
                            &ai_style_with_loras);
        }
        std::string negative = ai_style_negative();
        if (!negative.empty()) ImGui::Checkbox(trf("Inclure le style à éviter (%s)", negative.c_str()).c_str(), &ai_style_with_negative);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ImGui::InputTextWithHint("##ai_style_name", tr("Nom du préréglage (ex. : Cold wave 80s)"), &ai_style_name, ImGuiInputTextFlags_EnterReturnsTrue);
        text_context_menu(ai_style_name);
        bool valid = !trim(ai_style_name).empty() && !trim(ai_style_text).empty();
        if (modal_buttons(tr("Enregistrer"), valid) || (enter && valid)) {
            if (personal_preset_exists(ai_style_name)) ai_style_conflict = true;  // question ci-dessous
            else {
                store_ai_style(false);
                ImGui::CloseCurrentPopup();
            }
        }
    } else {
        ImGui::TextWrapped("%s", trf("Un de vos styles s'appelle déjà « %s ». Remplacer : l'ancien style est remplacé par celui-ci. Garder les deux : celui-ci est ajouté sous un autre nom.",
                                     trim(ai_style_name).c_str()).c_str());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(tr("Garder les deux"))) {
            store_ai_style(false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Remplacer"), ImVec2(0, 0), &P.accent)) {
            store_ai_style(true);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// ---- Dernier album écrit par l'IA, gardé dans donnees\dernier_album.json (1.0.36) et relu au démarrage
fs::path App::last_album_file() const { return settings_path.parent_path() / "dernier_album.json"; }

void App::save_last_album() const {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_strcpy(doc, o, "band", ai_album_band.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "album", ai_album_name.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "sound", ai_album_sound.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "cover_prompt", ai_album_cover_prompt.c_str());
    yyjson_mut_val * arr = yyjson_mut_obj_add_arr(doc, o, "tracks");
    for (const AiAlbumLine & l : ai_album_titles) {
        yyjson_mut_val * t = yyjson_mut_arr_add_obj(doc, arr);
        yyjson_mut_obj_add_strcpy(doc, t, "label", l.label.c_str());
        yyjson_mut_obj_add_strcpy(doc, t, "style", l.style.c_str());
        yyjson_mut_obj_add_int(doc, t, "number", l.number);
        yyjson_mut_obj_add_bool(doc, t, "instrumental", l.instrumental);
        yyjson_mut_obj_add_strcpy(doc, t, "title", l.song.title.c_str());
        yyjson_mut_obj_add_strcpy(doc, t, "language", l.song.language.c_str());
        yyjson_mut_obj_add_strcpy(doc, t, "song_style", l.song.style.c_str());
        yyjson_mut_obj_add_strcpy(doc, t, "lyrics", l.song.lyrics.c_str());
    }
    write_text(last_album_file(), write_json(doc));
}

void App::load_last_album() {
    std::string  text = read_text(last_album_file());
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return;
    yyjson_val * r        = yyjson_doc_get_root(doc);
    ai_album_band         = json_str(r, "band");
    ai_album_name         = json_str(r, "album");
    ai_album_sound        = json_str(r, "sound");
    ai_album_cover_prompt = json_str(r, "cover_prompt");
    ai_album_titles.clear();
    size_t       i, n;
    yyjson_val * t;
    yyjson_arr_foreach(yyjson_obj_get(r, "tracks"), i, n, t) {
        AiAlbumLine l;
        l.label        = json_str(t, "label");
        l.style        = json_str(t, "style");
        l.number       = (int) yyjson_get_int(yyjson_obj_get(t, "number"));
        l.instrumental = yyjson_is_true(yyjson_obj_get(t, "instrumental"));
        l.song.title    = json_str(t, "title");
        l.song.language = json_str(t, "language");
        l.song.style    = json_str(t, "song_style");
        l.song.lyrics   = json_str(t, "lyrics");
        l.song.ok       = !l.song.lyrics.empty();
        ai_album_titles.push_back(l);
    }
    yyjson_doc_free(doc);
}
