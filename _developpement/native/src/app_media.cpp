// Jeux, lecture continue, pochettes, exports, vidéos (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

// ------------------------------------------------------------------ Jeux, lecture continue, tâches

// Suspend la file tant qu'un jeu tourne ; la reprend 10 s après sa fermeture (évite les à-coups).
void App::update_games() {
    double now = now_seconds();
    if (now - last_game_check < 2.0) return;
    last_game_check = now;
    std::string name;
    bool        found = settings.game_pause && detect_game(settings.game_list, name);
    if (found) {
        game_clear_since = 0;
        game_name        = name;
        if (!game_active) {
            game_active = true;
            studio->set_gate(true);
            notify(trf("Jeu détecté (%s) : la file de génération est suspendue.", name.c_str()));
        }
    } else if (game_active) {
        if (!settings.game_pause) {
            game_active = false;
            studio->set_gate(false);
        } else if (game_clear_since == 0) {
            game_clear_since = now;
        } else if (now - game_clear_since >= 10.0) {
            game_active = false;
            studio->set_gate(false);
            notify(tr("Jeu terminé : la file de génération reprend."));
        }
    }
}

// Fichier lu par le lecteur : le WAV (sortie brute du moteur) ; à défaut le MP3 (morceaux livrés sans WAV, 1.0.30).
fs::path App::playable_file(const std::string & id) const {
    fs::path dir = song_folder(id);
    return !cached_exists(dir / "audio.wav") && cached_exists(dir / "audio.mp3") ? dir / "audio.mp3" : dir / "audio.wav";
}

void App::play_song(const std::string & id) {
    fs::path wav = playable_file(id);
    if (player.open(id, wav.wstring())) {
        player.set_volume(settings.volume);
        player.play();
        studio->set_heard(id, true);
    }
}

void App::start_new_playback(const std::vector<const Song *> & shown) {
    new_queue.clear();
    for (const Song * s : shown)
        if (s->status == "done" && !s->heard) new_queue.push_back(s->id);
    new_mode = !new_queue.empty();
    if (new_mode) play_song(new_queue.front());
}

void App::next_new_song() {
    auto it = std::find(new_queue.begin(), new_queue.end(), player.id());
    if (it != new_queue.end() && ++it != new_queue.end()) play_song(*it);
    else {
        new_mode = false;  // fin de la liste
        player.close();
        notify(tr("Tous les nouveaux morceaux ont été écoutés."));
    }
}

void App::update_autoplay() {
    bool playing = player.playing();
    // Un autre morceau lancé à la main : la lecture des nouveaux s'arrête.
    if (new_mode && !player.id().empty() && std::find(new_queue.begin(), new_queue.end(), player.id()) == new_queue.end()) new_mode = false;
    if (playing) {
        last_pos = player.position();
    } else if (was_playing && new_mode && !player.id().empty() && player.length() > 0 && player.stopped() &&
               last_pos >= player.length() - 2500) {
        next_new_song();
    } else if (was_playing && settings.autoplay && !player.id().empty() && player.length() > 0 && player.stopped() &&
               last_pos >= player.length() - 2500) {
        auto it = std::find(play_queue.begin(), play_queue.end(), player.id());
        if (it != play_queue.end() && ++it != play_queue.end()) play_song(*it);
    }
    was_playing = playing;
}

void App::start_quality_check(const std::vector<Song> & all, const std::string & key, const std::string & label) {
    if (quality_job && quality_job->state == 0) {
        notify(tr("Un contrôle qualité est déjà en cours."));
        return;
    }
    std::vector<Song> songs;
    for (const Song & s : all) {
        if (s.status != "done") continue;
        if (!key.empty() && (s.album.empty() || album_key(s.artist, s.album) != key)) continue;
        songs.push_back(s.long_join > 0 ? with_full_lyrics(s, all) : s);  // morceau long : paroles de toutes ses parties
    }
    if (songs.empty()) {
        notify(tr("Aucun morceau terminé à contrôler."));
        return;
    }
    const fs::path report = (key.empty() ? studio->paths().library : album_folder(songs[0].artist, songs[0].album)) / L"controle_qualite.txt";
    quality_found = std::make_shared<std::vector<QualityFinding>>();
    quality_job   = new_job(tr("Contrôle qualité — ") + label);
    start_quality(quality_job, songs, studio->paths().whisper, studio->paths().models_dir / "voix" / whisper_file().name, report, quality_found);
    notify(trf("Contrôle qualité lancé (%d morceau(x)) : les morceaux à refaire recevront un pouce rouge.", (int) songs.size()));
}

void App::update_quality() {
    if (!quality_job || quality_job->state == 0 || !quality_found) return;
    std::vector<QualityFinding> found;
    {
        std::lock_guard<std::mutex> lock(quality_job->mutex);
        found = *quality_found;
    }
    quality_found.reset();
    const std::vector<Song> all = studio->snapshot();
    for (const QualityFinding & f : found)
        for (const Song & s : all)
            if (s.id == f.id && !s.validated && !s.flagged && !quality_no_flag) studio->set_flagged(s.id, true);
}

void App::update_jobs() {
    // 1.1.008 : avant, dans draw_form seulement : en disposition B, sur une autre page que « Nouveau morceau », une pochette
    // finie n'était jamais rangée (fenêtre « Création des pochettes » bloquée, « Annuler » sans effet), et de même pour
    // les réponses de l'IA, les albums écrits par l'IA, les descriptions et la partition préparée.
    poll_ai_job();      // la réponse de l'IA est reçue même si l'on est revenu sur « Nouveau morceau »
    poll_ai_album();    // morceaux d'album mis en file au fur et à mesure (1.0.33)
    poll_covers();      // pochettes générées rangées au fur et à mesure (1.0.36)
    poll_descriptions();
    auto_album_step();  // essai automatique (variable STUDIO_TEST_AUTO_ALBUM) ; sans effet sinon
    poll_plan();        // partition préparée (onglet « Partition »)
    update_quality();
    poll_trials();       // 1.0.97
    poll_publication();
    poll_night();
    poll_stems_download();
    poll_stem_score();
    poll_sound_downloads();  // 1.0.97
    poll_render();
    poll_help();         // 1.0.98
    poll_key_check();
    for (auto & j : jobs) {
        if (j->state != 0 && !j->announced) {
            j->announced = true;
            {
                std::lock_guard<std::mutex> lock(j->mutex);
                if (j->state == 1) journal::event("travail", j->name + " : terminé.");
                else journal::error("travail", j->name + " : échec" + (j->message.empty() ? "" : " (" + j->message + ")"));
            }
            notify(j->name + (j->state == 1 ? tr(" : terminé.") : tr(" : échec.")));
        }
    }
}

