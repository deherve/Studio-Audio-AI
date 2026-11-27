// Pochettes générées, album écrit par l'IA (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

fs::path    App::image_models_dir() const { return studio->paths().models_dir / "images"; }

std::string App::image_service_model(const std::string & id) const {
    auto it = settings.ai_image_models.find(id);
    if (it != settings.ai_image_models.end() && !trim(it->second).empty()) return trim(it->second);
    const ImageService * s = find_image_service(id);
    return s ? s->model : "";
}

AiEndpoint App::image_endpoint(const std::string & id) {
    AiEndpoint           ep;
    const ImageService * s = find_image_service(id);
    if (!s) return ep;
    ep.base_url = id == "custom" ? settings.ai_custom_url : s->base_url;
    ep.key      = ai_unprotect_key(settings.ai_keys[s->key_id]);
    ep.model    = image_service_model(id);
    return ep;
}

// Moteur d'images prêt ? why : raison sinon ; label : nom affiché.
bool App::image_engine_ready(std::string & why, std::string & label) {
    const std::string &  id = settings.ai_image_engine;
    const ImageService * s  = find_image_service(id);
    label = s ? tr(s->name) : tr("Aucun moteur");
    if (!s) why = tr("Choisissez un service d'images dans Réglages › IA › Pochettes.");
    else if (id == "local") {
        if (find_sd_cli(app_dir()).empty()) why = tr("Moteur d'images introuvable (dossier « outils\\sd-vulkan »).");
        else if (!image_model_ready(image_models_dir(), local_image_model(settings.ai_image_local)))
            why = tr("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes.");
        else if (studio->local_active() || ai_local_busy()) why = tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours.");
    } else if (id == "custom" && trim(settings.ai_custom_url).empty()) why = tr("Adresse du service manquante : voir Réglages › IA.");
    else if (settings.ai_keys[s->key_id].empty()) why = trf("Clé d'API de %s manquante : voir Réglages › IA.", tr(s->name));
    return why.empty();
}

// 1.0.50 : MOSS-Image garde ses poids en mémoire vive sur les cartes de 12 Go ou moins (sinon la mémoire vidéo manque).
// 1.0.58 : FLUX.2 klein aussi : en 1344 × 768 sur 12 Go, la mémoire vidéo déborde sans cela (mesuré : 81 s par image contre 27 s).
// 1.0.72 : Z-Image aussi : 60 s par image avec, 176 s sans.
bool App::image_offload(const LocalImageModel & m) {
    return (std::string(m.id) == "moss" || std::string(m.id) == "klein" || std::string(m.id) == "zimage") && gpu_profile().gpu.vram <= 12.5 * 1024.0 * 1024 * 1024;
}

// 1.0.51 : menu « ▾ » de « Générer », « Régénérer » et « Toutes les pochettes » (à appeler dans un BeginPopup).
// Renvoie vrai quand un choix est fait : model = "" (service des Réglages) ou l'identifiant d'un modèle local installé.
bool App::image_model_menu(std::string & model) {
    std::string why, label;
    bool        def_ready = image_engine_ready(why, label);
    bool        idle      = !studio->local_active() && !ai_local_busy();
    bool        sd        = !find_sd_cli(app_dir()).empty();
    bool        picked    = false;
    ImGui::TextColored(P.muted, "%s", tr("Créer avec…"));
    std::string def = trf("Réglage par défaut (%s)", settings.ai_image_engine == "local" ? local_image_model(settings.ai_image_local).name : label.c_str());
    if (ImGui::MenuItem(def.c_str(), nullptr, false, def_ready)) {
        model.clear();
        picked = true;
    }
    if (!def_ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
    ImGui::Separator();
    for (const LocalImageModel & m : local_image_models()) {
        bool        ok   = sd && idle && image_model_ready(image_models_dir(), m);
        std::string time = m.seconds >= 120 ? trf("≈ %d min", m.seconds / 60) : trf("≈ %d s", m.seconds);
        if (ImGui::MenuItem(m.name, ok ? time.c_str() : tr("non installé"), false, ok)) {
            model  = m.id;
            picked = true;
        }
        if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !idle ? tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours.")
                                          : tr("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes."));
    }
    return picked;
}

void App::cover_size(const std::string & slot, int & w, int & h) {
    bool yt = slot.size() > 8 && slot.compare(slot.size() - 8, 8, "#youtube") == 0;
    bool tt = slot.size() > 7 && slot.compare(slot.size() - 7, 7, "#tiktok") == 0;
    w       = yt ? 1024 : tt ? 576 : 768;  // ~0,6 mégapixel : 768×768 ≈ 42 s sur RX 6700 XT
    h       = yt ? 576 : tt ? 1024 : 768;
}

// Description d'image par défaut d'un album sans description écrite par l'IA : son style, sans texte.
std::string App::default_cover_prompt(const std::string & album, const std::string & style) const {
    std::vector<std::string> keep;
    for (const std::string & tg : split_tags(style)) {
        bool lang = false;
        for (const auto & L : LANGUAGES) lang = lang || (L.tag[0] && lower(L.tag) == lower(tg));
        if (!lang && lower(tg).find("bpm") == std::string::npos) keep.push_back(tg);
    }
    return "Album cover art in the mood of the album \"" + album + "\", music style: " + join_style(keep) +
           ". Striking, artistic composition. No text, no letters, no logo.";
}

std::string App::cover_prompt_for(const std::string & key, const std::string & album, const std::string & style) const {
    auto it = settings.cover_prompts.find(key);
    return it != settings.cover_prompts.end() && !trim(it->second).empty() ? it->second : default_cover_prompt(album, style);
}

  // clé d'album -> dossier, mis à jour à chaque image (frame)
void App::refresh_album_dirs(const std::vector<Song> & songs) {
    album_dirs.clear();
    std::map<std::string, double> first;  // clé d'album -> création de son plus ancien morceau
    for (const Song & s : songs) {
        if (s.album.empty()) continue;
        std::string key = album_key(s.artist, s.album);
        album_dirs.emplace(key, album_folder(s.artist, s.album));
        auto it = first.find(key);
        if (it == first.end() || s.created < it->second) first[key] = s.created;
    }
    // 1.0.46 : couleurs d'album sans doublon. Les plus anciens albums choisissent d'abord (leur couleur ne change pas
    // quand un album est créé) : case tirée de la clé, sinon la suivante libre de la palette.
    std::vector<std::pair<double, std::string>> order;
    for (const auto & [k, t] : first) order.push_back({ t, k });
    std::sort(order.begin(), order.end());
    g_album_slots.clear();
    std::vector<bool> used(ALBUM_COLORS, false);
    int               taken = 0;
    for (const auto & [t, k] : order) {
        int slot = (int) (std::hash<std::string>{}(k) % ALBUM_COLORS);
        if (taken < ALBUM_COLORS)
            while (used[slot]) slot = (slot + 1) % ALBUM_COLORS;
        used[slot] = true;
        taken++;
        g_album_slots[k] = slot;
    }
}

fs::path App::cover_dir(const std::string & slot, std::wstring & name) const {
    size_t      hash_at = slot.find('#');
    std::string key     = slot.substr(0, hash_at);
    name                = hash_at == std::string::npos ? L"pochette" : L"pochette_" + widen(slot.substr(hash_at + 1));
    if (key.rfind("titre:", 0) == 0) return song_folder(key.substr(6));
    auto it = album_dirs.find(key);
    if (it != album_dirs.end()) return it->second;
    char h[40];
    snprintf(h, sizeof(h), "%016llx", (unsigned long long) std::hash<std::string>{}(slot));
    name = widen(h);
    return pochettes_dir();
}

