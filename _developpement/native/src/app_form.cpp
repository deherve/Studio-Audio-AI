// Formulaire « Nouveau morceau » et traduction des paroles (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

// ------------------------------------------------------------------ Formulaire : actions
void App::clear_form() {
    Form & f = form;
    f.title.clear();
    f.style.clear();
    f.negative_style.clear();
    f.artist.clear();
    f.album.clear();
    (f.instrumental ? f.lyrics_inst : f.lyrics_song).clear();
    f.cover.clear();
    f.cover_seconds = 0;
    f.cursor        = -1;
    f.dirty         = true;
    ImGui::ClearActiveID();
    notify(tr("Formulaire vidé : vous pouvez coller un nouveau morceau."));
}

bool App::has_lyrics_text(const std::string & lyrics) {
    size_t start = 0;
    while (start < lyrics.size()) {
        size_t      end  = lyrics.find('\n', start);
        std::string line = trim(lyrics.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!line.empty() && !(line.front() == '[' && line.back() == ']')) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// Paroles en anglais, français, espagnol ou italien ? Compte les mots-outils les plus fréquents.
int App::guess_language(const std::string & lyrics) {
    static const char * en[] = { "the", "and", "you", "is", "my", "to", "in", "of", "me", "it", "we", "your",
                                 "on", "i'm", "don't", "be", "all", "for", "with", "this" };
    static const char * fr[] = { "le", "la", "les", "je", "tu", "et", "est", "une", "des", "que", "dans", "pour",
                                 "pas", "mon", "ma", "qui", "sur", "nous", "c'est", "au" };
    static const char * es[] = { "el", "los", "las", "yo", "eres", "y", "es", "una", "en", "que", "por", "mi", "no", "con" };
    static const char * it[] = { "il", "gli", "io", "sei", "e", "è", "una", "di", "che", "per", "non", "mio", "con", "sono" };
    int         score[5] = { 0, 0, 0, 0, 0 };
    std::string word;
    auto        flush = [&] {
        for (const char * w : en) score[1] += word == w;
        for (const char * w : fr) score[2] += word == w;
        for (const char * w : es) score[3] += word == w;
        for (const char * w : it) score[4] += word == w;
        word.clear();
    };
    for (char c : fold(lyrics) + " ") {
        if (isalpha((unsigned char) c) || c == '\'' || (unsigned char) c >= 0x80) word += c;
        else flush();
    }
    int best = 0;
    for (int i = 1; i < 5; i++) {
        if (score[i] > score[best]) best = i;
    }
    return score[best] >= 5 ? best : 0;
}

// Convertit des blocs au format Suno vers le formulaire au format YuE2.
void App::adapt(const std::string & title_block, const std::string & style_block, const std::string & lyrics_block, bool instrumental) {
    Form &     f  = form;
    TitleBlock tb = parse_title_block(title_block);
    if (!tb.title.empty()) f.title = tb.title;
    if (!tb.artist.empty()) f.artist = tb.artist;
    if (!tb.album.empty()) f.album = tb.album;
    std::vector<std::string> extra;
    std::string              lyrics = convert_lyrics(lyrics_block, instrumental, extra);
    std::string              style  = convert_style(style_block);
    if (style.empty()) style = style_block;  // rien de reconnu : texte d'origine conservé
    std::vector<std::string> tags = split_style(merge_style(style, extra));  // le BPM des paroles l'emporte
    if (tags.size() > 18) tags.resize(18);  // au-delà, le style devient flou et contradictoire
    int lang = instrumental ? 0 : guess_language(lyrics);
    if (lang) f.language = lang;
    if (f.instrumental != instrumental) set_mode(f, instrumental);
    f.style = set_language(join_style(tags), instrumental ? "" : LANGUAGES[f.language].tag);
    (instrumental ? f.lyrics_inst : f.lyrics_song) = lyrics;
    f.auto_duration = true;
    f.cursor        = -1;
    f.dirty         = true;
    ImGui::ClearActiveID();
    notify(trf("Converti au format YuE2 : %d balises de style, %s estimées.", (int) split_style(f.style).size(),
               fmt_time(estimate_seconds(lyrics, instrumental)).c_str()));
}

void App::pick_cover() {
    wchar_t       file[MAX_PATH] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner   = hwnd;
    std::wstring filter = file_filter(wtr("Fichiers audio"), L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.opus");
    std::wstring title  = wtr("Morceau à reprendre");
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title.c_str();
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    Form & f        = form;
    f.cover         = file;
    f.cover_seconds = media_duration(f.cover);
    // Reprise chantée : la mélodie seule guide la voix ; instrumentale : partition complète (accords).
    f.cot = f.instrumental ? 0 : 1;
}

void App::submit(bool held) {
    scan_loras(true);
    sync_lora_triggers();  // mot déclencheur des LoRA cochées : toujours présent dans le style envoyé
    Form &        f      = form;
    std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
    std::string   style  = join_style(split_style(f.style));
    std::string   words  = trim(lyrics);
    // Paroles collées au format Suno sans conversion : adaptées au vol (sections, BPM des paroles…).
    if (needs_conversion(words)) {
        std::vector<std::string> extra;
        words = trim(convert_lyrics(words, f.instrumental, extra));
        style = merge_style(style, extra);
    }
    if (f.instrumental || !has_lyrics_text(words)) {
        // Aucune parole dans la structure : on retire les références à la voix (et la langue chantée) du style,
        // sinon le modèle invente des paroles pour remplir les sections vides.
        std::vector<std::string> kept;
        int                      removed = 0;
        for (const std::string & t : split_tags(style)) {
            bool lang = false;
            for (const auto & L : LANGUAGES) lang = lang || (L.tag[0] && lower(L.tag) == lower(t));
            if (lang || is_voice_tag(t)) removed++;
            else kept.push_back(t);
        }
        if (removed > 0) {
            style = join_style(kept);
            notify(f.instrumental ? trf("Instrumental : %d balise(s) de voix retirée(s) du style avant la génération.", removed)
                                  : trf("Aucune parole dans la structure : %d balise(s) de voix retirée(s) du style avant la génération.", removed));
        }
    }
    // 1.0.48 (prototype) : morceau long. Les paroles sont coupées entre deux sections, vers le milieu ; la partie 2
    // sera lancée seule à la fin de la partie 1 (Studio::worker) et les deux seront assemblées.
    std::string long_rest;
    if (f.long_track && f.cover.empty() && submit_abc.empty()) {  // 1.0.88 : une partition fournie couvre tout le morceau
        // 1.0.70 : 3 parties pour un texte de plus de 6 minutes (estimate_seconds plafonne à 360 s) : chacune reste sous la limite de mémoire vidéo d'une carte de 12 Go
        bool three = ai_sung_lines(words) >= 60 || (int) std::count(words.begin(), words.end(), '[') >= 18;
        // 1.1.020 : aussi 3 parties quand la durée estimée de chaque moitié dépasse ce que la carte supporte (sur 12 Go : « Epic Journey »,
        // 2 parties de 251 s et 290 s, a manqué de mémoire vidéo ; 3 parties de ≈ 180 s restent sous 220 s)
        {
            const GpuMem   gm         = gpu_monitor.get();
            const unsigned long long vram = gm.total > 0 ? gm.total : (unsigned long long) gpu_profile().gpu.vram;  // moniteur pas encore lancé : profil de la carte
            const bool     small_card = vram > 0 && vram <= (unsigned long long) (12.5 * 1073741824.0);
            const double   target     = small_card ? 220.0 : 330.0;
            const double   total_est  = estimate_seconds(words, f.instrumental, 3600);
            if (!three && total_est > 2 * target) {
                three = true;
                notify(trf("Morceau long : coupé en 3 parties d'environ %s chacune pour tenir dans la mémoire de la carte graphique.", fmt_time(total_est / 3).c_str()));
            }
        }
        auto parts = split_long_lyrics(words, three ? 3 : 2);
        if (parts.second.empty()) {
            notify(tr("Morceau long : il faut au moins deux sections ([Verse], [Chorus]…) pour le couper en deux parties."));
            return;
        }
        words     = parts.first;
        long_rest = parts.second;
        if (three) {
            auto rest = split_long_lyrics(long_rest);
            if (!rest.second.empty()) long_rest = rest.first + LONG_PART_MARK + rest.second;
        }
    }
    double estimate = estimate_seconds(words, f.instrumental);
    std::vector<std::string> trial_ids;
    for (int v = 1; v <= f.versions; v++) {
        Song s;
        s.title             = trim(f.title) + (f.versions > 1 ? " (v" + std::to_string(v) + ")" : "");
        s.style             = style;
        s.lyrics            = words;
        s.instrumental      = f.instrumental;
        s.instrumental_lora = f.engine == 0;
        s.cot               = COT_VALUES[f.cot];
        s.abc               = submit_abc;  // 1.0.88 : partition de l'onglet « Partition » (vide sinon)
        s.adapters          = chosen_loras();
        s.negative_style    = join_style(split_style(f.negative_style));
        if (v == 1) remember_negative_tags(s.negative_style);
        s.negative_scale    = f.negative_scale;
        if (f.fade_structure) s.fade_seconds = structure_fade_seconds(words, f.instrumental);
        s.artist            = trim(f.artist);
        s.album             = trim(f.album);
        s.track             = submit_track > 0 ? std::to_string(submit_track) : parse_title_block(f.title).track;  // album IA : n° de piste
        s.auto_duration     = f.auto_duration;
        if (!f.cover.empty()) {
            s.kind             = "cover";
            s.source_file      = narrow(f.cover.wstring());
            s.parent_title     = narrow(f.cover.filename().wstring());
            s.duration         = std::clamp(std::ceil(f.cover_seconds) + 15, 30.0, 360.0);
            s.expected_seconds = f.cover_seconds;
        } else if (f.auto_duration) {
            // Chanson : plafond maximal, le modèle termine à la fin des paroles.
            // Instrumental : il ne conclut pas seul, le plafond suit la structure (+ fondu si atteint).
            s.duration         = s.instrumental ? estimate : 360;
            s.expected_seconds = estimate;
        } else {
            s.duration         = f.duration;
            s.expected_seconds = std::min<double>(f.duration, estimate);
        }
        if (!long_rest.empty()) {
            s.long_rest     = long_rest;
            s.title        += tr(" (partie 1)");
            s.auto_duration = true;
            s.duration      = s.instrumental ? std::min(360.0, estimate) : 360;
        }
        s.seed = s.lm_seed = f.seed < 0 ? -1 : f.seed + v - 1;
        trial_ids.push_back(studio->submit(s, held));
    }
    if (trial_ids.size() > 1) trial_groups.push_back(trial_ids);  // 1.0.97 : meilleur essai
    const std::string quoted = trim(f.title);
    journal::event("file", "Morceau ajouté à la file : « " + quoted + " » (" + (f.instrumental ? "instrumental" : "chanson") + ", groupe « " +
                               trim(f.artist) + " », album « " + trim(f.album) + " », " + std::to_string(f.versions) + " version(s)" +
                               (held ? ", en pause" : "") + ") style : " + style.substr(0, 200));
    if (settings.save_group_style && !trim(f.artist).empty() && !style.empty()) auto_group_style(trim(f.artist), style);
    if (submit_quiet) return;  // album IA : un seul message pour tout l'album
    if (f.versions > 1) {
        notify(held ? trf("%d versions de « %s » ajoutées à la file, en pause.", f.versions, quoted.c_str())
                    : trf("%d versions de « %s » ajoutées à la file.", f.versions, quoted.c_str()));
    } else {
        notify(held ? trf("« %s » ajouté à la file, en pause.", quoted.c_str()) : trf("« %s » ajouté à la file.", quoted.c_str()));
    }
}

void App::reuse(const Song & s) {
    Form & f = form;
    f.cover.clear();
    f.cover_seconds = 0;
    set_mode(f, s.instrumental);
    f.title    = s.title;
    f.style    = s.style;
    f.artist   = s.artist;
    f.album    = s.album;
    f.language = detect_language(s.style);
    (s.instrumental ? f.lyrics_inst : f.lyrics_song) = s.lyrics;
    f.engine        = s.instrumental_lora ? 0 : 1;
    f.loras.clear();
    for (const auto & [name, scale] : s.adapters) f.loras.push_back({ name, (float) scale, true });
    scan_loras(true);
    sync_lora_triggers();
    f.fade_structure = s.fade_seconds > 0;
    f.negative_style = s.negative_style;
    f.negative_scale = (float) s.negative_scale;
    f.cot           = s.cot == "melody" ? 1 : s.cot == "off" ? 2 : 0;
    f.duration      = (int) std::min(360.0, s.duration);
    f.auto_duration = s.auto_duration;
    f.seed          = -1;
    if (s.kind == "cover" && fs::exists(widen(s.source_file))) {
        f.cover         = widen(s.source_file);
        f.cover_seconds = media_duration(f.cover);
    }
    f.dirty = true;
    ImGui::ClearActiveID();
    scroll_form_top = true;
}

// same_seed : on rejoue le même tirage de bruit acoustique (rendu identique à l'original, mais
// sans l'écrêtage des anciennes versions) ; sinon, nouvelle graine aléatoire = nouvelle variation.
void App::rerender(const Song & s, bool same_seed) {
    Song r;
    r.kind              = "rerender";
    r.parent            = s.id;
    r.parent_title      = s.title;
    r.title             = s.title + tr(" (re-rendu)");
    r.style             = s.style;
    r.lyrics            = s.lyrics;
    r.instrumental      = s.instrumental;
    r.instrumental_lora = s.instrumental_lora;
    r.adapters          = s.adapters;
    r.fade_seconds      = s.fade_seconds;
    r.negative_style    = s.negative_style;  // pour mémoire : un re-rendu ne recompose pas
    r.negative_scale    = s.negative_scale;
    r.cot               = s.cot;
    r.duration          = s.audio_seconds;
    r.artist            = s.artist;
    r.album             = s.album;
    r.track             = s.track;
    r.seed              = same_seed && s.has_used_seeds ? s.used_seed : -1;
    studio->submit(r);
    notify(same_seed ? trf("Re-rendu de « %s » (même graine) ajouté à la file.", s.title.c_str())
                     : trf("Re-rendu de « %s » (graine aléatoire) ajouté à la file.", s.title.c_str()));
}

// Nouvelle composition à partir du même style et des mêmes paroles (nouvelle graine) : la seule
// façon de corriger une composition défectueuse, qu'un re-rendu ne fait que resynthétiser.
void App::recompose(const Song & s) {
    Song n;
    std::string title = s.title;
    // Retire les suffixes ajoutés par les re-rendus et recompositions précédents (pas d'empilement).
    for (const std::string suffix : { std::string(tr(" (re-rendu)")), std::string(tr(" (recomposé)")) }) {
        while (title.size() > suffix.size() &&
               title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0) {
            title.resize(title.size() - suffix.size());
        }
    }
    n.title             = title + tr(" (recomposé)");
    n.style             = s.style;
    n.lyrics            = s.lyrics;
    n.instrumental      = s.instrumental;
    n.instrumental_lora = s.instrumental_lora;
    n.adapters          = s.adapters;
    n.fade_seconds      = s.fade_seconds;
    n.negative_style    = s.negative_style;  // 1.0.46 : la recomposition garde le style à éviter (oubli)
    n.negative_scale    = s.negative_scale;
    n.cot               = s.cot;
    n.artist            = s.artist;
    n.album             = s.album;
    n.track             = s.track;
    n.seed = n.lm_seed  = -1;
    double estimate     = estimate_seconds(s.lyrics, s.instrumental);
    bool   automatic    = s.kind == "rerender" || s.auto_duration;
    n.auto_duration     = automatic;
    if (s.kind == "cover") {
        n.kind             = "cover";
        n.source_file      = s.source_file;
        n.parent_title     = s.parent_title;
        n.duration         = s.duration;
        n.expected_seconds = s.expected_seconds;
    } else if (automatic) {
        n.duration         = s.instrumental ? estimate : 360;
        n.expected_seconds = estimate;
    } else {
        n.duration         = s.duration;
        n.expected_seconds = std::min(s.duration, estimate);
    }
    studio->submit(n);
    notify(trf("Recomposition de « %s » ajoutée à la file.", title.c_str()));
}

// ------------------------------------------------------------------ Formulaire : affichage
void App::draw_form_toolbar() {
    ImGui::PushFont(g_header);
    ImGui::TextUnformatted(tr("Nouveau morceau"));
    ImGui::PopFont();
    const char * labels[] = { tr("Adapter au format YuE2"), tr("Tout vider") };
    float        w        = 0;
    for (const char * l : labels) w += ImGui::CalcTextSize(l).x + ImGui::GetStyle().FramePadding.x * 2;
    w += ImGui::GetStyle().ItemSpacing.x;
    {   // boutons à droite du titre ; sous le titre quand la place manque (langues à textes longs)
        float title_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        float want_x    = ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize;
        if (want_x >= title_end + 12 * g_dpi) ImGui::SameLine(want_x);
    }
    if (soft_button(labels[0], P.teal)) {
        adapt(form.title, form.style, form.instrumental ? form.lyrics_inst : form.lyrics_song, form.instrumental);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Convertit le style et les paroles déjà collés (format Suno) en format YuE2."));
    ImGui::SameLine();
    if (danger_button(labels[1])) clear_form();
}

const char * App::tempo_name(int bpm) {
    return bpm < 76 ? "Largo" : bpm < 108 ? "Adagio / Andante" : bpm < 120 ? "Moderato" : bpm < 156 ? "Allegro" : "Vivace / Presto";
}

fs::path App::app_dir() const {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

fs::path App::translate_dir() const { return studio->paths().models_dir / "traduction"; }

// Recherche du moteur local (llama.cpp + modèle), au plus toutes les 5 secondes.
void App::refresh_translator(bool force) {
    if (!force && ImGui::GetTime() - local_scan_at < 5) return;
    local_translator = find_local_translator(app_dir(), studio->paths().models_dir);
    local_scan_at    = ImGui::GetTime();
}

// Le service Internet répond-il ? Test en tâche de fond (quelques secondes au plus hors ligne).
void App::test_translate_web() {
    if (*translate_web_state == 1) return;
    *translate_web_state = 1;
    auto state           = translate_web_state;
    std::thread([state] { *state = translate_web_available() ? 2 : 3; }).detach();
}

bool App::web_translation_possible() const { return settings.translate_web && *translate_web_state != 3; }

// Langue proposée : par Internet (toutes), ou hors ligne si le modèle local la connaît.
bool App::translate_lang_usable(const TranslateLang & l) const {
    return web_translation_possible() || (local_translator.ready() && l.llm);
}

void App::start_translation(const std::string & lyrics) {
    auto job      = std::make_shared<TranslateJob>();
    job->target   = settings.translate_target;
    job->used_web = web_translation_possible();
    translate_job = job;
    LocalTranslator local = local_translator;
    std::thread([job, lyrics, local] {
        job->result = translate_lyrics(lyrics, job->target, job->used_web, local, &job->progress, &job->cancel);
        job->state  = 1;
    }).detach();
}

// Fin de la traduction : les paroles sont remplacées (structure intacte) et la langue du style suit.
void App::poll_translation() {
    if (!translate_job || translate_job->state != 1) return;
    auto job = translate_job;
    translate_job.reset();
    const TranslateResult & r = job->result;
    if (job->used_web && r.engine == "local") *translate_web_state = 3;  // Internet n'a pas répondu
    if (!r.ok) {
        notify(trf("Traduction impossible : %s", tr(r.error.c_str())));
        return;
    }
    Form & f         = form;
    translate_undo   = f.lyrics_song;
    f.lyrics_song    = r.text;
    translate_last   = r.text;
    translate_engine = r.engine;
    f.cursor         = -1;
    f.dirty          = true;
    ImGui::ClearActiveID();
    const TranslateLang * lang = find_translate_lang(job->target);
    std::string           code = job->target == "zh-CN" ? "zh" : job->target;
    int                   li   = language_index_for_code(code);
    if (lang && lang->yue2 && li > 0) {
        f.language = li;
        f.style    = set_language(f.style, LANGUAGES[li].tag);  // balise de langue du style
    }
    const char * engine = r.engine == "web" ? tr("Internet") : tr("modèle local");
    notify(trf("Paroles traduites en %s (%s).", lang ? lang->name : job->target.c_str(), engine));
    if (r.kept > 0) notify(trf("%d ligne(s) non traduite(s) par le modèle local : relisez les paroles.", r.kept));
    if (lang && !lang->yue2) notify(tr("Attention : YuE2 ne chante pas cette langue, le résultat sera incertain."));
}

// Ligne « Traduire vers [langue] [Traduire] » du bloc Paroles.
void App::draw_translate_row(std::string & lyrics) {
    draw_translate_row_inner(lyrics);
}

void App::draw_translate_row_inner(std::string & lyrics) {
    Form & f = form;
    refresh_translator();
    if (settings.translate_web && *translate_web_state == 0) test_translate_web();
    const auto &          langs = translate_languages();
    const TranslateLang * cur   = find_translate_lang(settings.translate_target);
    if (!cur) cur = &langs[0];

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Traduire vers"));
    ImGui::SameLine(0, 8 * g_dpi);
    ImGui::SetNextItemWidth(220 * g_dpi);
    if (ImGui::BeginCombo("##tr_lang", cur->name, ImGuiComboFlags_HeightLarge)) {
        ImGui::SeparatorText(tr("Chantées par YuE2"));
        for (const TranslateLang & l : langs) {
            if (!l.yue2) continue;
            ImGui::BeginDisabled(!translate_lang_usable(l));
            if (ImGui::Selectable(l.name, cur == &l)) {
                settings.translate_target = l.code;
                save_settings(settings, settings_path);
            }
            ImGui::EndDisabled();
        }
        ImGui::SeparatorText(tr("Autres langues (non chantées par YuE2)"));
        for (const TranslateLang & l : langs) {
            // La liste s'adapte au moteur : sans Internet, seules les langues du modèle local restent.
            if (l.yue2 || !translate_lang_usable(l)) continue;
            if (ImGui::Selectable(l.name, cur == &l)) {
                settings.translate_target = l.code;
                save_settings(settings, settings_path);
            }
        }
        if (!web_translation_possible()) {
            ImGui::Separator();
            colored_wrapped(P.muted, tr("Sans Internet : seules les langues du modèle local sont proposées."));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    bool        running = translate_job != nullptr;
    std::string why     = !has_lyrics_text(lyrics)       ? tr("Aucune parole à traduire.")
                        : !translate_lang_usable(*cur)   ? tr("Pas de connexion à Internet et pas de modèle de traduction local (Réglages › Analyse et paroles).")
                                                         : "";
    ImGui::BeginDisabled(running || !why.empty());
    if (accent_button(tr("Traduire"), ImVec2(0, 0), &P.teal)) start_translation(lyrics);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", !why.empty() ? why.c_str()
                                             : tr("Traduit les lignes chantées ; les balises de section ([Verse], [Chorus]…) et les lignes vides ne bougent pas.\nPar Internet (Google Traduction) si possible, sinon avec le modèle local."));
    }
    ImGui::SameLine();
    if (running) {
        progress_bar(translate_job->progress, ImVec2(140 * g_dpi, ImGui::GetFrameHeight()), "");
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Arrêter"))) translate_job->cancel = true;
    } else if (!translate_undo.empty() && lyrics == translate_last) {
        if (ImGui::SmallButton(tr("Annuler la traduction"))) {
            lyrics = translate_undo;
            translate_undo.clear();
            f.dirty = true;
            ImGui::ClearActiveID();
        }
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", translate_engine == "web" ? tr("traduit par Internet") : tr("traduit par le modèle local"));
    } else {
        int  web   = *translate_web_state;
        bool local = local_translator.ready();
        std::string state = !settings.translate_web ? (local ? tr("hors ligne : modèle local") : tr("Internet désactivé, pas de modèle local"))
                          : web == 3            ? (local ? tr("hors ligne : modèle local") : tr("hors ligne"))
                          : web == 2            ? tr("● Internet")
                                                : tr("test de la connexion…");
        ImGui::TextColored(P.muted, "%s", state.c_str());
    }
}

// Réglages › Analyse et paroles : moteurs de traduction.
void App::draw_translate_settings() {
    draw_translate_settings_inner();
}

void App::draw_translate_settings_inner() {
    refresh_translator();
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.violet, "%s", tr("Traduction des paroles"));
    ImGui::PopFont();
    muted_wrapped(tr("Le bouton « Traduire » du bloc Paroles traduit les lignes chantées dans une autre langue sans toucher aux balises de section. Par Internet : Google Traduction (gratuit, plus de 100 langues ; le texte des paroles est envoyé à Google). Hors ligne : modèle local Qwen2.5-3B (environ 2 Go, exécuté par le processeur pour laisser la carte libre, une vingtaine de langues, 20 s à 1 min par morceau)."));
    if (ImGui::Checkbox(tr("Utiliser Internet (Google Traduction)"), &settings.translate_web)) {
        save_settings(settings, settings_path);
        if (settings.translate_web) test_translate_web();
    }
    // État d'Internet
    ImGui::AlignTextToFramePadding();
    int web = *translate_web_state;
    ImGui::TextColored(P.muted, "%s", tr("Internet :"));
    ImGui::SameLine();
    if (web == 2) ImGui::TextColored(P.ok, "%s", tr("● disponible"));
    else if (web == 3) ImGui::TextColored(P.warn, "%s", tr("● injoignable"));
    else if (web == 1) ImGui::TextColored(P.muted, "%s", tr("test…"));
    else ImGui::TextColored(P.muted, "%s", tr("non testé"));
    ImGui::SameLine(0, 12 * g_dpi);
    ImGui::BeginDisabled(web == 1);
    if (ImGui::SmallButton(tr("Tester"))) test_translate_web();
    ImGui::EndDisabled();
    // Moteur local : programme llama.cpp
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Moteur local (llama.cpp) :"));
    ImGui::SameLine();
    if (!local_translator.exe.empty()) ImGui::TextColored(P.ok, "%s", tr("● installé"));
    else ImGui::TextColored(P.warn, "%s", tr("absent (dossier « outils\\llama » de l'application)"));
    // Modèle local
    const TranslateModelFile & mf  = translate_model_file();
    auto                       job = downloader.find("translate");
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Modèle local :"));
    ImGui::SameLine();
    if (job && job->state == 0) {
        unsigned long long dd = job->done, tt = std::max<unsigned long long>(1, job->total);
        progress_bar((float) ((double) dd / (double) tt), ImVec2(200 * g_dpi, 0));
        ImGui::SameLine();
        if (ImGui::SmallButton("✕##tr_cancel")) job->cancel = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Annuler le téléchargement"));
    } else if (!local_translator.model.empty()) {
        std::error_code ec;
        ImGui::TextColored(P.ok, "%s", tr("● installé"));
        ImGui::SameLine(0, 8 * g_dpi);
        ImGui::TextColored(P.muted, "%s", fmt_mb(fs::file_size(local_translator.model, ec)).c_str());
        ImGui::SameLine(0, 12 * g_dpi);
        if (delete_button(tr("Supprimer le modèle de traduction du disque"))) pending_translate_delete = true;
    } else {
        if (accent_button(tr("Télécharger le modèle de traduction"), ImVec2(0, 0), &P.violet)) {
            std::vector<DlFile> files = { { mf.url, mf.name, mf.bytes } };
            std::error_code     ec;
            fs::create_directories(translate_dir(), ec);
            auto j = downloader.start("translate", tr("Traduction des paroles"), translate_dir(), files);
            if (std::find(tracked_downloads.begin(), tracked_downloads.end(), j) == tracked_downloads.end()) tracked_downloads.push_back(j);
            announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), j.get()), announced_downloads.end());
            notify(tr("Téléchargement du modèle de traduction lancé."));
            local_scan_at = -100;
        }
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", trf("environ %s", fmt_mb(mf.bytes).c_str()).c_str());
        if (job && job->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", job->error.c_str()).c_str());
    }
    muted_wrapped(tr("Modèle Qwen2.5-3B-Instruct (Alibaba Cloud), licence Qwen Research : usage personnel et non commercial."));
}

bool App::has_negative_tag(const std::string & tag) const {
    for (const std::string & t : settings.negative_tags) if (lower(t) == lower(tag)) return true;
    return false;
}

// Ajoute aux balises rapides les termes du champ qui n'y sont pas encore (casse ignorée), puis enregistre.
void App::remember_negative_tags(const std::string & field) {
    bool added = false;
    for (const std::string & t : split_style(field)) {
        std::string tag = trim(t);
        if (tag.empty() || tag.size() > 48 || has_negative_tag(tag)) continue;
        settings.negative_tags.push_back(tag);
        added = true;
    }
    if (added) save_settings(settings, settings_path);
}

// Balises rapides négatives, chacune suivie d'une croix (suppression après confirmation).
// field : champ du formulaire (un clic sur la balise l'ajoute ou la retire) ; nullptr dans les Réglages.
// owner : 0 formulaire, 1 Réglages (la fenêtre de confirmation s'ouvre là où l'on a cliqué).
bool App::draw_negative_chips(std::string * field, int owner) {
    std::vector<std::string> tags  = field ? split_tags(*field) : std::vector<std::string>();
    ImGuiStyle &             st    = ImGui::GetStyle();
    float                    avail = ImGui::GetContentRegionAvail().x, x = 0;
    float                    cross = ImGui::CalcTextSize("×").x + st.FramePadding.x * 2;
    bool                     changed = false;
    const ImVec4 &           color   = P.warn;
    if (settings.negative_tags.empty()) colored_wrapped(P.muted, tr("Aucune balise rapide : tapez un terme dans le champ pour l'ajouter."));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    for (size_t i = 0; i < settings.negative_tags.size(); i++) {
        const std::string & tag = settings.negative_tags[i];
        auto        it   = std::find_if(tags.begin(), tags.end(), [&](const std::string & t) { return lower(t) == lower(tag); });
        bool        on   = field && it != tags.end();
        std::string text = (on ? "✓ " : "") + tag;
        float       w    = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2 + 1 * g_dpi + cross;
        if (i > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
            ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
            x += st.ItemSpacing.x * 0.6f + w;
        } else {
            x = w;
        }
        ImGui::PushID((int) i);
        ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, on ? 0.55f : 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, on ? 0.65f : 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? P.accent_ink : color);
        if (ImGui::SmallButton(text.c_str()) && field) {
            if (on) tags.erase(it);
            else tags.push_back(tag);
            changed = true;
        }
        ImGui::SameLine(0, 1 * g_dpi);
        if (ImGui::SmallButton("×")) {
            neg_pending_delete = tag;
            neg_delete_owner   = owner;
        }
        bool cross_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleColor(4);
        // Bulle après PopStyleColor : sinon elle reprend la couleur de texte de la balise (illisible).
        if (cross_hovered) ImGui::SetTooltip("%s", tr("Supprimer cette balise rapide"));
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    if (changed) *field = join_style(tags);

    // Confirmation, pour éviter une suppression par erreur de clic.
    const char * title = tr("Supprimer la balise rapide ?");
    if (!neg_pending_delete.empty() && neg_delete_owner == owner && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", trf("La balise « %s » sera retirée des balises rapides (le champ « Style à éviter » n'est pas modifié).",
                                     neg_pending_delete.c_str()).c_str());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            neg_pending_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            auto & list = settings.negative_tags;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string & t) { return lower(t) == lower(neg_pending_delete); }), list.end());
            save_settings(settings, settings_path);
            neg_pending_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return changed;
}