// Résultat du test de santé : le pilote testé devient la référence.
void App::update_health() {
    HealthResult h = studio->health();
    if ((h.state == 2 || h.state == 3) && h.when != last_health_seen) {
        last_health_seen = h.when;
        if (!driver_version.empty()) {
            settings.gpu_driver = driver_version;
            save_settings(settings, settings_path);
        }
        driver_changed = false;
        notify(tr("Test de santé : ") + h.message);
    }
}

// ------------------------------------------------------------------ Pochettes, exports et vidéos

// Dossier des images d'albums : donnees\pochettes (1.0.30 ; à la racine du programme avant).
fs::path App::pochettes_dir() const { return studio->paths().data / "pochettes"; }

bool App::fs_entry_valid(const FsEntry & e, double now) const {
    return e.watched ? e.gen == fs_gen && now - e.at < 60.0 : now - e.at < 3.0;
}

bool App::fs_watched(const std::wstring & path) const {
    return !fs_watch_root.empty() && path.size() > fs_watch_root.size() && _wcsnicmp(path.c_str(), fs_watch_root.c_str(), fs_watch_root.size()) == 0;
}

bool App::cached_exists(const fs::path & p) const {
    double now = ImGui::GetTime();
    auto   it  = exists_cache.find(p.native());
    if (it != exists_cache.end() && fs_entry_valid(it->second, now)) return it->second.value != 0;
    std::error_code ec;
    bool            e = fs::exists(p, ec);
    if (exists_cache.size() > 4096) exists_cache.clear();
    exists_cache[p.native()] = { e ? 1 : 0, now, fs_gen, fs_watched(p.native()) };
    return e;
}

int App::video_count(const fs::path & dir) const {
    double now = ImGui::GetTime();
    auto   it  = video_cache.find(dir.native());
    if (it != video_cache.end() && fs_entry_valid(it->second, now)) return it->second.value;
    int             n = 0;
    std::error_code ec;
    for (fs::directory_iterator d(dir, ec), e; !ec && d != e; d.increment(ec)) n += _wcsicmp(d->path().extension().c_str(), L".mp4") == 0;
    if (video_cache.size() > 1024) video_cache.clear();
    video_cache[dir.native()] = { n, now, fs_gen, fs_watched(dir.native() + L"\\") };
    return n;
}

   // largeur de coupure de la description de la pochette
std::string App::cover_for(const std::string & key) const {
    auto it = settings.covers.find(key);
    if (it == settings.covers.end()) return std::string();
    fs::path p = widen(it->second);
    if (cached_exists(p)) return it->second;
    // Album renommé, morceau déplacé ou dossier du programme déplacé : même image dans le dossier actuel (1.0.43).
    std::wstring stem;
    fs::path     dir  = cover_dir(key, stem);  // à part : stem n'est rempli qu'après cet appel
    fs::path     here = dir / (stem + p.extension().wstring());
    if (cached_exists(here)) return narrow(here.wstring());
    here = pochettes_dir() / p.filename();
    return cached_exists(here) ? narrow(here.wstring()) : std::string();
}

fs::path App::ask_image(const wchar_t * title) {
    wchar_t       file[MAX_PATH] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner   = hwnd;
    std::wstring filter = file_filter(wtr("Images"), L"*.jpg;*.jpeg;*.png;*.webp;*.bmp");
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
}

fs::path App::ask_save(const std::wstring & name, const wchar_t * label, const wchar_t * ext) {
    wchar_t file[MAX_PATH] = {};
    wcsncpy_s(file, name.c_str(), _TRUNCATE);
    std::wstring filter = std::wstring(label) + L" (*." + ext + L")";
    filter.push_back(L'\0');
    filter += std::wstring(L"*.") + ext;
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = ext;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? fs::path(file) : fs::path();
}

// Emplacements d'images d'un album : la pochette (étiquettes des fichiers) et une image par réseau.
std::string App::slot_youtube(const std::string & key) { return key + "#youtube"; }

std::string App::slot_tiktok(const std::string & key) { return key + "#tiktok"; }

// Choisit une image pour un emplacement ; la copie est conservée dans le dossier « pochettes ».
bool App::set_image(const std::string & slot, const wchar_t * title) {
    fs::path img = ask_image(title);
    return !img.empty() && install_cover(slot, img);  // 1.0.43 : même rangement que les pochettes de l'IA
}

std::shared_ptr<Job> App::new_job(const std::string & name) {
    auto j  = std::make_shared<Job>();
    j->name = name;
    jobs.push_back(j);
    return j;
}

// Ouvre la fenêtre de création de vidéos pour un morceau ou un album (key = clé d'album, ou vide).
// clip = true (bouton « Clip » d'un morceau) : le clip animé est déjà coché, format YouTube seul pour un essai rapide.
void App::open_video_dialog(const std::vector<Song> & songs, const std::string & title, const std::string & key, bool clip) {
    if (songs.empty()) return;
    video_redo = false;
    video_songs = songs;
    compute_video_recs(songs);
    video_title = display_title(title);
    video_key   = key;
    open_video_dialog_images(songs, key, clip);
}