// Pochettes de donnees\pochettes (versions précédentes) rangées dans le dossier de leur album (1.0.43), avec journal.
void App::migrate_covers() {
    bool            changed = false;
    std::error_code ec;
    for (auto & [slot, path] : settings.covers) {
        fs::path p = widen(path);
        if (!fs::exists(p, ec)) p = pochettes_dir() / p.filename();  // chemin d'une version antérieure à la 1.0.30
        if (!fs::exists(p, ec) || p.parent_path() != pochettes_dir()) continue;
        std::wstring name;
        fs::path     dir = cover_dir(slot, name);
        if (dir == pochettes_dir()) continue;
        fs::path dst = dir / (name + p.extension().wstring());
        if (fs::exists(dst, ec)) continue;
        fs::create_directories(dir, ec);
        fs::rename(p, dst, ec);
        std::ofstream log(studio->paths().cache / L"migration.log", std::ios::binary | std::ios::app);
        log << narrow(p.wstring()) << " -> " << narrow(dst.wstring()) << (ec ? " : ECHEC " + ec.message() : std::string()) << "\n";
        if (ec) continue;
        path    = narrow(dst.wstring());
        changed = true;
    }
    if (changed) save_settings(settings, settings_path);
}

// Image (fichier) rangée comme pochette d'un emplacement, dans le dossier donné par cover_dir (1.0.43).
bool App::install_cover(const std::string & slot, const fs::path & img) {
    std::wstring    stem;
    fs::path        dir = cover_dir(slot, stem);
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<fs::path> old;
    for (auto & e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file() && e.path().stem() == stem) old.push_back(e.path());
    }
    fs::path dst = dir / (stem + img.extension().wstring());
    for (auto & p : old) if (p != img) fs::remove(p, ec);
    if (img != dst) fs::copy_file(img, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        notify(tr("Copie de l'image impossible."));
        return false;
    }
    settings.covers[slot] = narrow(dst.wstring());
    exists_cache.clear();  // nouvelle image : visible tout de suite (les vignettes suivent la date du fichier)
    save_settings(settings, settings_path);
    return true;
}

bool App::local_text_ready(LocalLlm & llm, fs::path & model, int & layers) {
    std::error_code ec;
    llm    = find_local_llm(app_dir());
    model  = ai_models_dir() / widen(settings.ai_local_model);
    auto size = fs::file_size(model, ec);
    layers = llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
    return llm.ready() && !ec && !settings.ai_local_model.empty();
}

void App::start_descriptions(const std::string & key, const std::vector<Song> & all) {
    LocalLlm llm;
    fs::path model;
    int      layers = 0;
    if (studio->local_active() || ai_local_busy()) {
        notify(tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours."));
        return;
    }
    if (!local_text_ready(llm, model, layers)) {
        notify(tr("IA locale de texte non prête (Réglages › IA)."));
        return;
    }
    std::vector<Song> songs;
    for (const Song & s : album_songs(all, key, false))
        if (s.long_rest.empty()) songs.push_back(with_full_lyrics(s, all));  // partie 1 d'un morceau long : pas de description
    if (songs.empty()) return;
    auto job   = std::make_shared<DescJob>();
    job->total = (int) songs.size() + 2;
    desc_job   = job;
    std::string lang = i18n::current();
    std::thread([job, songs, lang, llm, model, layers] {
        std::atomic<int> stage{ 0 };
        std::string      err;
        AiCallResult     r = ai_local_session(llm, model, layers, &stage, &job->cancel, [&](const AiAsk & ask) {
            write_descriptions(songs, lang, ask, [&](const std::string & what) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->current = what;
                job->done++;
            }, job->cancel, job->written, err);
        });
        std::lock_guard<std::mutex> lock(job->mutex);
        job->error = !err.empty() ? err : r.ok ? std::string() : r.error;
        job->finished = true;
    }).detach();
}

void App::poll_descriptions() {
    if (!desc_job || !desc_job->finished) return;
    auto job = desc_job;
    desc_job.reset();
    if (!job->error.empty()) notify(trf("Descriptions : %s", job->error.c_str()));
    else notify(trf("%d description(s) écrite(s) (fichiers « description.txt », modifiables).", job->written.load()));
}

void App::start_covers(std::vector<CoverTask> tasks) {
    std::string why, label;
    if (tasks.empty()) return;
    if (cover_job) {
        notify(tr("Des pochettes sont déjà en cours de création."));
        return;
    }
    // 1.0.51 : un modèle local choisi dans le menu « ▾ » passe avant le service des Réglages (toutes les tâches ont le même)
    const std::string forced = tasks[0].model;
    if (forced.empty() && !image_engine_ready(why, label)) {
        notify(why);
        return;
    }
    if (!forced.empty()) {
        if (find_sd_cli(app_dir()).empty() || !image_model_ready(image_models_dir(), local_image_model(forced))) {
            notify(tr("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes."));
            return;
        }
        if (studio->local_active() || ai_local_busy()) {
            notify(tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours."));
            return;
        }
    }
    auto job     = std::make_shared<CoverJob>();
    job->local   = !forced.empty() || settings.ai_image_engine == "local";
    job->total   = (int) tasks.size();
    job->started = ImGui::GetTime();
    cover_job    = job;
    const std::string service = settings.ai_image_engine;
    AiEndpoint        ep      = job->local ? AiEndpoint{} : image_endpoint(service);
    fs::path          sd      = find_sd_cli(app_dir()), dir = image_models_dir(), tmp = app_temp_dir() / L"pochettes";
    const LocalImageModel & model   = local_image_model(forced.empty() ? settings.ai_image_local : forced);
    const bool              offload = image_offload(model);
    std::thread([job, tasks, service, ep, sd, dir, tmp, &model, offload] {
        std::error_code ec;
        fs::create_directories(tmp, ec);
        for (const CoverTask & t : tasks) {
            if (job->cancel) break;
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->current = t.label;
            }
            char name[40];
            snprintf(name, sizeof(name), "%016llx", (unsigned long long) std::hash<std::string>{}(t.slot));
            AiCallResult r = job->local ? ai_image_local(sd, dir, model, t.prompt, t.w, t.h, tmp / (widen(name) + L".png"), &job->cancel, offload, -1, t.reference)
                                        : ai_image_online(service, ep, t.prompt, t.w, t.h, tmp / widen(name));
            std::lock_guard<std::mutex> lock(job->mutex);
            if (r.ok) job->ready.push_back({ t.slot, r.text });
            else if (job->error.empty() && !job->cancel) job->error = trf(tr(r.error.c_str()), r.error_arg.c_str());
            job->done++;
        }
        job->state = 1;
    }).detach();
}

// Images terminées : rangées comme pochettes au fur et à mesure ; message à la fin.
void App::poll_covers() {
    if (!cover_job) return;
    auto                                             job = cover_job;
    std::vector<std::pair<std::string, std::string>> todo;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        todo.swap(job->ready);
    }
    for (auto & [slot, file] : todo) {
        std::error_code ec;
        install_cover(slot, widen(file));
        fs::remove(widen(file), ec);  // copie temporaire (donnees\temp\pochettes)
    }
    if (job->state != 1) return;
    cover_job.reset();
    if (job->cancel) notify(tr("Création des pochettes arrêtée."));
    else if (!job->error.empty()) notify(job->error);
    else notify(trf("%d pochette(s) créée(s).", job->done.load()));
}

void App::draw_cover_progress() {
    if (!cover_job) return;
    std::string current;
    {
        std::lock_guard<std::mutex> lock(cover_job->mutex);
        current = cover_job->current;
    }
    int secs = (int) (ImGui::GetTime() - cover_job->started);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.pink, "%s", trf("Pochettes %d/%d : %s… %d s", std::min(cover_job->done.load() + 1, cover_job->total), cover_job->total,
                                         current.c_str(), secs).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton((std::string(tr("Arrêter")) + "##covers").c_str())) cover_job->cancel = true;
}