// Réglages › Génération : gestion des balises rapides négatives.
void App::draw_negative_tags_settings() {
    draw_negative_tags_settings_inner();
}

void App::draw_negative_tags_settings_inner() {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.warn, "%s", tr("Balises rapides « Style à éviter »"));
    ImGui::PopFont();
    muted_wrapped(tr("Balises proposées sous le champ « Style à éviter » du formulaire. Un terme tapé dans ce champ est ajouté ici automatiquement ; la croix d'une balise la supprime (après confirmation)."));
    draw_negative_chips(nullptr, 1);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    float add_w = ImGui::CalcTextSize(tr("Ajouter")).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::min(320 * g_dpi, ImGui::GetContentRegionAvail().x - add_w));
    bool enter = tx::InputTextWithHint("##neg_new", tr("Nouvelle balise (ex. : brass section)"), &neg_new_tag, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(trim(neg_new_tag).empty());
    if ((ImGui::Button(tr("Ajouter")) || enter) && !trim(neg_new_tag).empty()) {
        remember_negative_tags(neg_new_tag);  // accepte aussi plusieurs termes séparés par des virgules
        neg_new_tag.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Rétablir la liste par défaut"))) {
        settings.negative_tags = default_negative_tags();
        save_settings(settings, settings_path);
    }
}