void App::compute_video_recs(const std::vector<Song> & songs) {
    {  // recommandation du nombre d'images de chaque morceau, d'après ses paroles (morceau long : les deux parties)
        video_lines.clear();
        video_rec.clear();
        const std::vector<Song> all = studio->snapshot();
        for (const Song & s0 : songs) {
            const Song  full = with_full_lyrics(s0, all);
            drop_stale_lyrics_file(s0, full, song_folder(s0.id));  // paroles.txt ancien (partie 2 seule) : relu depuis les deux parties
            std::string text = full.lyrics;
            std::ifstream pf(song_folder(s0.id) / L"paroles.txt", std::ios::binary);
            if (pf) text.assign((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
            const size_t n = full.instrumental ? 0 : clip_lyrics_lines(text).size();
            video_lines[s0.id] = (int) n;
            video_rec[s0.id]   = clip_recommend_images(n, s0.audio_seconds);
        }
    }
}

void App::open_video_dialog_images(const std::vector<Song> & songs, const std::string & key, bool clip) {
    auto pick   = [&](const std::string & slot) {  // image du réseau, à défaut la pochette de l'album
        std::string c = key.empty() ? std::string() : cover_for(slot);
        if (c.empty() && !key.empty()) c = cover_for(key);
        return c.empty() ? fs::path() : fs::path(widen(c));
    };
    img_yt     = pick(slot_youtube(key));
    img_tt     = pick(slot_tiktok(key));
    img_ig     = pick(key);  // 1:1 = la pochette carrée de l'album
    video_yt   = video_tt = video_ig = true;
    if (clip) {
        video_clip = true;
        video_tt = video_ig = false;
    }
    // 1.0.43 : Audios\<Groupe>\<Album>\videos (sans album : Audios\<Groupe>\videos) ; plus de dossier au choix
    video_dest = album_folder(songs[0].artist, key.empty() ? std::string() : songs[0].album) / L"videos";
    open_video = true;
}

// Une vidéo par réseau coché : <titre>_YouTube.mp4 (16:9), <titre>_TikTok.mp4 (9:16), <titre>_Instagram.mp4 (1:1).
// 1.0.82 : morceaux sans Short (terminés, ni « partie 1 » ni « partie 2 » d'un morceau long) : de l'album « key », ou de
// toute la bibliothèque si key est vide.
std::vector<Song> App::songs_missing_short(const std::vector<Song> & all, const std::string & key) const {
    std::vector<Song> out;
    for (const Song & s : all) {
        if (s.status != "done" || !s.long_rest.empty()) continue;
        if (!key.empty() && (s.album.empty() || album_key(s.artist, s.album) != key)) continue;
        const fs::path f = album_folder(s.artist, s.album) / L"videos" / L"Shorts" / (widen(safe_filename(display_title(s.title))) + L"_Short.mp4");
        if (!cached_exists(f)) out.push_back(s);
    }
    return out;
}

// Une seule file de création (comme les clips) : Short seul, avec les réglages du clip (rendu, moteur d'images, durée) ;
// les images d'un clip déjà fait sont reprises, sinon elles sont créées d'abord.
void App::start_missing_shorts(const std::vector<Song> & songs, const std::string & label) {
    if (songs.empty()) return;
    const bool keep_skip = skip_existing_clips;
    video_songs     = songs;
    compute_video_recs(songs);  // nombre d'images conseillé par morceau, comme la fenêtre « Créer des vidéos »
    if (!clip_style_loaded) {   // type de rendu mémorisé dans les réglages
        clip_style_loaded = true;
        for (int i = 0; i < (int) clip_styles().size(); i++)
            if (settings.clip_style_id == clip_styles()[i].id) clip_style = i;
    }
    video_dest      = album_folder(songs[0].artist, songs[0].album) / L"videos";
    video_title     = label;
    video_key.clear();
    img_yt = img_tt = img_ig = fs::path();
    video_clip      = true;
    video_yt = video_tt = video_ig = false;
    video_short     = true;
    video_redo      = false;
    video_per_album = true;
    skip_existing_clips = true;
    create_videos();
    skip_existing_clips = keep_skip;
    video_per_album     = false;
}

  // essai STUDIO_TEST_ALBUMS=shorts
void App::draw_shorts_confirm() {
    if (shorts_open) {
        ImGui::OpenPopup(tr("Shorts en série"));
        shorts_open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Shorts en série"), nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(shorts_label.c_str());
    ImGui::PopFont();
    colored_wrapped(P.text, trf("%d morceau(x) sans Short : un Short de %s s par morceau, centré sur le refrain, rangé dans le dossier « videos\\Shorts » de son album.",
                                (int) shorts_pending.size(), settings.clip_short_seconds.c_str()));
    muted_wrapped(tr("Les images du clip d'un morceau sont reprises. Si un morceau n'a pas encore de clip, ses images sont créées d'abord (quelques minutes par morceau). Les Shorts déjà faits ne sont pas refaits. Type de rendu, moteur d'images et durée : ceux de la fenêtre « Créer des vidéos »."));
    int shown = 0;
    for (const Song & s : shorts_pending)
        if (shown++ < 6) ImGui::TextColored(P.muted, "  %s — %s", s.artist.c_str(), display_title(s.title).c_str());
    if (shown > 6) ImGui::TextColored(P.muted, "%s", trf("… et %d autre(s) morceau(x).", shown - 6).c_str());
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (modal_buttons(tr("Créer les Shorts"), !shorts_pending.empty(), &P.teal)) {
        start_missing_shorts(shorts_pending, shorts_label);
        shorts_pending.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::ask_missing_shorts(const std::vector<Song> & all, const std::string & key, const std::string & label) {
    shorts_pending = songs_missing_short(all, key);
    shorts_label   = label;
    if (shorts_pending.empty()) notify(tr("Tous les morceaux ont déjà leur Short."));
    else shorts_open = true;
}

void App::create_videos() {
    std::error_code ec;
    fs::create_directories(video_dest, ec);
    struct Net { bool on; const char * name; const wchar_t * suffix; fs::path img; int w, h; };
    std::vector<VideoTask> tasks;
    std::vector<ClipTask>  clips;
    const bool             clip = video_clip;
    // 1.0.53 : scénario d'après les paroles et les fiches, puis images par scène, au repos seulement (carte graphique libre)
    SceneEngine scene_engine;
    bool        with_scenes = false;
    if (clip) {
        if (studio->local_active() || ai_local_busy()) {
            notify(tr("Les images du clip se font au repos : le clip sera fait avec la pochette tant qu'une génération est en cours."));
        } else {
            std::error_code ec2;
            scene_engine.llm        = find_local_llm(app_dir());  // cherché ici : « local_llm » n'est rempli que si l'onglet « Composer avec l'IA » a été ouvert
            scene_engine.llm_model  = ai_models_dir() / widen(settings.ai_local_model);
            auto size               = fs::file_size(scene_engine.llm_model, ec2);
            scene_engine.llm_layers = scene_engine.llm.vulkan ? ai_gpu_layers(ec2 ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            scene_engine.sd_cli      = find_sd_cli(app_dir());
            scene_engine.image_root  = image_models_dir();
            // Moteur d'images choisi dans la fenêtre (FLUX.2 klein par défaut : ≈ 14 s par image, MOSS ≈ 3 min) ; s'il n'est pas
            // installé, FLUX.2 klein à défaut.
            {
                const LocalImageModel & want = local_image_model(settings.clip_image_model.empty() ? "klein" : settings.clip_image_model);
                scene_engine.image_model = &(image_model_ready(image_models_dir(), want) ? want : local_image_model("klein"));
            }
            scene_engine.offload     = image_offload(*scene_engine.image_model);
            const ClipStyle & cs     = clip_styles()[std::min<size_t>((size_t) clip_style, clip_styles().size() - 1)];
            scene_engine.scene_count = std::clamp(atoi(settings.clip_image_count.c_str()), 0, 60);
            for (const Song & sg : video_songs) scene_engine.counts[sg.id] = clip_images_for(sg);  // recommandation ou réglage, par morceau
            scene_engine.style_id    = cs.id;
            scene_engine.style_prompt = cs.prompt;
            scene_engine.same_seed   = settings.clip_same_seed == "1";  // 1.0.98
            with_scenes              = true;
        }
        if (video_redo && with_scenes) {  // « Refaire » : scénario et images de ce rendu repartent de zéro
            for (const Song & sg : video_songs) {
                std::error_code ec6;
                fs::remove_all(clip_scenes_dir(scene_engine, sg), ec6);
            }
        }
    }
    video_redo = false;
    auto clip_task = [&](const VideoTask & t) {
        ClipTask c;
        static_cast<VideoTask &>(c) = t;
        c.text_start    = trim(settings.clip_text_start);
        c.text_end      = trim(settings.clip_text_end);
        c.karaoke       = settings.clip_karaoke == "1";  // 1.0.98
        c.end_pos       = settings.clip_end_pos == "1" ? 1 : 0;  // 1.0.98 : texte de fin centre / bas
        c.end_box       = settings.clip_end_box == "1";
        c.whisper_exe   = studio->paths().whisper;
        c.whisper_model = studio->paths().models_dir / "voix" / whisper_file().name;
        if (with_scenes) {
            c.make_scenes = [scene_engine](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out,
                                           std::string & note) { return make_clip_scenes(scene_engine, j, sg, lines, seconds, out, note); };
            c.make_scenario = [scene_engine](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out,
                                             std::string & note) { return make_clip_scenes(scene_engine, j, sg, lines, seconds, out, note, true); };
        }
        return c;
    };
    for (const Net & n : { Net{ video_yt, "YouTube", L"YouTube", img_yt, 1920, 1080 },
                           Net{ video_tt, "TikTok", L"TikTok", img_tt, 1080, 1920 },
                           Net{ video_ig, "Instagram", L"Instagram", img_ig, 1080, 1080 } }) {
        if (!n.on || (n.img.empty() && !clip)) continue;  // un clip a ses propres images : la pochette ne sert qu'en secours
        for (const Song & s : video_songs) {
            if (clip && !s.long_rest.empty()) continue;  // partie 1 d'un morceau long : seul le morceau assemblé a son clip
            VideoTask t;
            t.song   = s;
            if (clip) {
                // morceau long assemblé (partie 1 + partie 2) : le clip doit suivre les paroles des deux parties
                const Song full = with_full_lyrics(s, studio->snapshot());
                drop_stale_lyrics_file(s, full, song_folder(s.id));
                t.song = full;
            }
            t.image  = n.img;
            t.width  = n.w;
            t.height = n.h;
            // 1.0.52 : un clip animé se range dans le même dossier de format que la vidéo fixe, suffixe « _Clip »
            t.output = video_dest / (n.w > n.h ? L"YouTube" : n.w == n.h ? L"IG" : L"TikTok") /
                       (widen(safe_filename(display_title(s.title))) + L"_" + n.suffix + (clip ? L"_Clip.mp4" : L".mp4"));
            if (clip && skip_existing_clips) {
                std::error_code ec5;
                if (fs::exists(t.output, ec5)) continue;
            }
            if (clip) {
                clips.push_back(clip_task(t));
            } else {
                tasks.push_back(std::move(t));
            }
        }
    }
    // 1.0.74 : Shorts après les clips : les images du clip sont alors déjà faites et sont reprises telles quelles
    if (clip && video_short)
        for (const Song & s : video_songs) {
            if (!s.long_rest.empty()) continue;
            VideoTask t;
            t.song   = with_full_lyrics(s, studio->snapshot());
            t.image  = img_tt.empty() ? img_yt : img_tt;
            t.width  = 1080;
            t.height = 1920;
            fs::path dest = video_dest;
            if (video_per_album) {  // 1.0.82 : série sur plusieurs albums
                dest = album_folder(s.artist, s.album) / L"videos";
                const std::string key = s.album.empty() ? std::string() : album_key(s.artist, s.album);
                std::string       c   = key.empty() ? std::string() : cover_for(slot_tiktok(key));
                if (c.empty() && !key.empty()) c = cover_for(key);
                t.image = c.empty() ? fs::path() : fs::path(widen(c));
            }
            t.output = dest / L"Shorts" / (widen(safe_filename(display_title(s.title))) + L"_Short.mp4");
            std::error_code ec7;
            if (skip_existing_clips && fs::exists(t.output, ec7)) continue;
            ClipTask c      = clip_task(t);
            c.short_seconds = std::clamp(atoi(settings.clip_short_seconds.c_str()), 15, 90);
            clips.push_back(std::move(c));
        }
    if (!clips.empty()) {
        auto job = new_job(tr("Clips — ") + video_title + " (" + std::to_string(clips.size()) + ")");
        start_clips(job, studio->paths(), std::move(clips));
        notify(trf("%d titre(s) : création des clips en cours.", (int) video_songs.size()));
        return;
    }
    if (tasks.empty()) {
        if (clip) notify(tr("Aucun clip à créer : la partie 1 d'un morceau long n'a pas de clip (utilisez le morceau assemblé)."));
        return;
    }
    auto job = new_job(tr("Vidéos — ") + video_title + " (" + std::to_string(tasks.size()) + ")");
    start_videos(job, studio->paths(), std::move(tasks));
    notify(trf("%d titre(s) : création des vidéos en cours.", job ? (int) video_songs.size() : 0));
}

// Bloc d'un réseau dans la fenêtre vidéo : case, aperçu de l'image, choix de l'image.
void App::draw_video_network(const char * label, const char * format, bool & enabled, fs::path & img, const std::string & slot, float max_w, float max_h) {
    if (img.empty() && !slot.empty()) {  // 1.0.78 : image créée entre-temps (bouton « Générer »)
        std::string c = cover_for(slot);
        if (!c.empty()) img = fs::path(widen(c));
    }
    ImGui::BeginGroup();
    ImGui::Checkbox(label, &enabled);
    ImGui::SameLine();
    ImGui::TextColored(P.muted, "%s", format);
    ImTextureID tex = 0;
    ImVec2      tsize;
    ImVec2      p0 = ImGui::GetCursorScreenPos();
    float       box_w = max_w, box_h = max_h;
    if (!img.empty() && thumb_texture(img, tex, tsize, 512)) {
        float k = std::min(box_w / tsize.x, box_h / tsize.y);
        ImGui::Dummy(ImVec2(box_w, box_h));
        ImVec2 sz(tsize.x * k, tsize.y * k);
        ImVec2 a = p0 + ImVec2((box_w - sz.x) / 2, (box_h - sz.y) / 2);
        ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(box_w, box_h), ImGui::GetColorU32(P.bg), 6 * g_dpi);
        ImGui::GetWindowDrawList()->AddImage(tex, a, a + sz);
    } else {
        ImGui::Dummy(ImVec2(box_w, box_h));
        ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(box_w, box_h), ImGui::GetColorU32(P.chip), 6 * g_dpi);
        ImGui::GetWindowDrawList()->AddText(p0 + ImVec2(12 * g_dpi, box_h / 2 - 8 * g_dpi), ImGui::GetColorU32(P.muted),
                                            tr("Aucune image"));
    }
    if (soft_button(img.empty() ? tr("Choisir l'image…") : tr("Changer l'image…"), P.violet)) {
        if (!slot.empty()) {
            if (set_image(slot, wtr("Image de la vidéo").c_str())) img = fs::path(widen(settings.covers[slot]));  // mémorisée pour l'album
        } else {
            fs::path chosen = ask_image(wtr("Image de la vidéo").c_str());
            if (!chosen.empty()) img = chosen;
        }
    }
    if (img.empty() && !slot.empty() && !video_key.empty()) {  // 1.0.78 : créer l'image manquante avec le modèle d'images
        ImGui::SameLine();
        ImGui::BeginDisabled(cover_job != nullptr);
        if (soft_button(tr("Générer"), P.pink)) {
            CoverTask t;
            t.slot   = slot;
            t.prompt = album_image_prompt(video_key, video_title, video_songs.empty() ? std::string() : video_songs[0].artist);
            t.label  = video_title + " · " + label;
            cover_size(t.slot, t.w, t.h);
            start_covers({ t });
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Crée cette image avec le modèle d'images des Réglages, d'après la description de l'album."));
    }
    ImGui::EndGroup();
}

void App::draw_video_modal() {
    if (open_video) {
        ImGui::OpenPopup(tr("Créer des vidéos"));
        open_video = false;
    }
    // 1.0.76 : aussi large que la fenêtre du Studio (marge de 24 px de chaque côté) ; deux colonnes à partir de 1100 px :
    // formats et images à gauche, réglages du clip à droite
    const ImGuiViewport * vp    = ImGui::GetMainViewport();
    const float           win_w = std::max(760 * g_dpi, vp->WorkSize.x - 48 * g_dpi);
    const bool            two   = win_w >= 1100 * g_dpi;
    ImGui::SetNextWindowSize(ImVec2(win_w, 0));
    ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));  // suit la taille de la fenêtre du Studio
    if (vp->WorkSize.y > 300 * g_dpi) ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, vp->WorkSize.y - 48 * g_dpi));
    if (!ImGui::BeginPopupModal(tr("Créer des vidéos"), nullptr, ImGuiWindowFlags_NoResize)) return;
    double total = 0;
    for (const Song & s : video_songs) total += s.audio_seconds;
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(video_title.c_str());
    ImGui::PopFont();
    muted_wrapped(trf("%d morceau(x) · %s", (int) video_songs.size(), fmt_time(total).c_str()) +
                  (video_songs.size() > 1 ? tr(" · une vidéo individuelle par titre et par réseau") : "") +
                  std::string(video_clip ? "." : tr(". Chaque vidéo est une image fixe avec le son en qualité maximale (AAC 320 kbit/s).")));
    auto files_block = [&] {  // dossier de destination et noms des fichiers créés
        ImGui::TextColored(P.muted, tr("Dossier de destination"));
        ImGui::SameLine();
        ImGui::TextWrapped("%s", narrow(video_dest.wstring()).c_str());  // 1.0.43 : dossier de l'album, fixe
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        std::string base = safe_filename(display_title(video_songs.empty() ? video_title : video_songs[0].title));
        for (const Song & sg : video_songs)  // exemple : un morceau qui a vraiment un clip (pas une « partie 1 »)
            if (sg.long_rest.empty()) {
                base = safe_filename(display_title(sg.title));
                break;
            }
        ImGui::TextColored(P.muted, tr("Fichiers créés :"));
        // 1.0.57 : avec le clip, les fichiers finissent par « _Clip » et aucune image n'est exigée
        const bool  clip_names = video_clip;
        const char * clip_end  = clip_names ? "_Clip" : "";
        if (video_yt) ImGui::Text("  YouTube\\%s_YouTube%s.mp4%s%s", base.c_str(), clip_end, video_songs.size() > 1 ? " …" : "", !clip_names && img_yt.empty() ? tr("   (image manquante)") : "");
        if (video_tt) ImGui::Text("  TikTok\\%s_TikTok%s.mp4%s%s", base.c_str(), clip_end, video_songs.size() > 1 ? " …" : "", !clip_names && img_tt.empty() ? tr("   (image manquante)") : "");
        if (video_ig) ImGui::Text("  IG\\%s_Instagram%s.mp4%s%s", base.c_str(), clip_end, video_songs.size() > 1 ? " …" : "", !clip_names && img_ig.empty() ? tr("   (image manquante)") : "");
        if (video_clip && video_short) ImGui::Text("  Shorts\\%s_Short.mp4%s", base.c_str(), video_songs.size() > 1 ? " …" : "");
    };
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (two) {
        ImGui::BeginTable("##video_cols", 2, ImGuiTableFlags_BordersInnerV);
        ImGui::TableSetupColumn("##formats", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("##clip", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
    }
    // aperçus à l'échelle de la place disponible : YouTube 330 px et Instagram 186 px à l'origine ; TikTok garde la largeur de son titre (≈ 180 px) ; 2 espaces de 24 px
    auto        head = [](const char * name, const char * fmt) {  // case + nom + format : largeur du titre d'un bloc
        const ImGuiStyle & st = ImGui::GetStyle();
        return ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(name).x + st.ItemSpacing.x + ImGui::CalcTextSize(fmt).x;
    };
    const float k = std::clamp((ImGui::GetContentRegionAvail().x - 48 * g_dpi - head("TikTok", "9:16 · 1080 × 1920") - head("Instagram", "1:1 · 1080 × 1080")) /
                                   (330 * g_dpi), 0.6f, 1.3f);
    draw_video_network("YouTube", "16:9 · 1920 × 1080", video_yt, img_yt,
                       video_key.empty() ? std::string() : slot_youtube(video_key), 330 * g_dpi * k, 186 * g_dpi * k);
    ImGui::SameLine(0, 24 * g_dpi);
    draw_video_network("TikTok", "9:16 · 1080 × 1920", video_tt, img_tt,
                       video_key.empty() ? std::string() : slot_tiktok(video_key), 105 * g_dpi * k, 186 * g_dpi * k);
    ImGui::SameLine(0, 24 * g_dpi);
    draw_video_network("Instagram", "1:1 · 1080 × 1080", video_ig, img_ig, video_key, 186 * g_dpi * k, 186 * g_dpi * k);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (!video_key.empty()) muted_wrapped(tr("Les images choisies sont mémorisées pour cet album."));
    if (two) {
        ImGui::Dummy(ImVec2(0, 12 * g_dpi));
        files_block();
        ImGui::TableNextColumn();
    }

    // 1.0.52 : clip animé
    ImGui::Checkbox(tr("Vrai clip : scénario d'après les paroles et les fiches, images par scène (IA locale), paroles en sous-titres"), &video_clip);
    if (video_clip) {
        // 1.0.53 : type de rendu des images (liste de choix)
        const auto & styles = clip_styles();
        if (!clip_style_loaded) {  // type de rendu mémorisé dans les réglages
            clip_style_loaded = true;
            for (int i = 0; i < (int) styles.size(); i++)
                if (settings.clip_style_id == styles[i].id) clip_style = i;
        }
        clip_style = std::min<int>(clip_style, (int) styles.size() - 1);
        const float field_x = 130 * g_dpi;  // champs alignés (dans un tableau, SameLine compte déjà depuis le bord de la colonne)
        ImGui::TextColored(P.muted, "%s", tr("Type de rendu"));
        ImGui::SameLine(field_x);
        ImGui::SetNextItemWidth(330 * g_dpi);
        if (ImGui::BeginCombo("##clip_style", tr(styles[clip_style].label))) {
            for (int i = 0; i < (int) styles.size(); i++)
                if (ImGui::Selectable(tr(styles[i].label), i == clip_style)) {
                    clip_style              = i;
                    settings.clip_style_id = styles[i].id;
                    save_settings(settings, settings_path);
                }
            ImGui::EndCombo();
        }
        // 1.0.54 : moteur d'images local choisi avant la génération (temps estimé pour une image 16:9 de 1344 × 768)
        {
            const LocalImageModel & cur = local_image_model(settings.clip_image_model.empty() ? "klein" : settings.clip_image_model);
            ImGui::TextColored(P.muted, "%s", tr("Moteur d'images"));
            ImGui::SameLine(field_x);
            ImGui::SetNextItemWidth(330 * g_dpi);
            if (ImGui::BeginCombo("##clip_engine", cur.name)) {
                for (const LocalImageModel & m : local_image_models()) {
                    const bool ready = image_model_ready(image_models_dir(), m);
                    char       label[160];
                    snprintf(label, sizeof(label), "%s · ≈ %d s %s%s", m.name, (int) (m.seconds * 1.75), tr("par image"), ready ? "" : tr(" · non installé"));
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Selectable(label, settings.clip_image_model == m.id)) {
                        settings.clip_image_model = m.id;
                        save_settings(settings, settings_path);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox(tr("Refaire le scénario et les images (sinon ceux déjà faits sont réutilisés : montage seul, plus rapide)"), &video_redo);
        {  // 1.0.98 (atelier clip, point 8) : graine commune aux images d'un même clip
            bool same = settings.clip_same_seed == "1";
            if (ImGui::Checkbox(tr("Même graine pour toutes les images d'un clip (personnage et décor plus réguliers)"), &same)) {
                settings.clip_same_seed = same ? "1" : "0";
                save_settings(settings, settings_path);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Chaque scène garde sa description, mais le tirage de départ est le même : visage, couleurs et cadrage se ressemblent davantage d'une image à l'autre. Ce n'est pas une garantie. Ne change que les nouvelles images : cochez « Refaire » pour un clip déjà fait."));
        }
        {  // 1.0.98 : karaoké
            bool k = settings.clip_karaoke == "1";
            if (ImGui::Checkbox(tr("Karaoké : paroles défilantes (lues dans paroles.lrc, calées par Whisper sinon)"), &k)) { settings.clip_karaoke = k ? "1" : "0"; save_settings(settings, settings_path); }
        }
        ImGui::Checkbox(tr("Short (YouTube Shorts / TikTok, 9:16) centré sur le refrain, avec les images du clip"), &video_short);
        if (video_short) {  // 1.0.75 : durée au choix ; 30 s = durée conseillée pour faire découvrir un titre (TikTok : 21 à 34 s)
            static const int lengths[] = { 15, 30, 45, 60, 90 };
            const int        cur       = std::clamp(atoi(settings.clip_short_seconds.c_str()), 15, 90);
            ImGui::TextColored(P.muted, "%s", tr("Durée du Short"));
            ImGui::SameLine(field_x);
            ImGui::SetNextItemWidth(200 * g_dpi);
            if (ImGui::BeginCombo("##short_len", (std::to_string(cur) + " s" + (cur == 30 ? tr(" (conseillé)") : "")).c_str())) {
                for (int n : lengths)
                    if (ImGui::Selectable((std::to_string(n) + " s" + (n == 30 ? tr(" (conseillé)") : "")).c_str(), n == cur)) {
                        settings.clip_short_seconds = std::to_string(n);
                        save_settings(settings, settings_path);
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("30 s : durée conseillée pour faire découvrir un titre (TikTok : 21 à 34 s ; YouTube Shorts accepte jusqu'à 3 min)."));
        }
        // 1.0.55 : nombre d'images du clip (0 = automatique : 6 à 12 selon les paroles), mémorisé dans les réglages
        // 1.0.56 : recommandation par morceau (d'après le nombre de lignes de paroles et la durée), que l'on suit ou que l'on
        // ajuste : plus (×1,5), moins (×0,5) ou un nombre fixe pour tous les morceaux
        {
            int mode = settings.clip_image_mode == "more" ? 1 : settings.clip_image_mode == "less" ? 2 : settings.clip_image_mode == "fixed" ? 3 : 0;
            int old  = mode;
            ImGui::TextColored(P.muted, "%s", tr("Nombre d'images"));
            ImGui::RadioButton(tr("Recommandé"), &mode, 0);
            ImGui::SameLine();
            ImGui::RadioButton(tr("Plus (× 1,5)"), &mode, 1);
            ImGui::SameLine();
            ImGui::RadioButton(tr("Moins (× 0,5)"), &mode, 2);
            ImGui::SameLine();
            ImGui::RadioButton(tr("Nombre fixe"), &mode, 3);
            if (mode != old) {
                settings.clip_image_mode = mode == 1 ? "more" : mode == 2 ? "less" : mode == 3 ? "fixed" : "rec";
                save_settings(settings, settings_path);
            }
            if (mode == 3) {
                int n = std::clamp(atoi(settings.clip_image_count.c_str()), 0, 60);
                ImGui::SetNextItemWidth(330 * g_dpi);
                if (ImGui::SliderInt("##clip_n", &n, 0, 60, n == 0 ? tr("Auto") : "%d")) settings.clip_image_count = std::to_string(n);
                if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
            }
            // une ligne par morceau : lignes de paroles, recommandation, nombre retenu
            const LocalImageModel & em = local_image_model(settings.clip_image_model.empty() ? "klein" : settings.clip_image_model);
            int                     total = 0, shown = 0;
            double                  audio = 0;
            for (const Song & sg : video_songs) {
                if (!sg.long_rest.empty()) continue;  // partie 1 d'un morceau long : pas de clip
                const int use = clip_images_for(sg);
                total += use > 0 ? use : 9;
                audio += sg.audio_seconds > 0 ? sg.audio_seconds : sg.duration;
                if (shown++ < 4)
                    muted_wrapped(trf("%s : %d lignes de paroles · recommandé %d · retenu %s", clean_title(display_title(sg.title)).c_str(), video_lines[sg.id],
                                      video_rec[sg.id], use > 0 ? std::to_string(use).c_str() : tr("Auto")));
            }
            if (shown > 4) muted_wrapped(trf("… et %d autre(s) morceau(x).", shown - 4));
            // 1.0.57 : le montage compte aussi (≈ 1,1 fois la durée du morceau, mesuré, pour chaque format coché)
            const int img_min = std::max(1, (int) (total * em.seconds * 1.75 / 60.0 + 0.5));
            const int cut_min = std::max(1, (int) (audio * 1.1 * ((int) video_yt + (int) video_tt + (int) video_ig) / 60.0 + 0.5));
            muted_wrapped(trf("Calcul estimé : environ %d min d'images (celles déjà faites sont réutilisées) et %d min de montage pour les formats cochés.", img_min, cut_min));
        }
        // 1.0.54 : le clip affiche le titre, le groupe et l'album au début ; textes personnalisés de début et de fin
        muted_wrapped(tr("Au début du clip : titre du morceau, groupe et album. Vous pouvez ajouter vos propres textes (plusieurs lignes possibles) :"));
        ImGui::TextColored(P.muted, "%s", tr("Texte de début"));
        ImGui::SameLine(field_x);
        ImGui::SetNextItemWidth(440 * g_dpi);
        tx::InputTextMultiline("##clip_t1", &settings.clip_text_start, ImVec2(std::max(300 * g_dpi, ImGui::GetContentRegionAvail().x), 52 * g_dpi));
        if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
        ImGui::TextColored(P.muted, "%s", tr("Texte de fin"));
        ImGui::SameLine(field_x);
        ImGui::SetNextItemWidth(440 * g_dpi);
        tx::InputTextMultiline("##clip_t2", &settings.clip_text_end, ImVec2(std::max(300 * g_dpi, ImGui::GetContentRegionAvail().x), 52 * g_dpi));
        if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
        {  // 1.0.98 (atelier clip, point 9) : place du texte de fin et bandeau sombre, pour qu'il ne masque pas le personnage
            int pos = settings.clip_end_pos == "1" ? 1 : 0;
            ImGui::TextColored(P.muted, "%s", tr("Texte de fin :"));
            ImGui::SameLine(field_x);
            bool ch = ImGui::RadioButton(tr("au centre"), &pos, 0);
            ImGui::SameLine();
            ch |= ImGui::RadioButton(tr("en bas"), &pos, 1);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("En bas, à la place des paroles : le centre de l'image (souvent le personnage) reste visible."));
            if (ch) { settings.clip_end_pos = pos ? "1" : "0"; save_settings(settings, settings_path); }
            ImGui::SameLine();
            bool box = settings.clip_end_box == "1";
            if (ImGui::Checkbox(tr("fond sombre"), &box)) { settings.clip_end_box = box ? "1" : "0"; save_settings(settings, settings_path); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Bandeau noir semi-transparent derrière le texte de fin : lisible même sur une image claire."));
        }
        muted_wrapped(tr("Exemples : « Rock Ballads Fr » au début ; « Abonnez-vous à la chaîne » à la fin (affiché pendant les 8 dernières secondes)."));
    }
    if (video_clip) muted_wrapped(tr("Fichiers « _Clip.mp4 » dans les mêmes dossiers YouTube / TikTok / IG. Paroles : fiche « paroles.txt » du morceau (modifiable) ; scénario et images dans le dossier « clip_scenes » du morceau. À lancer au repos."));
    if (!video_clip && two) muted_wrapped(tr("Cochez « Vrai clip » pour un clip animé : scénario, images par scène, paroles en sous-titres et Shorts."));
    if (two) ImGui::EndTable();
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));

    if (!two) files_block();

    bool ok = (video_yt && !img_yt.empty()) || (video_tt && !img_tt.empty()) || (video_ig && !img_ig.empty()) ||
              (video_clip && (video_yt || video_tt || video_ig || video_short));
    if (modal_buttons(tr("Créer les vidéos"), ok, &P.teal)) {
        create_videos();
        ImGui::CloseCurrentPopup();
    }
    draw_cover_splash();  // 1.0.78 : images créées depuis cette fenêtre
    ImGui::EndPopup();
}

void App::export_album(const std::string & key, const std::string & artist, const std::string & album, const std::vector<Song> & all) {
    std::vector<Song> sel = album_songs(all, key, exp_validated);
    if (sel.empty()) {
        notify(exp_validated ? tr("Aucun morceau validé dans cet album : validez-en (pouce vert) ou décochez « seulement les validés ».")
                             : tr("Aucun morceau terminé dans cet album."));
        return;
    }
    if (!(exp_flac) && !exp_mp3 && !(exp_wav)) {
        notify(tr("Choisissez au moins un format d'export."));
        return;
    }
    // 1.0.46 (demande de Herve) : export dans le dossier de l'album, sous-dossier « Audios » (comme « videos »)
    fs::path    parent = album_folder(artist, album) / L"Audios";
    std::string c      = cover_for(key);
    auto        job = new_job(tr("Export — ") + (artist.empty() ? album : artist + " — " + album));
    start_export(job, studio->paths(), sel, artist, album, c.empty() ? fs::path() : fs::path(widen(c)), parent, exp_flac,
                 exp_mp3, exp_wav);
    notify(trf("Export de « %s » lancé (%d morceau(x)).", album.c_str(), (int) sel.size()));
}

void App::draw_jobs() {
    int remove_at = -1, shown = 0;
    for (int i = (int) jobs.size() - 1; i >= 0 && shown < 4; i--, shown++) {
        Job &  j     = *jobs[i];
        int    state = j.state;
        ImVec4 color = state == 0 ? P.teal : state == 1 ? P.ok : P.danger;
        std::string msg, chapters;
        fs::path    out;
        {
            std::lock_guard<std::mutex> lock(j.mutex);
            msg      = j.message;
            chapters = j.chapters;
            out      = j.output;
        }
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.panel, color, 0.07f));
        ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpi, 8 * g_dpi));
        ImGui::BeginChild("job", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushFont(g_bold);
        ImGui::TextColored(color, "%s", j.name.c_str());
        ImGui::PopFont();
        if (state == 0) {
            // 1.0.54 : vidéo en cours sur le total, puis l'étape (scénario, images…) quand il y en a une
            if (j.total > 0) {
                std::string line = trf("Vidéo %d / %d", (int) j.item, (int) j.total);
                if (!msg.empty()) line += " — " + msg;
                muted_wrapped(line);
            }
            progress_bar(j.progress, ImVec2(-FLT_MIN, 0));
            if (danger_button(tr("Annuler"))) j.stop();
        } else {
            muted_wrapped(msg);
            if (state == 1 && !out.empty() && soft_button(tr("Afficher"), P.teal)) {
                std::wstring arg = fs::is_directory(out) ? L"\"" + out.wstring() + L"\""
                                                         : L"/select,\"" + out.wstring() + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Fermer"))) remove_at = i;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    if (remove_at >= 0) jobs.erase(jobs.begin() + remove_at);
}

std::vector<App::AlbumInfo> App::list_albums(const std::vector<Song> & songs) const {
    std::map<std::string, AlbumInfo> by_key;
    for (const Song & s : songs) {
        // 1.0.36 : les albums dont les morceaux attendent dans la file (en pause) sont listés aussi
        bool waiting = s.status != "done" && s.status != "error" && s.status != "cancelled";
        if ((s.status != "done" && !waiting) || s.album.empty()) continue;
        AlbumInfo & a = by_key[album_key(s.artist, s.album)];
        if (a.key.empty()) {
            a.key    = album_key(s.artist, s.album);
            a.artist = s.artist;
            a.album  = s.album;
        }
        if (a.style.empty()) a.style = s.style;
        if (waiting) {
            a.pending++;
            continue;
        }
        a.done++;
        a.validated += s.validated;
    }
    std::vector<AlbumInfo> out;
    for (auto & [k, a] : by_key) out.push_back(a);
    std::sort(out.begin(), out.end(), [](const AlbumInfo & x, const AlbumInfo & y) { return fold(x.album) < fold(y.album); });
    return out;
}

// Emplacement d'image dans la ligne d'un album : aperçu, légende, bouton de choix.
// prompt (1.0.36) : description de l'image ; non nul = boutons « Générer » / « Régénérer » (pochettes créées par l'IA).
void App::draw_image_slot(const std::string & slot, const char * label, float w, float h, const std::string * prompt, const std::string & album) {
    std::string c = cover_for(slot);
    ImGui::PushID(slot.c_str());  // les trois emplacements ont des boutons de même libellé
    ImGui::BeginGroup();
    ImTextureID tex = 0;
    ImVec2      tsize;
    ImVec2      p0 = ImGui::GetCursorScreenPos();
    // 1.0.46 : clic sur l'image = aperçu agrandi
    if (ImGui::InvisibleButton("##apercu", ImVec2(w, h)) && !c.empty()) {
        preview_image = fs::path(widen(c));
        preview_open  = true;
    }
    if (!c.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Cliquer pour agrandir"));
    ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(w, h), ImGui::GetColorU32(P.chip), 6 * g_dpi);
    if (!c.empty() && thumb_texture(fs::path(widen(c)), tex, tsize, 256)) {
        float  k  = std::min(w / tsize.x, h / tsize.y);
        ImVec2 sz(tsize.x * k, tsize.y * k);
        ImVec2 a = p0 + ImVec2((w - sz.x) / 2, (h - sz.y) / 2);
        ImGui::GetWindowDrawList()->AddImage(tex, a, a + sz);
    }
    ImGui::TextColored(P.muted, "%s", label);
    if (soft_button(c.empty() ? tr("Choisir…") : tr("Changer…"), P.violet)) set_image(slot, wtr("Image de l'album").c_str());
    if (prompt) {
        std::string why, lbl;
        bool        ready = image_engine_ready(why, lbl);
        // 1.0.51 : le bouton ouvre le menu des modèles (celui des Réglages, ou un autre modèle local pour cette image)
        ImGui::BeginDisabled(cover_job != nullptr || trim(*prompt).empty());
        if (soft_button((std::string(c.empty() ? tr("Générer") : tr("Régénérer")) + " ▾").c_str(), P.pink)) ImGui::OpenPopup("##modele");
        if (album_menu_test && slot == album_open) {  // essai : menu ouvert d'office
            ImGui::OpenPopup("##modele");
            album_menu_test = false;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !ready ? why.c_str() : tr("Crée l'image avec la description de l'album (Réglages › IA › Pochettes)."));
        if (ImGui::BeginPopup("##modele")) {
            std::string model;
            if (image_model_menu(model)) {
                CoverTask t;
                t.slot   = slot;
                t.prompt = *prompt;
                t.label  = album + " · " + label;
                t.model  = model;
                cover_size(slot, t.w, t.h);
                if (c.empty()) start_covers({ t });
                else {  // une image existe : confirmation avant de la remplacer
                    pending_regen      = t;
                    pending_regen_open = true;
                }
            }
            ImGui::EndPopup();
        }
    }
    if (!c.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) {
            settings.covers.erase(slot);
            save_settings(settings, settings_path);
        }
    }
    ImGui::EndGroup();
    ImGui::PopID();
}