// Fenêtre « splash » (1.0.37) : modale centrée sur fond assombri pendant toute création de pochettes ; se ferme
// seule à la fin (le message de fin ou d'erreur passe par notify). Appelée au niveau principal et depuis les
// fenêtres « Albums… » et Réglages : une modale ne peut s'ouvrir qu'au-dessus de la fenêtre modale déjà ouverte.
void App::draw_cover_splash() {
    const char * id = tr("Création des pochettes");
    if (cover_job && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    ImGui::SetNextWindowBgAlpha(0.94f);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) return;
    if (!cover_job) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    std::string current;
    {
        std::lock_guard<std::mutex> lock(cover_job->mutex);
        current = cover_job->current;
    }
    int   done = cover_job->done, total = std::max(1, cover_job->total);
    int   secs = (int) (ImGui::GetTime() - cover_job->started);
    ImGui::PushFont(g_header);
    ImGui::TextColored(P.pink, "%s", tr("Création des pochettes…"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    colored_wrapped(P.text, trf("Image %d sur %d : %s", std::min(done + 1, total), total, current.c_str()));
    // barre : images terminées + avancée lente de l'image en cours (≈ 45 s par image en local), pour montrer l'activité
    float part = std::min(0.95f, (float) std::fmod(ImGui::GetTime() - cover_job->started, 45.0) / 45.f);
    progress_bar(((float) done + (cover_job->local ? part : 0.5f)) / (float) total, ImVec2(-FLT_MIN, 0), fmt_time(secs).c_str());
    muted_wrapped(cover_job->local ? tr("Sur la carte graphique : environ 40 s par image. La génération de musique attend la fin.")
                                   : tr("Service en ligne : quelques secondes par image."));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (ImGui::Button(tr("Annuler"))) cover_job->cancel = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Arrête la création ; les images déjà terminées sont gardées."));
    ImGui::EndPopup();
}

// 1.0.78 : description d'image de l'album ; sans description, une description simple (titre, groupe, son de l'album) est
// proposée et mémorisée, pour qu'un bouton « Générer » marche sans passer par l'onglet Pochettes.
std::string App::album_image_prompt(const std::string & key, const std::string & album, const std::string & artist) {
    std::string & p = settings.cover_prompts[key];
    if (trim(p).empty()) {
        auto        snd   = settings.album_sounds.find(key);
        std::string sound = snd != settings.album_sounds.end() ? trim(snd->second) : std::string();
        p = "Album cover art for \"" + album + "\"" + (artist.empty() ? std::string() : " by the band " + artist) +
            (sound.empty() ? std::string() : ", music style: " + sound) + ". Striking, atmospheric illustration, no text, no letters, no logo.";
        save_settings(settings, settings_path);
    }
    return p;
}

// Tâches d'un album : pochette 1:1 et, si demandé, images YouTube et TikTok. skip_existing : une image déjà là est gardée.
std::vector<App::CoverTask> App::album_cover_tasks(const std::string & key, const std::string & album, const std::string & prompt, bool youtube, bool tiktok, bool skip_existing, int & skipped) {
    std::vector<CoverTask> out;
    skipped = 0;
    fs::path reference;  // 1.1.016 : image de référence du groupe de cet album
    for (const Song & sg : studio->snapshot()) {
        if (!sg.album.empty() && album_key(sg.artist, sg.album) == key) {
            reference = group_reference(sg.artist);
            break;
        }
    }
    const std::pair<std::string, const char *> slots[] = { { key, N_("Pochette 1:1") }, { slot_youtube(key), "YouTube 16:9" }, { slot_tiktok(key), "TikTok 9:16" } };
    for (int i = 0; i < 3; i++) {
        if ((i == 1 && !youtube) || (i == 2 && !tiktok)) continue;
        if (skip_existing && !cover_for(slots[i].first).empty()) {
            skipped++;
            continue;
        }
        CoverTask t;
        t.slot   = slots[i].first;
        t.prompt = prompt;
        t.label  = album + " · " + tr(slots[i].second);
        cover_size(t.slot, t.w, t.h);
        t.reference = reference;
        out.push_back(t);
    }
    return out;
}

// ---- Album (1.0.33)

// Demande de l'album, d'après l'onglet.
AiAlbumRequest App::ai_album_request() const {
    AiAlbumRequest r;
    int            li = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
    r.language        = LANGUAGES[li].tag[0] ? LANGUAGES[li].tag : "English";
    r.influence       = trim(settings.ai_influence);
    r.theme           = trim(settings.ai_theme);
    r.mood            = trim(settings.ai_mood);
    r.extra           = trim(settings.ai_extra);
    r.band            = trim(settings.ai_band);
    r.album           = trim(settings.ai_album_title);
    r.count           = std::clamp(settings.ai_count, 1, 50);
    r.kind            = settings.ai_mixed ? 2 : settings.ai_instrumental ? 1 : 0;
    r.max_instrumentals = std::clamp(settings.ai_max_instrumentals, 0, 50);  // 1.0.35 : Réglages › IA
    r.instrumentals   = std::clamp(settings.ai_instrumentals, 0, r.max_instrumentals);
    r.avoid           = ai_avoid_words();
    r.negative        = join_style(split_style(settings.ai_negative));  // 1.0.34
    add_band_history(r);
    return r;
}

// 1.0.47 (b2, b3) : groupe déjà présent dans la bibliothèque. Ses titres ne sont jamais repris ; son son est gardé
// (album_sounds, sinon les balises de style présentes dans au moins la moitié de ses morceaux) ; si l'album existe
// déjà, les nouveaux morceaux le complètent (numéros de piste à la suite) ; sinon l'IA doit inventer un autre titre
// d'album que ceux du groupe.
std::string App::bare_title(std::string t) {
    if (t.size() > 4 && isdigit((unsigned char) t[0]) && isdigit((unsigned char) t[1]) && t[2] == '.' && t[3] == ' ') t = t.substr(4);
    return trim(t);
}

int App::band_album_last_track(const std::string & band, const std::string & album) const {
    int last = 0;
    if (band.empty() || album.empty()) return 0;
    std::string key = album_key(band, album);
    for (const Song & s : studio->snapshot())
        if (!s.album.empty() && album_key(s.artist, s.album) == key) last = std::max(last, track_number(s));
    return last;
}

void App::add_band_history(AiAlbumRequest & r) const {
    if (r.band.empty()) return;
    std::string              key = r.album.empty() ? std::string() : album_key(r.band, r.album);
    std::set<std::string>    titles, albums;
    std::map<std::string, int> tags;
    int                      count = 0;
    double                   newest = -1;
    std::string              newest_sound;
    for (const Song & s : studio->snapshot()) {
        if (fold(s.artist) != fold(r.band) || s.status == "error" || s.status == "cancelled") continue;
        bool in_album = !key.empty() && !s.album.empty() && album_key(s.artist, s.album) == key;
        r.complete |= in_album;
        if (!s.title.empty()) titles.insert(bare_title(s.title));
        if (!s.album.empty()) albums.insert(s.album);
        if (!key.empty() && r.complete && !in_album) continue;  // compléter : le son de cet album seulement
        count++;
        std::set<std::string> once;
        for (const std::string & t : split_style(s.style)) once.insert(fold(t));
        for (const std::string & t : once) tags[t]++;
        if (!s.album.empty() && s.created > newest) {
            auto it = settings.album_sounds.find(album_key(s.artist, s.album));
            if (it != settings.album_sounds.end() && !trim(it->second).empty()) {
                newest       = s.created;
                newest_sound = it->second;
            }
        }
    }
    r.used_titles.assign(titles.begin(), titles.end());
    for (const std::string & a : albums) if (!r.complete) r.used_albums.push_back(a);
    auto known = key.empty() ? settings.album_sounds.end() : settings.album_sounds.find(key);
    if (r.complete && known != settings.album_sounds.end() && !trim(known->second).empty()) r.sound = known->second;
    else if (!newest_sound.empty()) r.sound = newest_sound;
    else if (count > 0) {
        std::vector<std::string> common;
        for (const auto & [t, n] : tags)
            if (n * 2 >= count && t.find("bpm") == std::string::npos && t != "instrumental") common.push_back(t);
        r.sound = join_style(common);
    }
}

void App::start_ai_album() {
    std::string why, label;
    if (!ai_engine_ready(why, label)) {
        notify(why);
        return;
    }
    if (settings.ai_engine == "local" && other_synth_running()) {
        notify(tr("L'IA locale ne travaille qu'au repos : attendez la fin de la génération en cours."));
        return;
    }
    auto job            = std::make_shared<AiAlbumJob>();
    job->local          = settings.ai_engine == "local";
    job->started        = ImGui::GetTime();
    int li              = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
    job->language_tag   = LANGUAGES[li].tag;
    job->language_index = li;
    ai_album            = job;
    ai_album_sent       = 0;
    ai_album_titles.clear();
    ai_album_band.clear();
    ai_album_name.clear();
    ai_album_sound.clear();
    ai_album_cover_prompt.clear();
    ai_album_selected = -1;
    const AiAlbumRequest req = ai_album_request();
    job->total               = req.count;
    ai_album_offset          = req.complete ? band_album_last_track(req.band, req.album) : 0;  // 1.0.47 : pistes à la suite
    AiRequest base           = ai_request();  // langue, influence, thème général, longueur, souhaits, mots à éviter
    base.title.clear();                        // le « Titre imposé » ne sert qu'à un morceau seul
    AiEndpoint ep            = job->local ? AiEndpoint{} : ai_endpoint(settings.ai_engine);
    LocalLlm   llm           = local_llm;
    fs::path   model         = ai_models_dir() / widen(settings.ai_local_model);
    std::error_code ec;
    auto       size          = fs::file_size(model, ec);
    int        layers        = llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
    const bool polish = settings.ai_polish;  // 1.1.021
    std::thread([job, req, base, ep, llm, model, layers, polish] {
        auto work = [&](const AiAsk & ask) {
            // 1. Plan de l'album (une nouvelle demande si illisible)
            job->stage                = -1;
            const std::string system  = ai_album_system_prompt(), user = ai_album_user_prompt(req);
            AiCallResult      res     = ask(system, user);
            AiAlbumPlan       plan    = res.ok ? ai_parse_album_plan(res.text, req) : AiAlbumPlan{};
            if (res.ok && !plan.ok && !job->cancel) {
                res  = ask(system, user + "\n\nYour previous answer could not be read. Answer again with ONE valid JSON object only.");
                plan = res.ok ? ai_parse_album_plan(res.text, req) : AiAlbumPlan{};
            }
            if (!plan.ok) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->error = res.ok ? tr("Plan de l'album illisible : réessayez, ou choisissez un autre modèle d'IA.")
                                    : trf(tr(res.error.c_str()), res.error_arg.c_str());
                return;
            }
            // 1.0.34 : nom du groupe ou titre de l'album choisis par l'IA avec un mot interdit → un nouveau plan
            // (ceux tapés par l'utilisateur ne sont jamais vérifiés ni changés). Le plan qui en contient le moins est gardé.
            std::vector<std::string> bad = ai_plan_forbidden(plan, req);
            if (!bad.empty() && !job->cancel) {
                AiCallResult again = ask(system, user + "\n\n" + ai_plan_retry_prompt(bad));
                AiAlbumPlan  p2    = again.ok ? ai_parse_album_plan(again.text, req) : AiAlbumPlan{};
                if (p2.ok && ai_plan_forbidden(p2, req).size() < bad.size()) plan = p2;
            }
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->band  = plan.band;
                job->album = plan.album;
                job->sound = plan.sound;
                job->cover_prompt = plan.cover_prompt;
            }
            // 2. Un morceau à la fois ; les titres déjà écrits sont transmis pour ne pas les répéter.
            std::vector<std::string> titles, album_lines;  // 1.0.73 : lignes déjà chantées dans l'album
            for (int i = 0; i < (int) plan.tracks.size() && !job->cancel; i++) {
                const AiTrackPlan & tp = plan.tracks[i];
                job->current           = i + 1;
                {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->current_title = tp.title;
                }
                AiRequest r     = base;
                r.instrumental  = tp.instrumental;
                r.title         = tp.title;
                if (!tp.theme.empty()) r.theme = tp.theme;
                r.used_titles   = titles;
                r.used_lines    = album_lines;
                r.album_context = ai_track_context(plan, i);
                const std::string sys = ai_system_prompt(r.instrumental), u = ai_user_prompt(r);
                AiCallResult      a   = ask(sys, u);
                AiSong            s   = a.ok ? ai_parse_song(a.text, r.instrumental) : AiSong{};
                if (a.ok && !s.ok && !job->cancel) {
                    AiCallResult b = ask(sys, u + "\n\n" + ai_retry_prompt());
                    if (b.ok) s = ai_parse_song(b.text, r.instrumental);
                }
                std::vector<std::string> found = s.ok ? ai_forbidden_found(r.instrumental ? s.title : s.title + "\n" + s.lyrics, r.avoid) : std::vector<std::string>{};  // instrumental : titre seul (1.0.34)
                if (!found.empty() && !job->cancel) {
                    AiCallResult b = ask(sys, u + "\n\n" + ai_avoid_retry_prompt(found));
                    AiSong       s2 = b.ok ? ai_parse_song(b.text, r.instrumental) : AiSong{};
                    if (s2.ok && ai_forbidden_found(r.instrumental ? s2.title : s2.title + "\n" + s2.lyrics, r.avoid).size() < found.size()) s = s2;
                }
                // 1.0.37 : titre encore fautif (ex. « Echoes of the Ancients » malgré « echo ») → un titre seul est redemandé
                std::vector<std::string> bad_title = s.ok && tp.title.empty() ? ai_forbidden_found(s.title, r.avoid) : std::vector<std::string>{};
                if (!bad_title.empty() && !job->cancel) {
                    AiCallResult b  = ask("You are a songwriter. You only answer with a JSON object.", ai_title_retry_prompt(s.title, bad_title, r.avoid));
                    std::string  nt = b.ok ? ai_parse_title(b.text) : std::string();
                    if (!nt.empty() && ai_forbidden_found(nt, r.avoid).empty()) s.title = nt;
                }
                if (s.ok && !r.instrumental) {  // 1.0.73 : lignes déjà chantées dans l'album ou plus haut retirées
                    std::set<std::string> known = ai_known_lines(album_lines);
                    ai_drop_repeated_lines(s.lyrics, known);
                }
                if (polish && s.ok && !r.instrumental && !job->cancel) ai_polish_song(s, req.language, r.avoid, ask, &job->cancel);  // 1.1.021
                if (r.length == 3 && !r.instrumental) ai_lengthen_song(s, AI_LONG_LINES, r.avoid, ask, &job->cancel, album_lines);
                if (s.ok && !r.instrumental)
                    for (const std::string & l : ai_song_lines(s.lyrics))
                        if (std::find(album_lines.begin(), album_lines.end(), l) == album_lines.end()) album_lines.push_back(l);
                if (job->cancel) break;
                std::lock_guard<std::mutex> lock(job->mutex);
                if (!s.ok) {
                    job->failed++;
                    if (!a.ok && job->error.empty()) job->error = trf(tr(a.error.c_str()), a.error_arg.c_str());
                    continue;
                }
                if (!tp.title.empty()) s.title = tp.title;  // titre du plan (déjà vérifié) prioritaire
                if (s.title.empty()) s.title = trf("Piste %d", i + 1);
                titles.push_back(s.title);
                job->ready.push_back({ s, r.instrumental, i + 1 });
            }
        };
        if (job->local) {
            AiCallResult start = ai_local_session(llm, model, layers, &job->stage, &job->cancel, work);
            if (!start.ok) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->error = trf(tr(start.error.c_str()), start.error_arg.c_str());
            }
        } else {
            work([&](const std::string & system, const std::string & user) { return ai_chat(ep, system, user, 180000); });
        }
        job->state = 1;
    }).detach();
}