// Balises négatives (expérimental) : style à éviter, avec quelques suggestions et la force du guidage.
// Le moteur yue2.cpp (modifié) compose en parallèle une version guidée par ces balises et s'en éloigne.
void App::draw_negative_style() {
    draw_negative_style_inner();
}

void App::draw_negative_style_inner() {
    Form & f = form;
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Style à éviter"));
    ImGui::SameLine(0, 8 * g_dpi);
    ImGui::TextColored(P.amber, "%s", tr("expérimental"));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Balises que le morceau doit éviter (instruments, effets de voix…), séparées par des virgules.\nYuE2 n'a pas été entraîné pour cela : l'effet est à juger à l'oreille.\nQuand ce champ est rempli, la composition est un peu plus lente (environ ×1,3) et demande environ 2,7 Go de mémoire vidéo en plus."));
    }
    // Champ d'une ligne (comme input_line), mais on repère la fin de saisie pour mémoriser les nouveaux termes.
    float clear_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 0.5f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear_w);
    if (ImGui::InputTextWithHint("##neg_style", tr("Ex. : vocoder, autotune, synthesizer"), &f.negative_style)) f.dirty = true;
    if (ImGui::IsItemDeactivatedAfterEdit()) remember_negative_tags(f.negative_style);
    if (text_context_menu(f.negative_style)) f.dirty = true;
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    f.dirty |= clear_button("##neg_style", f.negative_style);
    f.dirty |= draw_negative_chips(&f.negative_style, 0);
    if (!trim(f.negative_style).empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Force"));
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::SetNextItemWidth(std::min(260 * g_dpi, ImGui::GetContentRegionAvail().x));
        f.dirty |= ImGui::SliderFloat("##neg_scale", &f.negative_scale, 1.2f, 3.0f, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("1,5 conseillé. Plus fort, le morceau s'éloigne davantage du style à éviter, mais peut perdre en naturel."));
        }
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::TextColored(P.muted, "%s", tr("composition ≈ ×1,3, +2,7 Go de mémoire vidéo"));
    }
}