void App::auto_log(const std::string & line) {
    if (auto_report.empty()) return;
    if (FILE * f = _wfopen(auto_report.c_str(), L"ab")) {
        fprintf(f, "[%5.0f s] %s\n", ImGui::GetTime() - auto_t0, line.c_str());
        fclose(f);
    }
}

void App::auto_album_step() {
    static const wchar_t * env = _wgetenv(L"STUDIO_TEST_AUTO_ALBUM");
    if (!env || auto_state >= 9) return;
    if (auto_state == 0) {
        std::vector<std::string> p;
        std::string              s = narrow(env);
        for (size_t b = 0;;) {
            size_t e = s.find('|', b);
            p.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
        if (p.size() < 4) { auto_state = 9; return; }
        // textes de l'essai (exemples de la demande de Hervé) s'ils ne sont pas déjà réglés ; non enregistrés
        if (trim(settings.clip_text_start).empty()) settings.clip_text_start = "Rock Ballads Fr";
        if (trim(settings.clip_text_end).empty()) settings.clip_text_end = "Abonnez-vous\nRock Ballads Fr";
        if (p.size() >= 8) auto_formats = p[7];
        const bool clips_only = p.size() >= 7 && p[4] == "clips";  // « …|rapport|clips|Groupe|Album » : album déjà généré, seulement les clips
        auto_infl   = p[0];
        auto_count  = std::max(1, atoi(p[1].c_str()));
        auto_style  = p[2];
        auto_report = widen(p[3]);
        auto_t0     = ImGui::GetTime();
        if (clips_only) {
            auto_key        = album_key(p[5], p[6]);
            ai_album_name   = p[6];
            ai_album_sent   = 0;
            for (const Song & s : studio->snapshot())
                if (!s.album.empty() && album_key(s.artist, s.album) == auto_key) ai_album_sent++;
            auto_log("Reprise : clips seulement, album « " + p[6] + " » (" + std::to_string(ai_album_sent) + " titres connus)");
            auto_state = 3;
            return;
        }
        settings.ai_influence = auto_infl;
        settings.ai_count = auto_count;
        settings.ai_instrumental = false;
        settings.ai_mixed = false;
        settings.ai_band.clear();
        settings.ai_album_title.clear();
        auto_log("Album demandé : influence « " + auto_infl + " », " + std::to_string(auto_count) + " titres");
        start_ai_album();
        if (!ai_album) { auto_log("ECHEC : l'album n'a pas pu démarrer (voir notifications)"); auto_state = 9; PostQuitMessage(0); return; }
        auto_state = 1;
    } else if (auto_state == 1) {  // écriture de l'album, puis pochettes
        if (ai_album) return;
        if (ai_album_sent == 0) { auto_log("ECHEC : aucun morceau écrit"); auto_state = 9; PostQuitMessage(0); return; }
        auto_key = album_key(ai_album_band, ai_album_name);
        auto_log("Album écrit : « " + ai_album_name + " » de " + ai_album_band + ", " + std::to_string(ai_album_sent) + " morceaux en file");
        auto_state = 2;
    } else if (auto_state == 2) {  // pochettes finies (carte graphique libre) avant de lancer les morceaux
        if (cover_job && cover_job->state == 0) return;
        auto_log("Pochettes terminées ; lancement de la génération des morceaux");
        studio->resume_all();
        auto_state = 3;
    } else if (auto_state == 3) {  // génération des morceaux
        int done = 0, bad = 0;
        for (const Song & s : studio->snapshot())
            if (!s.album.empty() && album_key(s.artist, s.album) == auto_key) {
                if (s.status == "done") done++;
                else if (s.status == "error" || s.status == "cancelled") bad++;
            }
        if (done + bad < ai_album_sent) return;
        if (studio->local_active() || ai_local_busy()) return;  // les images du clip ne se font qu'au repos
        auto_log(std::to_string(done) + " morceau(x) terminé(s), " + std::to_string(bad) + " en échec");
        std::vector<Song> songs = album_songs(studio->snapshot(), auto_key, false);
        if (songs.empty()) { auto_state = 9; PostQuitMessage(0); return; }
        open_video_dialog(songs, ai_album_name, auto_key);
        video_yt = auto_formats.find("yt") != std::string::npos;  // formats demandés (« yt,tt,ig »)
        video_tt = auto_formats.find("tt") != std::string::npos;
        video_ig = auto_formats.find("ig") != std::string::npos;
        video_clip = true;
        skip_existing_clips = true;  // reprise après interruption : un clip déjà fait n'est pas refait
        for (size_t i = 0; i < clip_styles().size(); i++)
            if (auto_style == clip_styles()[i].id) clip_style = (int) i;
        open_video = false;
        auto jobs_before = jobs.size();
        create_videos();
        if (jobs.size() == jobs_before) { auto_log("ECHEC : aucun clip n'a été lancé"); auto_state = 9; PostQuitMessage(0); return; }
        auto_log("Clips lancés (style « " + auto_style + " »)");
        auto_state = 4;
    } else if (auto_state == 4) {  // clips
        if (jobs.empty() || jobs.back()->state == 0) return;
        std::lock_guard<std::mutex> lock(jobs.back()->mutex);
        auto_log(std::string("Clips terminés : ") + jobs.back()->message);
        auto_state = 9;
        PostQuitMessage(0);
    }
}

void App::poll_ai_album() {
    if (!ai_album) return;
    auto                      job = ai_album;
    std::vector<AiAlbumTrack> todo;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        todo.swap(job->ready);
        if (ai_album_band.empty()) ai_album_band = job->band;
        if (ai_album_name.empty()) ai_album_name = job->album;
        if (ai_album_sound.empty()) ai_album_sound = job->sound;
        if (ai_album_cover_prompt.empty()) ai_album_cover_prompt = job->cover_prompt;
    }
    // 1.0.47 : son commun gardé par album (pour le compléter ou faire un autre album du même groupe plus tard)
    if (!ai_album_band.empty() && !ai_album_name.empty() && !trim(ai_album_sound).empty()) {
        std::string & known = settings.album_sounds[album_key(ai_album_band, ai_album_name)];
        if (trim(known).empty()) {
            known = ai_album_sound;
            save_settings(settings, settings_path);
        }
    }
    if (!todo.empty()) {
        const Form saved = form;
        for (const AiAlbumTrack & t : todo) {
            Form & f = form;
            f.cover.clear();
            f.cover_seconds = 0;
            f.seed          = -1;
            // 1.0.40 : titre « 01. Titre » (numéro de piste sur 2 chiffres), sans doubler un préfixe déjà présent ;
            // les mots à éviter ont été vérifiés plus tôt, sur le titre sans numéro.
            AiSong numbered = t.song;
            const int number = t.number + ai_album_offset;
            {
                char prefix[16];
                snprintf(prefix, sizeof(prefix), "%02d. ", number);
                if (numbered.title.rfind(prefix, 0) != 0) numbered.title = prefix + numbered.title;
            }
            fill_form_with(f, numbered, t.instrumental);
            f.artist     = ai_album_band;
            f.album      = ai_album_name;
            submit_track = number;
            submit_quiet = true;
            submit(true);
            submit_track = 0;
            submit_quiet = false;
            form         = saved;
            remember_ai_song(t.song, t.instrumental);
            char num[16];
            snprintf(num, sizeof(num), "%02d. ", number);
            ai_album_titles.push_back({ num + t.song.title + (t.instrumental ? std::string(" · ") + tr("Instrumental") : std::string()),
                                        ai_song_style(t.song.style), t.song, t.instrumental, number });
            ai_album_sent++;
        }
        scan_loras(true);
        sync_lora_triggers();  // style du formulaire rendu : mots déclencheurs comme avant
        save_last_album();     // 1.0.36 : retrouvé au prochain lancement
    }
    if (job->state != 1) return;
    ai_album.reset();
    std::string error;
    int         failed;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        error  = job->error;
        failed = job->failed;
    }
    if (ai_album_sent > 0) {
        notify(trf("Album « %s » (%s) : %d morceau(x) ajouté(s) à la file, en pause.", ai_album_name.c_str(), ai_album_band.c_str(), ai_album_sent));
    }
    if (job->cancel) notify(tr("Album arrêté : les morceaux déjà écrits restent dans la file."));
    else if (!error.empty()) notify(error);
    if (failed > 0) notify(trf("%d morceau(x) de l'album illisible(s) : non ajouté(s).", failed));
    // 1.0.36 : pochettes de l'album, après l'écriture de tous les morceaux et leur mise en file (en pause).
    // Une image déjà présente n'est jamais remplacée ici (« Régénérer » de la fenêtre Albums, avec confirmation).
    if (ai_album_sent > 0 && !job->cancel && settings.ai_covers) {
        std::string key    = album_key(ai_album_band, ai_album_name);
        std::string prompt = trim(ai_album_cover_prompt).empty() ? default_cover_prompt(ai_album_name, ai_album_sound) : ai_album_cover_prompt;
        if (trim(settings.cover_prompts[key]).empty()) settings.cover_prompts[key] = prompt;
        save_settings(settings, settings_path);
        int  skipped = 0;
        auto tasks   = album_cover_tasks(key, ai_album_name, settings.cover_prompts[key], settings.ai_cover_youtube, settings.ai_cover_tiktok, true, skipped);
        if (skipped > 0) notify(trf("%d image(s) déjà présente(s) pour cet album : gardée(s).", skipped));
        start_covers(tasks);
    }
    save_last_album();
}

// « Nouveau morceau » contient-il déjà des paroles écrites (chanson) ou une structure modifiée (instrumental) ?
bool App::form_has_content(bool instrumental) const {
    const Form & f = form;
    if (instrumental) return trim(f.lyrics_inst) != trim(Form{}.lyrics_inst) && !trim(f.lyrics_inst).empty();
    return has_lyrics_text(f.lyrics_song);
}

// Morceau écrit par l'IA, présenté pour la lecture (1.0.28) : une ligne vide entre titre, style, langue et
// paroles. « Relire la réponse » sait relire ce texte (voir ai_text_for_parse).
std::string App::ai_song_text(const AiSong & s, bool instrumental) const {
    std::string t = std::string(tr("Titre")) + " : " + (s.title.empty() ? "—" : s.title) + "\n\n";
    t += std::string(tr("Style")) + " : " + s.style + "\n\n";
    if (!instrumental && !s.language.empty()) t += std::string(tr("Langue")) + " : " + s.language + "\n\n";
    t += std::string(instrumental ? tr("Structure") : tr("Paroles")) + " :\n\n" + s.lyrics;
    return t;
}

// Les intitulés traduits (« Titre : », « Titel: »…) redeviennent ceux que la lecture connaît (« Title: »…).
std::string App::ai_text_for_parse(const std::string & text) const {
    const std::pair<const char *, const char *> keys[] = {
        { N_("Titre"), "Title" }, { N_("Style"), "Style" }, { N_("Langue"), "Language" }, { N_("Structure"), "Structure" }, { N_("Paroles"), "Lyrics" } };
    std::string out;
    for (size_t start = 0; start < text.size();) {
        size_t      end  = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start            = end == std::string::npos ? text.size() : end + 1;
        for (const auto & [fr, en] : keys) {
            std::string label = tr(fr);
            if (line.rfind(label, 0) == 0) {
                size_t c = line.find_first_not_of(" \t\xc2\xa0", label.size());
                if (c != std::string::npos && line[c] == ':') line = std::string(en) + ":" + line.substr(c + 1);
                break;
            }
        }
        out += line + "\n";
    }
    return out;
}