bool App::draw_tempo_slider(std::string & style) {
    std::vector<std::string> tags = split_tags(style);
    int                      bpm  = 0;
    for (const std::string & t : tags) {
        if (is_bpm_tag(t)) {
            bpm = atoi(t.c_str());
            break;
        }
    }
    const bool unset = bpm <= 0;
    int        value = unset ? 120 : std::clamp(bpm, TEMPO_MIN, TEMPO_MAX);
    bool       changed = false;

    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Tempo"));
    ImGui::SameLine(0, 10 * g_dpi);
    ImGui::PushFont(g_bold);
    if (unset) ImGui::TextColored(P.muted, "%s", tr("non défini"));
    else ImGui::TextColored(P.accent, "%d BPM", bpm);
    ImGui::PopFont();
    if (!unset) {
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::TextColored(P.muted, "%s", tempo_name(value));
        ImGui::SameLine(0, 10 * g_dpi);
        if (ImGui::SmallButton(tr("Retirer le tempo"))) {
            tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
            style   = join_style(tags);
            changed = true;
        }
    }
    // barre des mouvements, alignée sur la piste du curseur
    float  pad = 8 * g_dpi, x0 = ImGui::GetCursorScreenPos().x + pad, x1 = x0 + ImGui::GetContentRegionAvail().x - 2 * pad;
    float  y = ImGui::GetCursorScreenPos().y + 2 * g_dpi, h = 6 * g_dpi;
    struct Zone { int from; const ImVec4 * color; const char * label; };
    const Zone zones[] = { { 60, &P.blue, "Largo" }, { 76, &P.teal, "Andante" }, { 108, &P.green, "Moderato" },
                           { 120, &P.amber, "Allegro" }, { 156, &P.orange, "Presto" } };
    ImDrawList * dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 5; i++) {
        int   to  = i < 4 ? zones[i + 1].from : TEMPO_MAX;
        float a   = x0 + (x1 - x0) * (zones[i].from - TEMPO_MIN) / (float) (TEMPO_MAX - TEMPO_MIN);
        float b   = x0 + (x1 - x0) * (to - TEMPO_MIN) / (float) (TEMPO_MAX - TEMPO_MIN);
        bool  cur = !unset && value >= zones[i].from && value <= to;
        dl->AddRectFilled(ImVec2(a + (i ? 1 : 0), y), ImVec2(b - 1, y + h), ImGui::GetColorU32(alpha(*zones[i].color, cur ? 0.95f : 0.45f)), 3 * g_dpi);
    }
    ImGui::Dummy(ImVec2(0, h + 4 * g_dpi));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, unset ? P.muted : P.accent);
    int edited = value;
    if (ImGui::SliderInt("##tempo", &edited, TEMPO_MIN, TEMPO_MAX, "")) {
        tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
        tags.push_back(std::to_string(edited) + " BPM");
        style   = join_style(tags);
        changed = true;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Tempo du morceau, de 60 à 260 BPM par pas de 1 : ajoute la balise « BPM » au style. Ctrl+clic pour saisir un nombre."));
    }
    // repères sous le curseur
    const char * marks[] = { "60", "110", "160", "210", "260" };
    float        ly      = ImGui::GetCursorScreenPos().y;
    for (int i = 0; i < 5; i++) {
        float w = ImGui::CalcTextSize(marks[i]).x;
        float x = x0 + (x1 - x0) * i / 4.f - (i == 0 ? 0 : i == 4 ? w : w / 2);
        dl->AddText(ImVec2(x, ly), ImGui::GetColorU32(P.muted), marks[i]);
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 2 * g_dpi));
    return changed;
}

void App::draw_song_section() {
    Form & f = form;
    std::string summary_song = (trim(f.title).empty() ? std::string(tr("(sans titre)")) : trim(f.title)) +
                               (f.instrumental ? tr("  ·  instrumental") : tr("  ·  chanson"));
    if (!begin_section("sec_morceau", tr("Morceau"), P.blue, nullptr, ellipsize(summary_song, 520 * g_dpi))) {
        end_section();
        return;
    }
    f.dirty |= input_line("##title", tr("Nom du morceau (ex. : 01. City Lights)"), f.title);
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    f.dirty |= input_line("##artist", tr("Groupe ou chanteur"), f.artist, half);
    ImGui::SameLine();
    f.dirty |= input_line("##album", tr("Album"), f.album);
    draw_group_style_checkbox();

    // Type et reprise sur une ligne
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, tr("Type"));
    for (int i = 0; i < 2; i++) {
        ImGui::SameLine(0, i ? 4 * g_dpi : 10 * g_dpi);
        bool         selected = f.instrumental == (i == 1);
        const char * text     = i ? tr("Instrumental") : tr("Chanson");
        if (selected ? accent_button(text, ImVec2(0, 0), &P.blue) : ImGui::Button(text)) {
            if (!selected) set_mode(f, i == 1);
        }
    }
    ImGui::SameLine(0, 18 * g_dpi);
    ImGui::TextColored(P.muted, tr("Reprise"));
    ImGui::SameLine();
    if (!studio->can_transcribe()) {
        ImGui::TextColored(P.muted, tr("modèles de transcription absents"));
    } else if (f.cover.empty()) {
        if (ImGui::Button(tr("Choisir un fichier audio…"))) pick_cover();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Facultatif : réinterpréter un morceau existant dans le style choisi."));
    } else {
        std::string name = narrow(f.cover.filename().wstring()) + "  (" + fmt_time(f.cover_seconds) + ")";
        pill(name.c_str(), P.blue);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Retirer"))) {
            f.cover.clear();
            f.cover_seconds = 0;
            f.cot           = COT_DEFAULT;
            if (analysis) analysis->cancel = true;
            analysis.reset();
        }
    }
    if (!f.cover.empty()) {
        muted_wrapped(f.instrumental ? tr("La partition (mélodie et accords) sera transcrite puis rejouée dans le style choisi.")
                                     : tr("La mélodie sera transcrite puis chantée dans le style choisi : écrivez les paroles à chanter."));
        draw_analysis_panel();
    }
    end_section();
}