void App::draw_ai_tab() {
    poll_ai_job();
    poll_ai_album();
    if (settings.ai_language < 0) settings.ai_language = std::max(0, language_index_for_code("fr"));  // premier affichage : français
    ImGui::PushFont(g_header);
    ImGui::TextUnformatted(tr("Composer avec l'IA"));
    ImGui::PopFont();
    // 1.0.78 : tous les champs texte de la demande remis à zéro (choix, cases et réglages gardés)
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(tr("Vider les champs")).x - ImGui::GetStyle().FramePadding.x * 2);
    ImGui::BeginDisabled(ai_local_busy() || ai_job != nullptr);
    if (soft_button(tr("Vider les champs"), P.danger, false)) {
        for (std::string * f : { &settings.ai_influence, &settings.ai_theme, &settings.ai_mood, &settings.ai_title, &settings.ai_extra,
                                 &settings.ai_band, &settings.ai_album_title, &settings.ai_negative })
            f->clear();
        save_settings(settings, settings_path);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr("Efface influence, thème, ambiance, titre, souhaits, groupe, titre de l'album et styles à éviter."));
    draw_ai_tab_inner();
}

void App::draw_ai_tab_inner() {
    muted_wrapped(tr("Décrivez le morceau voulu : l'IA écrit le titre, le style, la langue et les paroles avec leur structure. Vérifiez le résultat, puis envoyez-le dans « Nouveau morceau »."));
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));

    // Moteur
    std::string why, label;
    bool        ready = ai_engine_ready(why, label);
    begin_card(P.violet, "ai_engine");
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Moteur"));
    ImGui::SameLine();
    ImGui::TextColored(ready ? P.ok : P.amber, "● %s", label.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Réglages › IA…"))) {
        open_settings     = true;
        open_settings_cat = 6;
    }
    if (!why.empty()) colored_wrapped(P.amber, why);
    if (settings.ai_engine == "local" && local_llm.ready() && !local_llm.vulkan && gpu_profile().gpu.vram > 0) draw_llama_download();
    if (settings.ai_engine != "local" && settings.ai_engine != "server") {
        colored_wrapped(P.muted, tr("Service en ligne : la langue, l'influence et le thème sont envoyés à ce service."));
    }
    end_card();

    // Demande
    bool changed = false;
    begin_card(P.teal, "ai_request");
    const float label_w = 150 * g_dpi;
    auto        row     = [&](const char * text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", text);
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    // Un morceau ou un album (1.0.33)
    const bool album = settings.ai_album_mode;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Créer"));
    ImGui::SameLine(label_w);
    if (ImGui::RadioButton(tr("Un morceau"), !album)) {
        settings.ai_album_mode = false;
        changed                = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Un album"), album)) {
        settings.ai_album_mode = true;
        changed                = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("L'IA conçoit l'album (groupe, titre, morceaux), puis écrit chaque morceau ; le modèle n'est chargé qu'une fois. Les morceaux sont ajoutés à la file, en pause, avec les réglages et les LoRA de « Nouveau morceau »."));
    // Chanté, instrumental ou (album) mélangé
    const bool mixed = album && settings.ai_mixed;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Type"));
    ImGui::SameLine(label_w);
    if (ImGui::RadioButton(tr("Chanson"), !settings.ai_instrumental && !mixed)) {
        settings.ai_instrumental = false;
        settings.ai_mixed        = false;
        changed                  = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Instrumental"), settings.ai_instrumental && !mixed)) {
        settings.ai_instrumental = true;
        settings.ai_mixed        = false;
        changed                  = true;
    }
    if (album) {
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Mélangé"), mixed)) {
            settings.ai_mixed = true;
            changed           = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Chansons et instrumentaux dans le même album."));
    }
    if (album) {
        row(tr("Nombre de morceaux"));
        changed |= ImGui::SliderInt("##ai_count", &settings.ai_count, 1, 50);
        settings.ai_count = std::clamp(settings.ai_count, 1, 50);
        if (mixed) {
            row(tr("Dont instrumentaux"));
            ImGui::SetNextItemWidth(120 * g_dpi);
            changed |= ImGui::InputInt("##ai_inst_n", &settings.ai_instrumentals);
            // 1.0.35 : instrumentaux au plus par album, réglés dans Réglages › IA (2 par défaut)
            int cap = std::max(0, std::min(settings.ai_max_instrumentals, settings.ai_count - 1));
            settings.ai_instrumentals = std::clamp(settings.ai_instrumentals, 0, cap);
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", trf("0 = aucun, %d au plus", cap).c_str());
        }
        row(tr("Titre de l'album"));
        changed |= tx::InputTextWithHint("##ai_album", tr("facultatif : sinon l'IA le choisit"), &settings.ai_album_title);
        row(tr("Nom du groupe"));
        // 1.0.47 (b3) : liste des groupes déjà dans la bibliothèque, à droite du champ
        {
            std::set<std::string> bands;
            for (const Song & s : studio->snapshot())
                if (!trim(s.artist).empty() && s.status == "done") bands.insert(trim(s.artist));
            float pick_w = ImGui::CalcTextSize(tr("Groupe existant…")).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetNextItemWidth(std::max(120 * g_dpi, ImGui::GetContentRegionAvail().x - pick_w - ImGui::GetStyle().ItemSpacing.x));
            changed |= tx::InputTextWithHint("##ai_band", tr("facultatif : sinon l'IA l'invente"), &settings.ai_band);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(pick_w);
            ImGui::BeginDisabled(bands.empty());
            if (ImGui::BeginCombo("##ai_band_pick", tr("Groupe existant…"), ImGuiComboFlags_HeightLarge)) {
                for (const std::string & b : bands) {
                    if (ImGui::Selectable(b.c_str(), fold(b) == fold(settings.ai_band))) {
                        settings.ai_band = b;
                        settings.ai_album_title.clear();  // nouvel album (pour compléter : « Albums… › Compléter… »)
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            AiAlbumRequest probe;
            probe.band  = trim(settings.ai_band);
            probe.album = trim(settings.ai_album_title);
            add_band_history(probe);
            if (!probe.used_titles.empty())
                colored_wrapped(P.blue, probe.complete
                                            ? trf("Album existant : les nouveaux morceaux le complètent à partir de la piste %d, avec son son ; ses %d titres ne seront pas repris.",
                                                  band_album_last_track(probe.band, probe.album) + 1, (int) probe.used_titles.size())
                                            : trf("Groupe existant : son son est repris, et ses %d titres et %d album(s) ne seront pas réutilisés.",
                                                  (int) probe.used_titles.size(), (int) probe.used_albums.size()));
        }
        draw_group_style_checkbox();
        // Pochettes (1.0.36) : créées après l'écriture des morceaux et leur mise en file
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Pochettes"));
        ImGui::SameLine(label_w);
        changed |= ImGui::Checkbox(tr("Créer les pochettes"), &settings.ai_covers);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Une image carrée par album, créée après l'écriture de tous les morceaux et leur mise en file, en pause. Le service se choisit dans Réglages › IA › Pochettes."));
        ImGui::BeginDisabled(!settings.ai_covers);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("YouTube 16:9", &settings.ai_cover_youtube);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("TikTok 9:16", &settings.ai_cover_tiktok);
        ImGui::EndDisabled();
        if (settings.ai_covers) {
            std::string why, label;
            if (!image_engine_ready(why, label) && !cover_job) colored_wrapped(P.amber, why);  // sous les cases, dans la carte
        }
    }
    const bool sung = mixed || !settings.ai_instrumental;
    // Langue
    if (sung) {
        row(tr("Langue"));
        int li = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        if (ImGui::BeginCombo("##ai_lang", tr(LANGUAGES[li].label))) {
            for (int i = 0; i < (int) LANGUAGES.size(); i++) {
                if (ImGui::Selectable(tr(LANGUAGES[i].label), i == li)) {
                    settings.ai_language = i;
                    changed              = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    // Influence, avec suggestions tirées des profils d'influence du Studio
    row(tr("Influence"));
    ImGui::SetNextItemWidth(-FLT_MIN - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    changed |= tx::InputTextWithHint("##ai_infl", tr("groupe ou chanteur (ex. AC/DC, Édith Piaf)"), &settings.ai_influence);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##ai_infl_list", ImGuiDir_Down)) ImGui::OpenPopup("ai_infl_pop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Choisir parmi les profils d'influence du Studio"));
    if (ImGui::BeginPopup("ai_infl_pop")) {
        std::string want = fold(trim(settings.ai_influence));
        ImGui::BeginChild("##ai_infl_scroll", ImVec2(280 * g_dpi, 300 * g_dpi));
        for (const Influence & inf : INFLUENCES) {
            if (!want.empty() && fold(inf.name).find(want) == std::string::npos) continue;
            if (ImGui::Selectable(inf.name)) {
                settings.ai_influence = inf.name;
                changed               = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    row(tr("Thème"));
    changed |= tx::InputTextWithHint("##ai_theme", tr("facultatif (ex. la mer, un amour perdu)"), &settings.ai_theme);
    row(tr("Ambiance"));
    changed |= tx::InputTextWithHint("##ai_mood", tr("facultatif (ex. joyeuse, sombre, épique)"), &settings.ai_mood);
    if (!album) {
        row(tr("Titre imposé"));
        changed |= tx::InputTextWithHint("##ai_title", tr("facultatif : sinon l'IA le choisit"), &settings.ai_title);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Longueur"));
    ImGui::SameLine(label_w);
    // 1.0.49 : durées revues (les consignes imposent aussi le nombre de lignes) et « Très longue » = morceau en 2 parties
    const char * lengths[] = { tr("Courte (≈ 2-3 min)"), tr("Normale (≈ 3-4 min)"), tr("Longue (≈ 5-6 min)"), tr("Très longue (≈ 9-11 min, 3 parties)") };
    for (int i = 0; i < 4; i++) {
        if (i == 3) ImGui::NewLine(), ImGui::SameLine(label_w);
        else if (i) ImGui::SameLine();
        if (ImGui::RadioButton(lengths[i], settings.ai_length == i)) {
            settings.ai_length = i;
            changed            = true;
        }
        if (i == 3 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("L'IA écrit une structure assez longue pour 9 à 11 minutes ; à l'envoi, la case « Morceau long : 2 ou 3 parties enchaînées » est cochée toute seule et le morceau est généré en deux parties assemblées."));
    }
    row(tr("Autres souhaits"));
    changed |= tx::InputTextMultiline("##ai_extra", &settings.ai_extra, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2.6f));
    // Styles à éviter (1.0.34), comme dans « Nouveau morceau » : transmis à l'IA, retirés du style écrit, et
    // appliqués au « Style à éviter » de chaque morceau envoyé. Repris de « Nouveau morceau » au premier affichage.
    {
        static bool seeded = false;
        if (!seeded) {
            seeded = true;
            if (trim(settings.ai_negative).empty() && !trim(form.negative_style).empty()) {
                settings.ai_negative = form.negative_style;
                changed              = true;
            }
        }
        row(tr("Styles à éviter"));
        const char * take  = tr("Reprendre de « Nouveau morceau »");
        float        btn_w = ImGui::CalcTextSize(take).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(120 * g_dpi, ImGui::GetContentRegionAvail().x - btn_w));
        changed |= ImGui::InputTextWithHint("##ai_neg", tr("Ex. : vocoder, autotune, synthesizer"), &settings.ai_negative);
        if (ImGui::IsItemDeactivatedAfterEdit()) remember_negative_tags(settings.ai_negative);
        changed |= text_context_menu(settings.ai_negative);
        ImGui::SameLine();
        if (ImGui::SmallButton(take)) {
            settings.ai_negative = form.negative_style;
            changed              = true;
        }
        changed |= draw_negative_chips(&settings.ai_negative, 2);
    }
    {   // Profil d'influence connu : ses balises peuvent compléter le style
        std::string want  = fold(trim(settings.ai_influence));
        bool        known = false;
        for (const Influence & inf : INFLUENCES) known |= !want.empty() && fold(inf.name) == want;
        if (known) changed |= ImGui::Checkbox(tr("Compléter le style avec le profil d'influence du Studio"), &settings.ai_profile);
    }
    // Mots évités (1.0.33) : liste de Réglages › IA + mots fréquents des derniers morceaux
    {
        size_t auto_n = ai_auto_words().size();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Mots à éviter"));
        ImGui::SameLine(label_w);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(trf("%d dans la liste, %d automatique(s)", (int) settings.ai_avoid.size(), (int) auto_n).c_str());
        if (ImGui::IsItemHovered()) {
            std::string list;
            for (const std::string & w : ai_avoid_words()) list += (list.empty() ? "" : ", ") + w;
            ImGui::SetTooltip("%s", list.empty() ? tr("Aucun") : list.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Modifier…")) + "##avoid").c_str())) {
            open_settings     = true;
            open_settings_cat = 6;
        }
    }
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    colored_wrapped(P.accent, album ? ai_album_summary().c_str() : ai_summary().c_str());
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    bool running = ai_job != nullptr || ai_album != nullptr;
    ImGui::BeginDisabled(running || !ready);
    if (accent_button(album ? tr("Composer l'album") : tr("Composer"), ImVec2(0, 0), &P.violet)) {
        if (album) start_ai_album();
        else start_ai_compose();
    }
    ImGui::EndDisabled();
    if (!ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
    if (ai_album) {
        ImGui::SameLine();
        int         secs = (int) (ImGui::GetTime() - ai_album->started);
        int         cur  = ai_album->current, total = ai_album->total;
        std::string state;
        if (ai_album->stage == 0) state = trf("Chargement du modèle… %d s", secs);
        else if (cur == 0) state = trf("Plan de l'album… %d s", secs);
        else {
            std::string title;
            {
                std::lock_guard<std::mutex> lock(ai_album->mutex);
                title = ai_album->current_title;
            }
            state = title.empty() ? trf("Morceau %d/%d… %d s", cur, total, secs) : trf("Morceau %d/%d : « %s »… %d s", cur, total, title.c_str(), secs);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.violet, "%s", state.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Arrêter")) + "##album").c_str())) ai_album->cancel = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Arrête l'album : les morceaux déjà écrits restent dans la file."));
        char done[32];
        snprintf(done, sizeof(done), "%d / %d", ai_album_sent, total);
        progress_bar(total > 0 ? (float) std::max(0, cur - 1) / (float) total : 0.0f, ImVec2(-FLT_MIN, 0), done);
    }
    if (ai_job) {
        ImGui::SameLine();
        int         secs  = (int) (ImGui::GetTime() - ai_job->started);
        int         stage = ai_job->stage;
        std::string state = stage == 0 ? trf("Chargement du modèle… %d s", secs) : trf("L'IA écrit… %d s", secs);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.violet, "%s", state.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Arrêter"))) ai_job->cancel = true;
        if (ai_job->local && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Arrête le moteur local et libère la mémoire."));
    }
    end_card();
    if (changed) save_settings(settings, settings_path);

    // LoRA (1.0.35) : le même bloc que dans « Nouveau morceau », sur les mêmes données (form.loras) : cocher,
    // décocher ou changer une force ici se voit aussitôt dans l'autre onglet, et inversement.
    draw_lora_section();

    // Album en cours ou terminé : morceaux mis en file (1.0.33)
    if (album || !ai_album_titles.empty()) {
        begin_card(P.green, "ai_album_result");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.green, "%s", ai_album_name.empty() ? tr("Album") : trf("Album « %s »", ai_album_name.c_str()).c_str());
        ImGui::PopFont();
        if (!ai_album_band.empty()) colored_wrapped(P.muted, trf("Groupe : %s", ai_album_band.c_str()).c_str());
        if (ai_album_titles.empty()) muted_wrapped(tr("Les morceaux de l'album apparaîtront ici à mesure qu'ils sont écrits ; chacun est ajouté à la file, en pause : lancez-les depuis la file quand vous voulez."));
        if (!ai_album_sound.empty()) {
            colored_wrapped(P.muted, trf("Son de l'album : %s", ai_album_sound.c_str()).c_str());
            if (ImGui::SmallButton((std::string(tr("Enregistrer comme style personnalisé…")) + "##album_sound").c_str()))
                open_ai_style_save(ai_album_sound, ai_album_band.empty() ? ai_album_name : ai_album_band);
        }
        for (size_t i = 0; i < ai_album_titles.size(); i++) {
            const AiAlbumLine & t = ai_album_titles[i];
            ImGui::PushID((int) i);
            // 1.0.36 : un clic affiche le morceau (style, structure, paroles) dans « Résultat »
            if (ImGui::Selectable(t.label.c_str(), ai_album_selected == (int) i, ImGuiSelectableFlags_None,
                                  ImVec2(ImGui::CalcTextSize(t.label.c_str()).x + 8 * g_dpi, 0)) && t.song.ok) {
                ai_album_selected    = (int) i;
                ai_song              = t.song;
                ai_song_instrumental = t.instrumental;
                ai_response          = ai_song_text(t.song, t.instrumental);
            }
            if (!t.style.empty()) {
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.style.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Enregistrer le style…"))) {
                    std::string name = t.label.size() > 4 ? t.label.substr(4) : t.label;  // sans « 01. »
                    size_t      dot  = name.find(" · ");
                    open_ai_style_save(t.style, dot == std::string::npos ? name : name.substr(0, dot));
                }
            }
            ImGui::PopID();
        }
        // Pochettes (1.0.36) : progression, ou bouton pour les créer après coup (album déjà écrit)
        if (cover_job) draw_cover_progress();
        else if (!ai_album_titles.empty() && !ai_album_name.empty() && !ai_album) {
            std::string key = album_key(ai_album_band, ai_album_name);
            if (!cover_for(key).empty()) ImGui::TextColored(P.ok, "%s", tr("● pochette de l'album présente (fenêtre « Albums… »)"));
            else if (ImGui::SmallButton(tr("Créer la pochette de l'album"))) {
                std::string prompt = trim(ai_album_cover_prompt).empty() ? default_cover_prompt(ai_album_name, ai_album_sound) : ai_album_cover_prompt;
                if (trim(settings.cover_prompts[key]).empty()) settings.cover_prompts[key] = prompt;
                int skipped = 0;
                start_covers(album_cover_tasks(key, ai_album_name, settings.cover_prompts[key], settings.ai_cover_youtube, settings.ai_cover_tiktok, true, skipped));
            }
        }
        end_card();
    }

    // Résultat
    begin_card(P.accent, "ai_result");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, "%s", tr("Résultat"));
    ImGui::PopFont();
    if (ai_song.ok) {
        ImGui::TextColored(P.muted, "%s", tr("Titre"));
        ImGui::SameLine(label_w);
        ImGui::TextWrapped("%s", ai_song.title.empty() ? "—" : ai_song.title.c_str());
        ImGui::TextColored(P.muted, "%s", tr("Style"));
        ImGui::SameLine(label_w);
        ImGui::TextWrapped("%s", ai_song.style.c_str());
        if (!ai_song.language.empty()) {
            ImGui::TextColored(P.muted, "%s", tr("Langue"));
            ImGui::SameLine(label_w);
            ImGui::TextUnformatted(ai_song.language.c_str());
        }
        ImGui::TextColored(P.muted, "%s", tr("Structure"));
        ImGui::SameLine(label_w);
        ImGui::TextWrapped("%s", arrow_text(structure_tags(ai_song.lyrics)).c_str());
        if (ai_song.removed > 0) colored_wrapped(P.muted, trf("%d description(s) ou indication(s) retirée(s) par le nettoyage.", ai_song.removed).c_str());
        if (accent_button(tr("Envoyer dans Nouveau morceau"), ImVec2(0, 0), &P.accent)) {
            if (form_has_content(ai_song_instrumental)) ai_confirm_send = true;
            else send_ai_song();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Remplit le titre, le style (avec la langue) et les paroles de « Nouveau morceau »."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Enregistrer comme style personnalisé…"))) open_ai_style_save(ai_song_style(ai_song.style), ai_song.title);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ajoute ce style à vos styles (liste « Préréglages… » du bloc Style)."));
        ImGui::SameLine();
    } else if (!ai_response.empty()) {
        colored_wrapped(P.amber, tr("Réponse illisible : corrigez-la ci-dessous puis « Relire la réponse », ou copiez les éléments à la main."));
    } else {
        muted_wrapped(tr("Le morceau écrit par l'IA apparaîtra ici."));
    }
    ImGui::BeginDisabled(ai_response.empty());
    if (ImGui::Button(tr("Relire la réponse"))) {
        ai_song = ai_parse_song(ai_text_for_parse(ai_response), settings.ai_instrumental);
        ai_song_instrumental = settings.ai_instrumental;
        if (!ai_song.ok) notify(tr("Réponse toujours illisible : il faut au moins un style et des paroles avec des balises [Verse], [Chorus]…"));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Relit et nettoie le texte du champ ci-dessous (après une correction à la main, ou une réponse collée depuis un autre outil)."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Copier"))) ImGui::SetClipboardText(ai_response.c_str());
    ImGui::EndDisabled();
    ImGui::TextColored(P.muted, "%s", tr("Réponse de l'IA"));
    tx::InputTextMultiline("##ai_response", &ai_response, ImVec2(-FLT_MIN, 260 * g_dpi));
    end_card();

    // Confirmation : le formulaire contient déjà un morceau
    if (ai_confirm_send) {
        ImGui::OpenPopup(tr("Remplacer le morceau en cours ?"));
        ai_confirm_send = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Remplacer le morceau en cours ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", tr("« Nouveau morceau » contient déjà un titre ou des paroles. Les remplacer par le morceau écrit par l'IA ?"));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (accent_button(tr("Remplacer"), ImVec2(0, 0), &P.accent)) {
            send_ai_song();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    draw_ai_style_modal();  // 1.0.34
}