void App::draw_style_section() {
    Form & f = form;
    if (!begin_section("sec_style", tr("Style"), P.violet, tr("balises en anglais, séparées par des virgules"),
                       ellipsize(trim(f.style).empty() ? std::string(tr("(vide)")) : f.style, 520 * g_dpi))) {
        end_section();
        return;
    }
    {   // présentation du bloc : classique, pastilles, constructeur
        static const char * modes[] = { N_("Classique"), N_("Pastilles"), N_("Constructeur") };
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, tr("Présentation"));
        for (int i = 0; i < 3; i++) {
            ImGui::SameLine(0, 6 * g_dpi);
            ImGui::PushID(i);
            bool chosen = settings.style_mode == i;
            if (chosen ? accent_button(tr(modes[i]), ImVec2(0, 0), &P.violet) : ImGui::SmallButton(tr(modes[i]))) {
                settings.style_mode = i;
                save_settings(settings, settings_path);
            }
            ImGui::PopID();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(tr("Classique : onglets de balises.\nPastilles : vos balises en pastilles retirables, catégories repliables.\nConstructeur : une liste par catégorie (genres, ambiance, voix, instruments, tempo)."));
        }
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    }

    // Langue · profil d'influence · préréglage
    float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    ImGui::SetNextItemWidth(third);
    std::string lang = f.language ? trf("Langue : %s", tr(LANGUAGES[f.language].label)) : std::string(tr(LANGUAGES[0].label));
    bool lang_open = ImGui::BeginCombo("##language", lang.c_str());
    if (lang_open) {
        for (int i = 0; i < (int) LANGUAGES.size(); i++) {
            if (ImGui::Selectable(tr(LANGUAGES[i].label), f.language == i)) {
                f.language        = i;
                f.style           = set_language(f.style, LANGUAGES[i].tag);
                settings.language = i;
                save_settings(settings, settings_path);
                f.dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("Langue placée en tête du style. YuE2 chante surtout l'anglais :\nles autres langues (français, espagnol, italien, allemand, portugais, japonais, coréen,\nmandarin, cantonais) sont possibles mais moins naturelles."));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(third);
    bool influence_open = ImGui::BeginCombo("##influence", tr("Profil d'influence…"), ImGuiComboFlags_HeightLarge);
    if (influence_open) {
        for (const Influence & inf : INFLUENCES) {
            if (ImGui::Selectable(inf.name)) {
                f.style = set_language(influence_style(inf), LANGUAGES[f.language].tag);
                f.dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", influence_style(inf).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const Preset * current = nullptr;
    for (const Preset & p : settings.presets) {
        if (!f.style.empty() && p.style == f.style) current = &p;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string preview = current ? trf("Préréglage : %s", current->name.c_str())
                        : settings.presets.empty() ? tr("Aucun préréglage") : tr("Préréglages…");
    bool presets_open = ImGui::BeginCombo("##presets", preview.c_str(), ImGuiComboFlags_HeightLarge);
    if (presets_open) {
        std::vector<std::string> groups = pack_groups(true);
        for (const std::string & group : groups) {
            // Titre de groupe seulement quand des packs sont installés (sinon la liste reste comme avant)
            if (!group.empty()) ImGui::SeparatorText(group.c_str());
            else if (groups.size() > 1) ImGui::SeparatorText(tr("Mes styles"));
            for (size_t i = 0; i < settings.presets.size(); i++) {
                const Preset & p = settings.presets[i];
                if (p.pack != group) continue;
                std::string entry = p.name + (p.loras.empty() ? "" : tr("  · LoRA")) + (p.structure_song.empty() && p.structure_inst.empty() ? "" : tr("  · structure")) +
                                    (p.has_negative && !p.negative_style.empty() ? tr("  · à éviter") : "");
                ImGui::PushID((int) i);
                if (ImGui::Selectable(entry.c_str(), current == &p)) apply_preset(p);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset_summary(p).c_str());
                ImGui::PopID();
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled(trim(f.style).empty());
        if (ImGui::Selectable(tr("Enregistrer le style actuel…"))) {
            preset_name = current ? current->name : "";
            open_preset = true;
        }
        ImGui::EndDisabled();
        if (current && ImGui::Selectable(trf("Supprimer « %s »", current->name.c_str()).c_str())) {
            std::string name = current->name;
            settings.presets.erase(settings.presets.begin() + (current - settings.presets.data()));
            save_settings(settings, settings_path);
            notify(trf("Préréglage « %s » supprimé.", name.c_str()));
        }
        ImGui::EndCombo();
    }

    // Champ de style sur plusieurs lignes, avec ascenseur. Retour à la ligne « doux » : l'espace qui suit
    // une virgule devient un saut de ligne à l'affichage (même longueur, le curseur ne bouge pas).
    float   clear_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 0.5f;
    float   field_w = ImGui::GetContentRegionAvail().x - clear_w;
    ImGuiID style_id = ImGui::GetID("##style");
    if (ImGui::GetActiveID() != style_id) {
        style_view = wrap_style(f.style, field_w - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ScrollbarSize);
    }
    bool style_changed = ImGui::InputTextMultiline(
        "##style", &style_view,
        ImVec2(field_w, ImGui::GetTextLineHeight() * 3.6f + ImGui::GetStyle().FramePadding.y * 2));
    style_changed |= text_context_menu(style_view);
    if (style_changed) {
        f.style = style_view;
        std::replace(f.style.begin(), f.style.end(), '\n', ' ');
        f.dirty = true;
    }
    if (f.style.empty()) {
        // indice affiché dans le champ vide
        ImVec2 min = ImGui::GetItemRectMin() + ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(min, ImGui::GetColorU32(P.muted),
                                            tr("Ex. : French, grunge, 90s, melancholic, 110 BPM, distorted guitar, raspy male vocal"));
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    f.dirty |= clear_button("##style_clear", f.style);
    f.dirty |= draw_tempo_slider(f.style);
    draw_negative_style();

    // Balises par catégorie, chacune à sa couleur
    auto group_of = [&](const std::string & tag) -> const ChipGroup * {
        for (const ChipGroup & g : STYLE_GROUPS) {
            for (const char * item : group_items(g)) if (lower(item) == lower(tag)) return &g;
        }
        return nullptr;
    };
    if (settings.style_mode == 1) {
        // Pastilles : les balises du style, retirables ; puis les catégories repliables
        std::vector<std::string> tags = split_tags(f.style);
        if (!tags.empty()) {
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            ImGuiStyle & st    = ImGui::GetStyle();
            float        avail = ImGui::GetContentRegionAvail().x, x = 0;
            int          remove = -1;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
            for (size_t i = 0; i < tags.size(); i++) {
                const ChipGroup * g     = group_of(tags[i]);
                ImVec4            color = g ? category_color(g->color) : P.muted;
                std::string       text  = tags[i] + "  ✕";
                float             w     = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
                if (i > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
                    ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
                    x += st.ItemSpacing.x * 0.6f + w;
                } else {
                    x = w;
                }
                ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, 0.30f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.65f));
                ImGui::PushStyleColor(ImGuiCol_Text, P.text);
                ImGui::PushID((int) i);
                if (ImGui::SmallButton(text.c_str())) remove = (int) i;
                ImGui::PopID();
                ImGui::PopStyleColor(4);
            }
            ImGui::PopStyleVar(2);
            if (remove >= 0) {
                tags.erase(tags.begin() + remove);
                f.style = join_style(tags);
                f.dirty = true;
            }
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        }
        bool first = true;
        for (const ChipGroup & group : STYLE_GROUPS) {
            ImVec4 color = category_color(group.color);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            bool open = ImGui::CollapsingHeader(tr(group.name), first ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ImGui::PopStyleColor();
            first = false;
            if (open) {
                f.dirty |= style_chips(group_items(group), f.style, color);
                ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            }
        }
    } else if (settings.style_mode == 2) {
        // Constructeur : une liste par catégorie, qui coche et décoche les balises du style
        if (ImGui::BeginTable("##builder", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (const ChipGroup & group : STYLE_GROUPS) {
                ImGui::TableNextColumn();
                ImVec4                   color = category_color(group.color);
                std::vector<std::string> tags  = split_tags(f.style), chosen;
                for (const std::string & t : tags) {
                    for (const char * item : group_items(group)) {
                        if (lower(item) == lower(t)) chosen.push_back(t);
                    }
                }
                std::string label = tr(group.name);
                label += " : ";
                if (chosen.empty()) label += "—";
                for (size_t i = 0; i < chosen.size() && i < 3; i++) label += (i ? ", " : "") + chosen[i];
                if (chosen.size() > 3) label += " +" + std::to_string(chosen.size() - 3);
                label += "  ▾";
                ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, chosen.empty() ? 0.10f : 0.25f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.38f));
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::PushID(group.name);
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##choices");
                ImGui::SetNextWindowSizeConstraints(ImVec2(420 * g_dpi, 0), ImVec2(520 * g_dpi, 420 * g_dpi));
                if (ImGui::BeginPopup("##choices")) {
                    ImGui::TextColored(color, "%s", tr(group.name));
                    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
                    f.dirty |= style_chips(group_items(group), f.style, color);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                ImGui::PopStyleColor(3);
            }
            ImGui::EndTable();
        }
    } else if (ImGui::BeginTabBar("style_groups")) {
        for (const ChipGroup & group : STYLE_GROUPS) {
            ImVec4 color = category_color(group.color);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushStyleColor(ImGuiCol_TabSelected, alpha(color, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, alpha(color, 0.16f));
            ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, color);
            bool open = ImGui::BeginTabItem(tr(group.name));
            ImGui::PopStyleColor(4);
            if (open) {
                ImGui::Dummy(ImVec2(0, 1 * g_dpi));
                f.dirty |= style_chips(group_items(group), f.style, color);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    end_section();
}

void App::draw_lyrics_section() {
    Form &        f      = form;
    std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
    std::string tags_csv = structure_tags(lyrics);
    size_t      sections = tags_csv.empty() ? 0 : std::count(tags_csv.begin(), tags_csv.end(), ',') + 1;
    std::string summary_lyrics = trf("%d section(s)", (int) sections) +
                                 (f.instrumental || !has_lyrics_text(lyrics) ? "" : tr("  ·  paroles écrites"));
    if (!begin_section("sec_paroles", f.instrumental ? tr("Structure") : tr("Paroles"), P.teal,
                       f.instrumental ? tr("sections uniquement, sans paroles") : tr("une balise par section"), summary_lyrics)) {
        end_section();
        return;
    }
    float clear_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 0.5f;
    float combo_w = ImGui::GetContentRegionAvail().x - clear_w;
    ImGui::SetNextItemWidth(combo_w);
    // Liste de la largeur du champ : sans cette limite, elle s'élargissait à tout l'écran (croix placée en bout de ligne)
    ImGui::SetNextWindowSizeConstraints(ImVec2(combo_w, 0), ImVec2(combo_w, FLT_MAX));
    if (ImGui::BeginCombo("##structure", tr("Appliquer une structure type…"), ImGuiComboFlags_HeightLarge)) {
        // Applique un texte de structure ; si des paroles écrites seraient perdues, on demande confirmation.
        auto apply = [&](const std::string & text, const std::string & name) {
            if (f.instrumental || !has_lyrics_text(lyrics)) {
                lyrics  = text;
                f.dirty = true;
            } else {
                f.pending_structure = true;
                f.pending_text      = text;
                f.pending_name      = name;
            }
        };
        for (int i = 0; i < IM_ARRAYSIZE(STRUCTURES); i++) {
            if (ImGui::Selectable(tr(STRUCTURES[i].name))) apply(structure_text(STRUCTURES[i], f.instrumental), tr(STRUCTURES[i].name));
        }
        // Structures personnelles (« Mes structures »), puis celles de chaque pack importé
        for (const std::string & group : pack_groups(false)) {
            ImGui::SeparatorText(group.empty() ? tr("Mes structures") : group.c_str());
            for (size_t i = 0; i < settings.structures.size(); i++) {
                const UserStructure & st = settings.structures[i];
                if (st.pack != group) continue;
                ImGui::PushID((int) i);
                float x_w = ImGui::GetFrameHeight();
                if (ImGui::Selectable(st.name.c_str(), false, ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(ImGui::GetContentRegionAvail().x - x_w, 0))) {
                    apply(skeleton_text(st.sections, f.instrumental), st.name);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", arrow_text(st.sections).c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("×")) {  // suppression, après confirmation
                    pending_structure_delete = (int) i;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Supprimer cette structure"));
                ImGui::PopID();
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled(tags_csv.empty());
        if (ImGui::Selectable(tr("Enregistrer la structure actuelle…"))) {
            structure_to_save = tags_csv;
            structure_name.clear();
            open_structure = true;
        }
        ImGui::EndDisabled();
        if (tags_csv.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tr("Ajoutez d'abord des sections ([Verse], [Chorus]…) dans le champ ci-dessous."));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    f.dirty |= clear_button("##lyrics_clear", lyrics);

    if (!f.instrumental) draw_translate_row(lyrics);

    int tag = f.instrumental ? tag_row(INST_TAGS, IM_ARRAYSIZE(INST_TAGS), P.teal)
                             : tag_row(SONG_TAGS, IM_ARRAYSIZE(SONG_TAGS), P.teal);
    if (tag >= 0) {
        std::string t      = f.instrumental ? INST_TAGS[tag] : SONG_TAGS[tag];
        size_t      pos    = f.cursor < 0 ? lyrics.size() : std::min((size_t) f.cursor, lyrics.size());
        std::string before = lyrics.substr(0, pos);
        std::string gap    = f.instrumental ? "\n" : "\n\n";
        std::string insert = t + "\n";
        if (!before.empty() &&
            before.compare(before.size() - std::min(before.size(), gap.size()), gap.size(), gap) != 0) {
            insert = (before.back() == '\n' ? gap.substr(1) : gap) + insert;
        }
        lyrics.insert(pos, insert);
        f.cursor = (int) (pos + insert.size());
        f.dirty  = true;
    }
    float height = (f.instrumental ? 160.f : 300.f) * g_dpi;
    f.dirty |= ImGui::InputTextMultiline("##lyrics", &lyrics, ImVec2(-FLT_MIN, height),
                                         ImGuiInputTextFlags_CallbackAlways, lyrics_callback, &f);
    f.dirty |= text_context_menu(lyrics);
    end_section();
}

void App::draw_lora_section() {
    Form & f = form;
    scan_loras();
    int active_count = 0;
    for (const LoraUse & u : f.loras) active_count += u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE;
    if (!begin_section("sec_lora", tr("LoRA"), P.pink, tr("optionnelles, cumulables, une force par LoRA"),
                       active_count ? trf("%d LoRA active(s)", active_count) : std::string(tr("aucune LoRA")))) {
        end_section();
        return;
    }
    std::vector<std::string> usable;
    for (const auto & [file, size] : lora_files) {
        if (file != INSTRUMENTAL_FILE) usable.push_back(file);
    }
    if (usable.empty()) {
        muted_wrapped(tr("Aucune LoRA installée. Téléchargez-en dans Réglages > LoRA : elles changent le style, la voix ou l'ambiance du morceau."));
        if (ImGui::SmallButton(tr("Ouvrir les réglages"))) open_settings = true;
    } else {
        int active = 0;
        for (const LoraUse & u : f.loras) active += u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE;
        // Cartes : case + nom + type, description sur 2 lignes, force (ou taille et mot déclencheur)
        float line_h = ImGui::GetTextLineHeight();
        float gap    = ImGui::GetStyle().ItemSpacing.x;
        float inner  = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        int   cols   = std::max(1, (int) (inner / (250 * g_dpi)));
        float card_w = std::floor((inner - gap * (cols - 1)) / cols);
        float card_h = 2 * line_h + 2 * ImGui::GetFrameHeight() + 40 * g_dpi;
        float rows   = (float) std::min<size_t>((usable.size() + cols - 1) / cols, 3);
        ImGui::BeginChild("##lora_cards", ImVec2(0, rows * (card_h + ImGui::GetStyle().ItemSpacing.y) + 4 * g_dpi), ImGuiChildFlags_None);
        int index = 0;
        for (const std::string & file : usable) {
            if (index % cols) ImGui::SameLine(0, gap);
            index++;
            ImGui::PushID(file.c_str());
            LoraUse * use = find_lora_use(file);
            bool      on  = use && use->enabled;
            LoraRef   r   = lora_ref(file);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, on ? tint(P.card, P.accent, 0.14f) : P.card);
            ImGui::PushStyleColor(ImGuiCol_Border, on ? P.accent : P.line);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * g_dpi, 7 * g_dpi));
            ImGui::BeginChild("##card", ImVec2(card_w, card_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            if (ImGui::Checkbox("##on", &on)) set_lora(file, on);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            const char * kind = !r.entry ? custom_kind(custom_lora(file)) : !strcmp(r.entry->kind, "style") ? tr("Style") : !strcmp(r.entry->kind, "artist") ? tr("Artiste")
                              : !strcmp(r.entry->kind, "slider") ? tr("Curseur") : !strcmp(r.entry->kind, "instrument") ? tr("Instrument") : tr("Compos.");
            float pill_w = pill_width(kind);
            ImGui::PushFont(g_bold);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - pill_w - 8 * g_dpi);
            ImGui::TextUnformatted(custom_label(file).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - pill_w);
            pill(kind, on ? P.accent : P.muted);

            const CustomLora * cl = r.entry ? nullptr : custom_lora(file);
            std::string desc = r.entry ? std::string(tr(r.entry->description)) : cl ? custom_description(*cl) : std::string(tr("LoRA personnelle déposée dans modeles\\lora."));
            float       wrap = ImGui::GetContentRegionAvail().x;
            std::string shown = desc;
            if (ImGui::CalcTextSize(shown.c_str(), nullptr, false, wrap).y > 2 * line_h + 1) {  // 2 lignes au plus
                std::string cut;
                size_t      pos = 0;
                while (pos < desc.size()) {
                    size_t next = desc.find(' ', pos + 1);
                    if (next == std::string::npos) next = desc.size();
                    std::string trial = desc.substr(0, next) + "…";
                    if (ImGui::CalcTextSize(trial.c_str(), nullptr, false, wrap).y > 2 * line_h + 1) break;
                    cut = trial;
                    pos = next;
                }
                shown = cut.empty() ? desc : cut;
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            ImGui::TextColored(P.muted, "%s", shown.c_str());
            ImGui::PopTextWrapPos();
            // la description occupe toujours deux lignes : la force reste alignée d'une carte à l'autre
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (shown.find('\n') == std::string::npos &&
                                 ImGui::CalcTextSize(shown.c_str(), nullptr, false, wrap).y < 1.5f * line_h ? line_h : 0));
            float lo = r.entry ? r.entry->range_min : 0.f, hi = r.entry ? r.entry->range_max : 2.f;
            if (on && use) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##force", &use->scale, lo, hi, tr("force %.2f"))) f.dirty = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Intensité de la LoRA (1,0 = réglage conseillé en général)."));
            } else {
                unsigned long long size = lora_files.count(file) ? lora_files[file] : 0;
                std::string        info = fmt_mb(size);
                std::string word = lora_trigger(file);
                info += word.empty() ? std::string(tr("  ·  sans mot déclencheur")) : std::string(tr("  ·  mot : ")) + word;
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(P.muted, "%s", info.c_str());
            }
            ImGui::EndChild();
            if (ImGui::IsItemHovered()) {
                std::string tip = trf("%s\nFichier : %s", desc.c_str(), file.c_str());
                std::string word = lora_trigger(file);
                tip += word.empty() ? std::string(tr("\nSans mot déclencheur : la LoRA est quand même appliquée à la génération."))
                                    : trf("\nMot déclencheur « %s » : ajouté au style quand la LoRA est cochée.", word.c_str());
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (active > 0) {
            muted_wrapped(trf("%d LoRA active(s). Chacune est fusionnée dans le modèle au chargement : comptez 10 à 30 s de plus par morceau, sans ralentissement ensuite.", active));
        } else {
            ImGui::TextColored(P.muted, tr("Aucune LoRA utilisée pour ce morceau."));
        }
    }
    end_section();
}

void App::draw_generation_section() {
    Form &        f      = form;
    std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
    std::string   summary_gen = model_label(current_model) + "  ·  " + solver_label(current_solver) + "  ·  " +
                                (f.auto_duration ? std::string(tr("durée auto")) : trf("%d s", f.duration)) + "  ·  " +
                                (f.versions > 1 ? trf("%d versions", f.versions) : trf("%d version", f.versions));
    bool open = begin_section("sec_generation", tr("Génération"), P.amber, nullptr, summary_gen);
    if (settings.layout == 0) draw_generate_buttons();  // dispositions A et B : en bas du panneau (draw_form)
    draw_generation_options(open, lyrics);
}

// Boutons « Générer » et « Ajouter en pause », sur une même ligne, même quand le bloc Génération est replié.
// Pas de génération sans nom ni style (ni paroles pour une chanson).
void App::draw_generate_buttons() {
    Form &        f      = form;
    std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
    std::string missing;
    auto        need = [&](const char * what) { missing += (missing.empty() ? "" : ", ") + std::string(tr(what)); };
    if (trim(f.title).empty()) need(N_("nom du morceau"));
    if (trim(f.style).empty()) need(N_("style"));
    if (!f.instrumental && trim(lyrics).empty()) need(N_("paroles"));
    std::string button;
    if (!f.cover.empty()) button = f.versions > 1 ? trf("Générer %d versions de la reprise", f.versions) : std::string(tr("Générer la reprise"));
    else if (f.instrumental) button = f.versions > 1 ? trf("Générer %d versions de l'instrumental", f.versions) : std::string(tr("Générer l'instrumental"));
    else button = f.versions > 1 ? trf("Générer %d versions de la chanson", f.versions) : std::string(tr("Générer la chanson"));
    float       hold_w = ImGui::CalcTextSize(tr("Ajouter en pause")).x + ImGui::GetStyle().FramePadding.x * 2 + 10 * g_dpi;
    float       gen_w  = ImGui::GetContentRegionAvail().x - hold_w - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginDisabled(!missing.empty());
    ImGui::PushFont(g_bold);
    if (accent_button(button.c_str(), ImVec2(std::max(gen_w, 120 * g_dpi), 38 * g_dpi))) request_submit(false);
    ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::Button(tr("Ajouter en pause"), ImVec2(hold_w, 38 * g_dpi))) submit(true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(tr("Ajoute à la file sans lancer : vous le lancerez quand la carte graphique sera libre."));
    }
    ImGui::EndDisabled();
    if (!missing.empty()) muted_wrapped(trf("À renseigner pour lancer la génération : %s.", missing.c_str()));
    else {  // 1.1.015 : temps estimé de ce morceau, et avertissement pour un morceau long sur une carte de 12 Go
        Song p;
        p.kind         = f.cover.empty() ? "new" : "cover";
        p.instrumental = f.instrumental;
        p.cot          = f.cot == 1 ? "melody" : f.cot == 2 ? "off" : "full";
        p.solver       = current_solver;
        p.duration     = f.auto_duration ? (p.kind == "cover" ? 360 : f.instrumental ? estimate_seconds(lyrics, true) : 360) : f.duration;
        p.abc          = submit_abc;
        const double est = f.cover.empty() ? estimate_seconds(lyrics, f.instrumental) : f.cover_seconds;
        p.expected_seconds = f.auto_duration ? est : std::min<double>(f.duration, est);
        const double secs  = studio->eta_for(p) * std::max(1, f.versions);
        if (secs > 0) {
            ImGui::TextColored(P.muted, "%s", (f.versions > 1 ? trf("Temps estimé : environ %s pour %d versions.", fmt_time(secs).c_str(), f.versions)
                                                              : trf("Temps estimé : environ %s.", fmt_time(secs).c_str())).c_str());
        }
        const GpuMem g = gpu_monitor.get();
        if (p.expected_seconds > 240 && g.total > 0 && g.total <= (unsigned long long) (12.5 * 1073741824.0))
            colored_wrapped(P.amber, tr("Morceau long : sur une carte de 12 Go ou moins, la mémoire vidéo peut manquer vers la fin au-delà de 4 minutes environ ; le Studio refait alors un essai tout seul."));
    }
}

void App::draw_generation_options(bool open, std::string & lyrics) {
    Form & f = form;
    if (open) {
        static const char * engines[] = { N_("LoRA instrumentale"), N_("Modèle de base") };
        ImGui::Dummy(ImVec2(0, 3 * g_dpi));
        if (ImGui::BeginTable("options", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(P.muted, tr("Mode de composition"));
            ImGui::TableNextColumn();
            ImGui::TextColored(P.muted, f.instrumental ? tr("Moteur instrumental") : tr("Graine (-1 = aléatoire)"));
            ImGui::TableNextColumn();
            ImGui::Checkbox(tr("Durée automatique"), &f.auto_duration);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##cot", &f.cot, tr_list(COT_LABELS, (int) IM_ARRAYSIZE(COT_LABELS)).data(), IM_ARRAYSIZE(COT_LABELS));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (f.instrumental) {
                // LoRA instrumentale supprimée : seul le modèle de base reste possible (1.0.29).
                bool inst_lora = lora_files.count(INSTRUMENTAL_FILE) > 0;
                if (!inst_lora) f.engine = 1;
                ImGui::BeginDisabled(!inst_lora);
                ImGui::Combo("##engine", &f.engine, tr_list(engines, (int) IM_ARRAYSIZE(engines)).data(), 2);
                ImGui::EndDisabled();
            } else {
                ImGui::InputScalar("##seed", ImGuiDataType_S64, &f.seed);
                seed_context_menu(f.seed);
            }
            ImGui::TableNextColumn();
            if (f.auto_duration) {
                ImGui::AlignTextToFramePadding();
                double est = estimate_seconds(lyrics, f.instrumental);
                ImGui::TextColored(P.muted, "≈ %s", fmt_time(est).c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(f.instrumental ? tr("Estimée d'après la structure.") : tr("Estimée d'après les paroles : le modèle termine seul."));
                }
            } else {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##duration", &f.duration, 10, 60)) f.duration = std::clamp(f.duration, 10, 360);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Plafond en secondes : atteint, il déclenche un fondu de fin."));
            }
            if (f.instrumental) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(P.muted, tr("Graine (-1 = aléatoire)"));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputScalar("##seed", ImGuiDataType_S64, &f.seed);
                seed_context_menu(f.seed);
            }
            ImGui::EndTable();
        }
        // 1.0.48 : prototype « morceau long » (au-delà des 6 minutes du modèle)
        ImGui::BeginDisabled(!f.cover.empty());
        ImGui::Checkbox(tr("Morceau long : 2 ou 3 parties enchaînées"), &f.long_track);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Le modèle compose au plus 6 minutes d'un coup. Les paroles sont coupées en deux vers le milieu : la partie 1 est "
                                       "générée, puis la partie 2 la continue à partir de ses 25 dernières secondes, et les deux sont assemblées "
                                       "par un fondu enchaîné (jusqu'à environ 11 minutes). Résultat dans la partie 2 ; la partie 1 reste dans la "
                                       "bibliothèque. Les raccords sont à vérifier à l'oreille."));

        ImGui::Checkbox(tr("Fin en fondu sur la structure"), &f.fade_structure);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Si le morceau atteint le plafond de durée sans conclure (c'est le cas des instrumentaux), le fondu de fin\ndure comme la dernière section de la structure (outro : 20 s en instrumental) et suit une courbe douce,\nau lieu du fondu court du réglage général (%d s)."), settings.fade_seconds);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, tr("Versions"));
        for (int v = 1; v <= 3; v++) {
            ImGui::SameLine(0, v == 1 ? 10 * g_dpi : 4 * g_dpi);
            char text[8];
            snprintf(text, sizeof(text), " %d ", v);
            if (f.versions == v ? accent_button(text, ImVec2(0, 0), &P.amber) : ImGui::Button(text)) f.versions = v;
        }
    }
    end_section();
}
