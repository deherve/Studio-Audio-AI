// Fenêtres modales, Réglages, menu de gauche, image (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

// ------------------------------------------------------------------ Fenêtres modales
bool App::modal_buttons(const char * ok_label, bool ok_enabled, const ImVec4 * ok_color) {
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ok_enabled);
    bool ok = accent_button(ok_label, ImVec2(0, 0), ok_color) && ok_enabled;
    ImGui::EndDisabled();
    return ok;
}

void App::draw_modals() {
    draw_startup_issues();  // 1.0.81
    draw_score_view();      // 1.0.87
    draw_lyrics_view();     // 1.0.95
    draw_redo_modal();      // 1.1.006
    draw_master_modal();    // 1.0.97
    draw_pub_modal();
    draw_dup_modal();
    draw_night_modal();
    draw_help_question();  // 1.0.98
    { static bool hq_done = false; if (!hq_done && getenv("STUDIO_TEST_HELPQ")) { hq_done = true; open_helpq = true; help_q = "comment faire un Short ?"; } }  // essai
    // Essai (1.0.51) : STUDIO_TEST_ALBUMS=<clé d'album>[|1] ouvre « Albums » avec cet album déplié (onglet Vidéos si « |1 »)
    static bool test_library = [this] {  // essai (1.0.62) : STUDIO_TEST_LIBRARY=<filtre> : bibliothèque filtrée, cartes dépliées une sur deux
        wchar_t v[256];
        DWORD   n = GetEnvironmentVariableW(L"STUDIO_TEST_LIBRARY", v, 256);
        if (n == 0 || n >= 256) return false;
        filter_text = narrow(v);
        int k = 0;
        for (const Song & sg : studio->snapshot())
            if (sg.album == filter_text && k++ % 2 == 0) expanded_cards.insert(sg.id);
        return true;
    }();
    (void) test_library;
    static bool test_albums = [this] {
        wchar_t v[512];
        DWORD   n = GetEnvironmentVariableW(L"STUDIO_TEST_ALBUMS", v, 512);
        if (n == 0 || n >= 512) return false;
        std::string s = narrow(v);
        // 1.0.57 : « |v » ouvre directement « Créer des vidéos » sur cet album, clip coché
        if (s.size() > 2 && s.compare(s.size() - 2, 2, "|v") == 0) {
            std::string key = s.substr(0, s.size() - 2), name;
            std::vector<Song> all = studio->snapshot();
            for (const Song & sg : all)
                if (!sg.album.empty() && album_key(sg.artist, sg.album) == key) name = sg.album;
            open_video_dialog(album_songs(all, key, false), name, key);
            video_clip = true;
            video_short = true;  // 1.0.75 : liste des durées visible sur la capture
            return true;
        }
        if (s.rfind("qc:", 0) == 0) {  // 1.0.83 : contrôle qualité d'un album
            quality_test    = s.substr(3);
            quality_no_flag = true;  // essai : les pouces de l'utilisateur ne sont pas touchés
            open_albums  = true;
            return true;
        }
        if (s == "shorts") {  // 1.0.82 : fenêtre de confirmation des Shorts manquants de toute la bibliothèque
            shorts_test = true;
            open_albums = true;
            return true;
        }
        album_menu_test = s.size() > 2 && s.compare(s.size() - 2, 2, "|m") == 0;
        album_tab       = s.size() > 2 && s.compare(s.size() - 2, 2, "|1") == 0;
        album_tab_request = album_tab;
        album_open      = album_tab || album_menu_test ? s.substr(0, s.size() - 2) : s;
        open_albums   = true;
        album_scroll  = true;
        return true;
    }();
    (void) test_albums;
    static bool recognize_test = [this] {  // 1.0.89 : STUDIO_TEST_RECOGNIZE=<fichier audio> lance la reconnaissance
        const char * v = getenv("STUDIO_TEST_RECOGNIZE");
        if (v) start_recognize(fs::path(widen(v)), narrow(fs::path(widen(v)).stem().wstring()));
        return v != nullptr;
    }();
    (void) recognize_test;
    if (const char * pdf = getenv("STUDIO_TEST_ATELIER_PDF"))  // essai 1.0.91 : PDF écrit 4 s après le chargement de la page
        if (!atelier_pdf_done && atelier_ready_at > 0 && ImGui::GetTime() - atelier_ready_at > 4) {
            atelier_pdf_done = true;
            atelier_post("{\"type\":\"print_layout\"}");
            atelier_print_pdf(widen(pdf));
        }
    if (const char * imp = getenv("STUDIO_TEST_IMPORT")) {  // essai 1.0.93 : atelier vide, import du fichier puis « Utiliser dans le Studio »
        if (!atelier_import_done && atelier_ready_at == 0 && !atelier_is_open()) open_atelier(std::string(), tr("Atelier partition"), std::string(), "partition", fs::path());
        if (!atelier_import_done && atelier_ready_at > 0 && ImGui::GetTime() - atelier_ready_at > 3) {
            atelier_import_done = true;
            std::ifstream in(fs::path(widen(imp)), std::ios::binary);
            std::string   bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            DWORD         n = 0;
            CryptBinaryToStringA((const BYTE *) bytes.data(), (DWORD) bytes.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
            std::string b64(n, '\0');
            CryptBinaryToStringA((const BYTE *) bytes.data(), (DWORD) bytes.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &n);
            b64.resize(strlen(b64.c_str()));
            atelier_post("{\"type\":\"test_import\",\"use\":true,\"name\":\"" + narrow(fs::path(widen(imp)).filename().wstring()) + "\",\"data\":\"" + b64 + "\"}");
        }
    }
    if (getenv("STUDIO_TEST_EXPORT_DIR") && !atelier_export_done && atelier_ready_at > 0 && ImGui::GetTime() - atelier_ready_at > 4) {
        atelier_export_done = true;  // essai 1.0.92 : MIDI, MusicXML et ABC écrits dans ce dossier
        atelier_post("{\"type\":\"test_export\"}");
    }
    if (getenv("STUDIO_TEST_ATELIER_HQ") && atelier_ready_at > 0 && ImGui::GetTime() - atelier_ready_at > 5) {  // essai 1.0.97 : lecture « banque SoundFont » sans clic
        static bool hq_done = false;
        if (!hq_done) { hq_done = true; atelier_post("{\"type\":\"test_render\"}"); }
    }
    if (!atelier_test_done) {  // 1.0.91 : STUDIO_TEST_ATELIER=<titre> ouvre l'Atelier partition sur la partition de ce morceau
        const char * v = getenv("STUDIO_TEST_ATELIER");
        if (!v) atelier_test_done = true;
        else
            for (const Song & s : studio->snapshot())
                if (s.title == v && score_info(s).present) {
                    atelier_test_done = true;
                    open_atelier(score_info(s).text, display_title(s.title), s.artist, display_title(s.title), song_folder(s.id), s.lyrics);
                }
    }
    {  // 1.0.95 : STUDIO_TEST_LYRICS=<titre> ouvre « Paroles et structure » sur ce morceau
        static bool lyrics_test_done = false;
        const char * v = getenv("STUDIO_TEST_LYRICS");
        if (!lyrics_test_done && v)
            for (const Song & s : studio->snapshot())
                if (s.title.rfind(v, 0) == 0) { lyrics_test_done = true; lyrics_view_id = s.id; open_lyrics_view = true; }
    }
    if (!score_test_done) {  // 1.0.87 : STUDIO_TEST_SCORE=<titre> ouvre « Partition » sur ce morceau (dès que la bibliothèque est lue)
        const char * v = getenv("STUDIO_TEST_SCORE");
        if (!v) score_test_done = true;
        else
            for (const Song & s : studio->snapshot())
                if (s.title == v) {
                    score_test_done = true;
                    if (getenv("STUDIO_TEST_SCORE_TAB")) use_song_score(s);  // 1.0.88 : onglet Partition
                    else { score_view_id = s.id; open_score_view = true; }
                }
    }
    // Essai (1.0.69, captures de l'aide) : STUDIO_TEST_SHOT="clé=valeur;…" : filter, cards, marks, style, gauge, theme,
    // reglages (catégorie), scroll, ia, open (blocs dépliés), fold (blocs repliés), reuse (premier morceau filtré), edit.
    static bool test_shot = [this] {
        wchar_t v[512];
        DWORD   n = GetEnvironmentVariableW(L"STUDIO_TEST_SHOT", v, 512);
        if (n == 0 || n >= 512) return false;
        std::istringstream in(narrow(v));
        std::string        item;
        auto first_song = [&]() {
            for (const Song & sg : studio->snapshot())
                if (sg.status == "done" && (filter_text.empty() || sg.album == filter_text) && sg.title.find("partie") == std::string::npos) return sg;
            return Song{};
        };
        while (std::getline(in, item, ';')) {
            const size_t eq = item.find('=');
            const std::string key = item.substr(0, eq), val = eq == std::string::npos ? std::string() : item.substr(eq + 1);
            if (key == "filter") filter_text = val;
            else if (key == "cards") settings.card_mode = atoi(val.c_str());
            else if (key == "fakejob") {  // 1.1.011 : un travail qui ne finit pas (menu de gauche) ; 2 = il échoue après 12 s (journal)
                auto fj = new_job("Essai du point lumineux");
                if (atoi(val.c_str()) == 3) fj->pausable = true;  // 1.1.017 : bouton Pause
                if (atoi(val.c_str()) == 2)
                    fj->thread = std::thread([fj] {
                        std::this_thread::sleep_for(std::chrono::seconds(12));
                        { std::lock_guard<std::mutex> lock(fj->mutex); fj->message = "Échec voulu pour l'essai du journal"; }
                        fj->state = 2;
                    });
            }
            else if (key == "videodlg") {  // 1.1.017 : fenetre « Creer des videos » de l'album « Groupe|Album »
                const size_t bar = val.find('|');
                if (bar != std::string::npos) {
                    const std::string k = album_key(val.substr(0, bar), val.substr(bar + 1));
                    open_video_dialog(album_songs(studio->snapshot(), k, false), val.substr(bar + 1), k, true);
                }
            }
            else if (key == "videogo") create_videos();  // 1.1.017 : lance « Créer les vidéos » (essai de la reprise)
            else if (key == "groupstyle") settings.save_group_style = atoi(val.c_str()) != 0;  // 1.1.011
            else if (key == "layout") settings.layout = std::clamp(atoi(val.c_str()), 0, 2);  // 1.1.002
            else if (key == "page") b_page = std::clamp(atoi(val.c_str()), 0, 3);  // 1.1.003 : page de la disposition B (1.1.005 : 2 réglages, 3 aide)
            else if (key == "libtab") lib_tab = lib_tab_request = std::clamp(atoi(val.c_str()), 0, 1);  // 1.1.005 : 1 = onglet Albums
            else if (key == "redo") {  // 1.1.006 : fenêtre « Refaire à partir d'ici » du morceau de ce titre, section n° redopick
                for (const Song & sg : studio->snapshot())
                    if (sg.title == val) ask_redo(sg);
            }
            else if (key == "redopick") redo_pick_test = atoi(val.c_str());
            else if (key == "dups") start_dups(studio->snapshot());  // 1.1.007 : recherche de doublons (fenêtre à la fin)
            else if (key == "cover") {  // 1.1.008 : pochette 1:1 de l'album « Groupe|Album » (moteur des Réglages)
                const size_t bar = val.find('|');
                if (bar != std::string::npos) {
                    const std::string artist = val.substr(0, bar), album = val.substr(bar + 1), k = album_key(artist, album);
                    CoverTask task;
                    task.slot   = k;
                    task.prompt = album_image_prompt(k, album, artist);
                    task.label  = album;
                    start_covers({ task });
                }
            }
            else if (key == "marks") settings.mark_style = atoi(val.c_str());
            else if (key == "style") settings.style_mode = atoi(val.c_str());
            else if (key == "gauge") settings.gauge_style = atoi(val.c_str());
            else if (key == "theme") {
                settings.theme = g_theme = atoi(val.c_str());
                theme_pending  = true;
            }
            else if (key == "reglages") {
                open_settings     = true;
                open_settings_cat = atoi(val.c_str());
            }
            else if (key == "scroll") test_scroll = (float) atof(val.c_str()) * g_dpi, test_scroll_frames = 20;
            else if (key == "ia") form_tab_request = 1;
            else if (key == "open") g_collapsed.clear();
            else if (key == "fold") {
                std::istringstream ids(val);
                std::string        id;
                while (std::getline(ids, id, ',')) g_collapsed.insert(id);
            }
            else if (key == "reuse") {
                reuse(first_song());
                form.style = test_clean_style(form.style);
            }
            else if (key == "edit") {
                Song s      = first_song();
                edit_id     = s.id;
                edit_title  = s.title;
                edit_artist = s.artist;
                edit_album  = s.album;
                edit_track  = s.track;
                open_edit   = true;
            }
        }
        return true;
    }();
    (void) test_shot;
    draw_albums_modal();
    if (!albums_drawn) draw_video_modal();  // sinon déjà dessinée à l'intérieur de « Albums »
    settings_shown = false;
    draw_settings_modal();
    if (!settings_shown) draw_pack_modals();  // pack déposé sur la fenêtre, Réglages fermés
    // 1.0.37 : splash des pochettes au niveau principal, seulement si aucune autre fenêtre modale n'est ouverte
    // (sinon elle est dessinée depuis cette fenêtre-là : « Albums… », Réglages)
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::IsPopupOpen(tr("Création des pochettes"))) draw_cover_splash();
    draw_structure_modals();
    draw_ai_style_modal();  // 1.0.46 : « Style perso… » des cartes (sinon ouverte depuis l'onglet IA)
    // Supprimer un morceau
    if (!pending_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer le morceau ?"))) ImGui::OpenPopup(tr("Supprimer le morceau ?"));
    ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer le morceau ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("Le morceau et ses fichiers (FLAC, WAV, MP3…) seront envoyés à la corbeille de Windows : vous pourrez les y récupérer."));
        path_line(song_folder(pending_delete));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Mettre à la corbeille"), ImVec2(0, 0), &P.danger)) {
            if (player.id() == pending_delete) player.close();
            std::string err;
            if (!studio->remove(pending_delete, err)) notify(err);
            pending_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Modifier un morceau de la bibliothèque
    if (open_edit) {
        ImGui::OpenPopup(tr("Modifier le morceau"));
        open_edit = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Modifier le morceau"), nullptr, ImGuiWindowFlags_NoResize)) {
        muted_wrapped(tr("Les étiquettes des fichiers FLAC et MP3 sont mises à jour (sans réencodage)."));
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        ImGui::TextColored(P.muted, tr("Titre"));
        input_line("##edit_title", tr("Titre"), edit_title);
        ImGui::TextColored(P.muted, tr("Groupe ou chanteur"));
        input_line("##edit_artist", tr("Groupe ou chanteur"), edit_artist);
        ImGui::TextColored(P.muted, tr("Album"));
        input_line("##edit_album", tr("Album"), edit_album);
        ImGui::TextColored(P.muted, tr("N° de piste"));
        input_line("##edit_track", tr("Ex. : 01"), edit_track, 160 * g_dpi);
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        ImGui::TextColored(P.muted, tr("Pochette"));
        {
            std::string ckey = cover_key(edit_id, trim(edit_artist), trim(edit_album));
            draw_cover_art(ckey, edit_title, 76 * g_dpi);
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::BeginGroup();
            if (soft_button(tr("Choisir une image…"), P.violet)) set_image(ckey, wtr("Pochette du morceau").c_str());
            if (!cover_for(ckey).empty()) {
                ImGui::SameLine();
                if (danger_button(tr("Retirer"))) {
                    settings.covers.erase(ckey);
                    save_settings(settings, settings_path);
                }
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(P.muted, "%s", edit_album.empty() ? tr("Sans album, la pochette appartient à ce morceau seul.")
                                                                 : tr("La pochette est celle de l'album : tous ses morceaux la partagent (elle sert aussi aux exports et aux vidéos)."));
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
        }
        if (modal_buttons(tr("Enregistrer"), !trim(edit_title).empty(), &P.blue)) {
            if (player.id() == edit_id) player.close();  // le fichier MP3 ne doit pas être ouvert
            studio->edit_metadata(edit_id, trim(edit_title), trim(edit_artist), trim(edit_album), trim(edit_track));
            notify(tr("Morceau mis à jour."));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Re-rendu : même graine ou graine aléatoire
    if (open_rerender) {
        ImGui::OpenPopup(tr("Refaire le son : choix de la graine"));
        open_rerender = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Refaire le son : choix de la graine"), nullptr, ImGuiWindowFlags_NoResize)) {
        Song target;
        for (const Song & s : studio->snapshot()) {
            if (s.id == rerender_id) target = s;
        }
        muted_wrapped(trf("Le re-rendu garde la composition de « %s » (mélodie, paroles, structure) et refait seulement la synthèse audio.", target.title.c_str()));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::BeginDisabled(!target.has_used_seeds);
        if (accent_button(tr("Garder la même graine"), ImVec2(w, 36 * g_dpi), &P.violet)) {
            rerender(target, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        muted_wrapped(target.has_used_seeds
                          ? trf("Rendu identique à l'original (graine %s), sans l'écrêtage des anciennes versions. Utile pour corriger un ancien morceau.", std::to_string(target.used_seed).c_str())
                          : tr("Indisponible : la graine de ce morceau n'a pas été enregistrée."));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (accent_button(tr("Prendre une graine aléatoire"), ImVec2(w, 36 * g_dpi), &P.accent)) {
            rerender(target, false);
            ImGui::CloseCurrentPopup();
        }
        muted_wrapped(tr("Nouvelle variation du timbre et du mixage, avec la même composition."));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    draw_vram_popup();  // mémoire vidéo insuffisante avant une génération

    // Remplacer les paroles par une structure type
    if (form.pending_structure && !ImGui::IsPopupOpen(tr("Appliquer la structure ?"))) ImGui::OpenPopup(tr("Appliquer la structure ?"));
    ImGui::SetNextWindowSize(ImVec2(440 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Appliquer la structure ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("Les paroles actuelles seront remplacées par la structure « %s », avec des lignes vides à compléter."),
                           form.pending_name.c_str());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            form.pending_structure = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Remplacer les paroles"))) {
            form.lyrics_song       = form.pending_text;
            form.pending_structure = false;
            form.dirty             = true;
            ImGui::ClearActiveID();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Enregistrer un préréglage de style
    if (open_preset) {
        ImGui::OpenPopup(tr("Enregistrer le préréglage"));
        open_preset = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Enregistrer le préréglage"), nullptr, ImGuiWindowFlags_NoResize)) {
        muted_wrapped(form.style);
        // Ce que le préréglage emportera en plus du style
        std::vector<LoraUse> chosen;
        for (const LoraUse & u : form.loras) {
            if (u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE) chosen.push_back(u);
        }
        std::string tags = structure_tags(form.instrumental ? form.lyrics_inst : form.lyrics_song);
        if (!chosen.empty()) {
            std::string names;
            for (const LoraUse & u : chosen) names += (names.empty() ? "" : ", ") + lora_label(u.file);
            ImGui::Checkbox(trf("Inclure les LoRA cochées et leur force (%s)", names.c_str()).c_str(), &preset_with_loras);
        }
        if (!tags.empty()) {
            std::string shown;
            for (char c : tags) shown += c == ',' ? std::string(" > ") : std::string(1, c);
            ImGui::Checkbox((form.instrumental ? trf("Inclure la structure instrumentale (%s)", shown.c_str()) : trf("Inclure la structure des paroles (%s)", shown.c_str())).c_str(),
                            &preset_with_structure);
        }
        std::string negative = join_style(split_style(form.negative_style));
        if (!negative.empty()) {
            ImGui::Checkbox(trf("Inclure le style à éviter (%s)", negative.c_str()).c_str(), &preset_with_negative);
        }
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ImGui::InputTextWithHint("##preset", tr("Nom du préréglage (ex. : Cold wave 80s)"), &preset_name,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        text_context_menu(preset_name);
        bool valid = !trim(preset_name).empty();
        if ((modal_buttons(tr("Enregistrer"), valid) || (enter && valid))) {
            Preset np;
            np.name  = trim(preset_name);
            np.style = form.style;
            if (preset_with_loras) np.loras = chosen;
            if (preset_with_structure && !tags.empty()) (form.instrumental ? np.structure_inst : np.structure_song) = tags;
            // Style à éviter : mémorisé si la case est cochée ; un champ vide est mémorisé aussi
            // (choisir ce préréglage videra alors le champ).
            np.has_negative = negative.empty() || preset_with_negative;
            if (!negative.empty() && preset_with_negative) {
                np.negative_style = negative;
                np.negative_scale = form.negative_scale;
            }
            bool replaced = false;
            for (Preset & p : settings.presets) {
                if (p.name == np.name && p.pack.empty()) {
                    // Remplacement : on garde la structure de l'autre mode si celle-ci n'est pas réenregistrée
                    if (np.structure_song.empty()) np.structure_song = p.structure_song;
                    if (np.structure_inst.empty()) np.structure_inst = p.structure_inst;
                    p        = np;
                    replaced = true;
                }
            }
            if (!replaced) settings.presets.push_back(np);
            save_settings(settings, settings_path);
            notify(trf("Préréglage « %s » enregistré.", trim(preset_name).c_str()));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Prolonger un morceau : nouveau morceau qui reprend l'original et le continue
    if (open_extend) {
        ImGui::OpenPopup(tr("Prolonger le morceau"));
        open_extend = false;
    }
    ImGui::SetNextWindowSize(ImVec2(540 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Prolonger le morceau"), nullptr, ImGuiWindowFlags_NoResize)) {
        Song parent;
        for (const Song & s : studio->snapshot()) {
            if (s.id == extend_id) parent = s;
        }
        int max_extra  = std::max(10, 360 - (int) parent.audio_seconds);
        extend_seconds = std::clamp(extend_seconds, 10, max_extra);
        muted_wrapped(trf("Le nouveau morceau reprend « %s » (%s) puis compose la suite dans le même style.", parent.title.c_str(),
                          fmt_time(parent.audio_seconds).c_str()));
        ImGui::TextColored(P.muted, tr("Nom du nouveau morceau"));
        input_line("##ext_title", tr("Nom"), extend_title);
        ImGui::TextColored(P.muted, tr("Durée ajoutée"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##ext_sec", &extend_seconds, 10, max_extra, "%d s");
        ImGui::TextColored(P.muted, parent.instrumental ? tr("Structure de la suite") : tr("Paroles de la suite"));
        ImGui::InputTextMultiline("##ext_lyrics", &extend_lyrics, ImVec2(-FLT_MIN, 160 * g_dpi));
        text_context_menu(extend_lyrics);
        if (modal_buttons(tr("Lancer la prolongation"), !parent.id.empty() && !trim(extend_title).empty(), &P.violet)) {
            Song e;
            e.kind              = "extend";
            e.parent            = parent.id;
            e.parent_title      = parent.title;
            e.title             = trim(extend_title);
            e.style             = parent.style;
            e.instrumental      = parent.instrumental;
            e.instrumental_lora = parent.instrumental_lora;
            e.adapters          = parent.adapters;
            e.negative_style    = parent.negative_style;  // la suite évite le même style
            e.negative_scale    = parent.negative_scale;
            e.cot               = "off";
            e.artist            = parent.artist;
            e.album             = parent.album;
            // Les paroles complètes (originales + suite) accompagnent les codes du morceau d'origine.
            e.lyrics           = parent.lyrics + (parent.instrumental ? "\n" : "\n\n") + extend_lyrics;
            e.duration         = std::min(360.0, parent.audio_seconds + extend_seconds);
            e.expected_seconds = e.duration;
            studio->submit(e);
            notify(trf("Prolongation de « %s » ajoutée à la file.", parent.title.c_str()));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Quitter pendant une génération
    if (g_want_close && !ImGui::IsPopupOpen(tr("Quitter ?"))) ImGui::OpenPopup(tr("Quitter ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Quitter ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("Une génération est en cours : elle sera interrompue et vous pourrez la relancer plus tard. Les morceaux en file sont conservés et reviendront en pause."));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Rester"))) {
            g_want_close = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Quitter"), ImVec2(0, 0), &P.danger)) {
            g_want_close  = false;
            g_force_close = true;
            ImGui::CloseCurrentPopup();
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------------ Image
void App::update_tray(const std::vector<Song> & songs) {
    int running = 0, waiting = 0, held = 0;
    any_active = downloads_running();  // la progression d'un téléchargement demande des images régulières
    update_downloads();
    poll_language_check();
    any_active |= lang_running;
    poll_translation();
    any_active |= translate_job != nullptr;  // barre de progression de la traduction
    for (const Song & s : songs) {
        any_active |= s.status == "running" || s.waiting();
        running += s.status == "running";
        waiting += s.waiting();
        held += s.status == "queued" && s.held;
        if (s.local && s.active()) {
            watched[s.id] = true;
        } else if (watched.count(s.id) && !s.active()) {
            watched.erase(s.id);
            if (s.status == "done") {
                unseen_ready++;
                if (GetForegroundWindow() != hwnd) {
                    FLASHWINFO flash{ sizeof(flash), hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0 };
                    FlashWindowEx(&flash);
                }
            }
        }
    }
    if (GetForegroundWindow() == hwnd && !IsIconic(hwnd)) unseen_ready = 0;  // vus
    // 1.0.40 : l'icône principale ne parle que de la file (en cours + en attente) ; les terminés ont la leur.
    std::string tip = APP_NAME;
    if (running + waiting == 0) tip += " · " + std::string(tr("File vide"));
    else tip += " · " + trf("File : %d morceau(x)", running + waiting);
    if (running) tip += " · " + trf("%d en cours", running);
    if (waiting) tip += " · " + trf("%d en attente", waiting);
    if (held) tip += " · " + trf("%d en pause", held);
    int unheard = 0;  // 1.0.46 : terminés pas encore écoutés (comme « Lire les nouveaux »)
    for (const Song & s : songs) unheard += s.status == "done" && !s.heard;
    if (unheard) tip += " · " + trf("Non écoutés : %d", unheard);
    tray_update(hwnd, settings.notifications, running + waiting, unheard, tip);
    // Application réduite : jauges CPU / GPU / mémoire GPU et compteurs à côté de l'horloge.
    GpuMem      m = gpu_monitor.get();
    std::string tg = trf("CPU %s · GPU %s", m.cpu < 0 ? "?" : (std::to_string((int) std::lround(m.cpu)) + " %").c_str(),
                         m.gpu < 0 ? "?" : (std::to_string((int) std::lround(m.gpu)) + " %").c_str());
    if (m.ok && m.total) {
        char mem[64];
        snprintf(mem, sizeof(mem), "%.1f / %.1f", m.used / 1073741824.0, m.total / 1073741824.0);
        tg += " · " + trf("Mémoire GPU %s Go", mem);
    }
    std::string tc = trf("Terminés : %d", unseen_ready);
    tray_gauges_update(hwnd, settings.tray_gauges && IsIconic(hwnd), m, unseen_ready, tg, tc);
}

  // onglet affiché par draw_form (0 Nouveau morceau, 1 Composer avec l'IA, 2 Partition)
void App::draw_nav(const std::vector<Song> & songs) {
    int running = 0, waiting = 0;
    for (const Song & s : songs) {
        running += s.status == "running";
        waiting += s.waiting();
    }
    const ImVec2 item(0, std::round(30 * g_dpi));
    auto group = [&](const char * label) {
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        ImGui::TextColored(P.muted, "%s", label);
    };
    // 1.1.011 : point lumineux à droite de l'entrée quand un travail correspondant est en cours
    const bool jobs_busy = std::any_of(jobs.begin(), jobs.end(), [](const std::shared_ptr<Job> & j) { return j->state == 0; });
    const bool covers_busy = (cover_job && cover_job->state == 0) || desc_job != nullptr;
    const bool ai_busy     = ai_job != nullptr || ai_album != nullptr;
    const bool score_busy  = plan_job != nullptr && !plan_job->done;
    const bool dl_busy     = downloads_running();
    const bool help_busy   = help_job && !help_job->finished;
    any_active |= jobs_busy || covers_busy || ai_busy || score_busy || help_busy;
    auto entry = [&](const char * label, bool on, bool busy = false) {
        ImGui::PushStyleColor(ImGuiCol_Header, P.accent_soft);
        const bool clicked = ImGui::Selectable(label, on, 0, item);
        ImGui::PopStyleColor();
        if (busy) {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const ImVec2 c(b.x - 14 * g_dpi, (a.y + b.y) * 0.5f);
            const float  pulse = 0.55f + 0.45f * std::sin((float) ImGui::GetTime() * 4.f);
            ImDrawList * dl    = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(c, 9 * g_dpi, ImGui::GetColorU32(alpha(P.ok, 0.22f * pulse)));
            dl->AddCircleFilled(c, 6 * g_dpi, ImGui::GetColorU32(alpha(P.ok, 0.45f)));
            dl->AddCircleFilled(c, 3.8f * g_dpi, ImGui::GetColorU32(P.ok));
        }
        if (on) {  // trait de couleur à gauche de l'entrée choisie
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + 3 * g_dpi, b.y), ImGui::GetColorU32(P.accent));
        }
        return clicked;
    };
    auto go_form = [&](int tab) { b_page = 0; form_tab_request = tab; };
    group(tr("Créer"));
    if (entry(tr("Nouveau morceau"), b_page == 0 && form_shown == 0)) go_form(0);
    if (entry(tr("Composer avec l'IA"), b_page == 0 && form_shown == 1, ai_busy)) go_form(1);
    if (entry(tr("Partition"), b_page == 0 && form_shown == 2, score_busy)) go_form(2);
    group(tr("Bibliothèque"));
    const std::string lib = trf("Morceaux (%d)", (int) songs.size());
    if (entry(lib.c_str(), b_page == 1 && lib_tab == 0, running + waiting > 0 || jobs_busy)) b_page = 1, lib_tab = 0, lib_tab_request = 0;
    if (entry(tr("Albums"), b_page == 1 && lib_tab == 1, covers_busy || jobs_busy)) b_page = 1, lib_tab = 1, lib_tab_request = 1;  // 1.1.005 : onglet de la page
    if (running + waiting > 0) {
        const std::string q = trf("File : %d en cours, %d en attente", running, waiting);
        ImGui::TextColored(running ? P.accent : P.blue, "● %s", q.c_str());
        if (ImGui::IsItemClicked()) b_page = 1, lib_tab = 0, lib_tab_request = 0;
    }
    group(tr("Aide et réglages"));
    if (entry(tr("Réglages"), b_page == 2, dl_busy)) open_settings = true;  // 1.1.005 : page, catégories dessous
    if (b_page == 2) {
        ImGui::Indent(14 * g_dpi);
        for (int i = 0; i < SET_CAT_COUNT; i++) {
            ImGui::PushID(i);
            if (entry(tr(SET_CATS[i]), settings_cat == i)) settings_cat = i;
            ImGui::PopID();
        }
        ImGui::Unindent(14 * g_dpi);
    }
    if (entry(tr("Aide"), b_page == 3, help_busy)) open_help();
    if (entry(tr("Question…"), false)) open_helpq = true;
}

// Disposition A (1.1.002) : lecteur fixe en bas de la fenêtre, pour le morceau chargé dans le lecteur ; il reste visible
// quand on fait défiler la bibliothèque. Même commande que la carte (draw_player), plus « Paroles ».
void App::draw_fixed_player(const std::vector<Song> & songs, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 9 * g_dpi));
    ImGui::BeginChild("player_bar", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const Song * cur = nullptr;
    if (!player.id().empty())
        for (const Song & s : songs)
            if (s.id == player.id()) cur = &s;
    if (!cur) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Aucun morceau en lecture : « ▶ Lecture » sur une carte pour écouter."));
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(g_bold);
        ImGui::TextUnformatted(ellipsize(display_title(cur->title), 240 * g_dpi).c_str());
        ImGui::PopFont();
        if (!cur->artist.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", ellipsize(cur->artist, 160 * g_dpi).c_str());
        }
        ImGui::SameLine(0, 14 * g_dpi);
        ImGui::PushID("fixed_player");
        lyrics_button(*cur);
        ImGui::SameLine();
        draw_player(*cur);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void App::frame() {
    if (tags_dirty) {  // 1.1.011 : balises de style (Réglages › Styles)
        tags_dirty = false;
        rebuild_style_tags(settings);
    }
    if (theme_pending) {
        theme_pending = false;
        apply_theme(g_system_light, g_dpi);
        BOOL dark_title = theme_is_dark(g_theme, g_system_light);
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_title, sizeof(dark_title));
    }
    update_imports();  // packs .yue2pack à importer
    std::vector<Song> songs = studio->snapshot();
    static const bool test_shot_on = GetEnvironmentVariableW(L"STUDIO_TEST_SHOT", nullptr, 0) > 0;
    if (test_shot_on) for (Song & s : songs) s.style = test_clean_style(s.style);
    refresh_album_dirs(songs);  // 1.0.43 : dossiers des albums (pochettes)
    update_tray(songs);
    update_games();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) open_help();  // F1 : aide
    update_autoplay();
    if (driver_update->state == 3 && !driver_update_announced) {
        driver_update_announced = true;
        notify(trf("Mise à jour du pilote graphique disponible (AMD Software %s) : voir Réglages › Carte graphique.", driver_update->latest.c_str()));
    }
    update_jobs();
    update_health();

    const ImGuiViewport * vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Studio YuE2", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    // En-tête de l'application
    const float head_y = ImGui::GetCursorPosY(), head_h = gauge_height();
    ImGui::SetCursorPosY(head_y + std::max(0.f, (head_h - ImGui::GetFrameHeight()) * .5f));
    ImGui::PushFont(g_header);
    ImGui::TextColored(P.accent, "Studio");
    ImGui::SameLine(0, 5 * g_dpi);
    ImGui::TextUnformatted("Audio AI");
    ImGui::PopFont();
    draw_version_badge();
    ImGui::SameLine(0, 18 * g_dpi);
    ImGui::TextColored(P.muted, tr("Modèle"));
    ImGui::SameLine(0, 6 * g_dpi);
    draw_model_combo("##modele_rapide", 120 * g_dpi, false);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("Modèle utilisé pour les prochains morceaux (Q8_0 = qualité maximale, Q6_K et Q5_K_M = plus légers).\nLe modèle par défaut au démarrage se règle dans Réglages."));
    }
    ImGui::SameLine(0, 14 * g_dpi);
    ImGui::TextColored(P.muted, tr("Solveur"));
    ImGui::SameLine(0, 6 * g_dpi);
    draw_solver_combo("##solveur_rapide", 150 * g_dpi, false);
    const float gauges_x = ImGui::GetItemRectMax().x + 16 * g_dpi;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("Solveur de la phase audio (NAR) pour les prochains morceaux :\nAB3 16, AB3 20, AB3 24, Euler 32, AB2 32, AB3 32, MP 32 (méthode + nombre de pas).\nMoins d'évaluations du réseau = génération plus rapide ; comparez à l'écoute.\nLe solveur par défaut au démarrage se règle dans Réglages."));
    }
    {
        const char * text = tr("Pastilles de notification");
        float        w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(text).x;
        const char * rb = tr("Réglages…");
        const char * hb = tr("Aide");
        const char * qb = tr("Question…");
        const float  qw = ImGui::CalcTextSize(qb).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;  // 1.0.98
        float        bw = ImGui::CalcTextSize(rb).x + ImGui::GetStyle().FramePadding.x * 2;
        float        hw = ImGui::CalcTextSize(hb).x + ImGui::GetStyle().FramePadding.x * 2;
        const float right_x = ImGui::GetWindowWidth() - w - bw - hw - qw - ImGui::GetStyle().ItemSpacing.x * 2 - ImGui::GetStyle().WindowPadding.x;
        ImGui::SameLine(right_x);
        if (soft_button(qb, P.teal, false)) open_helpq = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Posez une question : l'IA locale répond d'après l'aide intégrée."));
        ImGui::SameLine();
        if (soft_button(hb, P.teal, false)) open_help();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Ouvre l'aide détaillée de l'application (touche F1)."));
        ImGui::SameLine();
        if (soft_button(rb, driver_changed ? P.warn : P.accent, false)) open_settings = true;
        if (driver_changed && ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Le pilote graphique a changé : un test de santé est conseillé."));
        ImGui::SameLine();
        if (ImGui::Checkbox(text, &settings.notifications)) save_settings(settings, settings_path);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Icône à côté de l'horloge : pastille rouge = morceaux prêts,\npastille violette = générations en cours ou en attente."));
        }
        const float win_x = ImGui::GetWindowPos().x, win_y = ImGui::GetWindowPos().y;
        draw_gauges(ImVec2(gauges_x, win_y + head_y), win_x + right_x - 12 * g_dpi - gauges_x, head_h);
    }

    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, head_y + head_h));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y - (ImGui::GetTime() < toast_until ? ImGui::GetFrameHeightWithSpacing() : 0);
    float left_w  = std::floor(avail_w * 0.47f);
    // Dispositions A et B (1.1.002, 1.1.003) : lecteur fixe en bas de la fenêtre
    const float player_h = settings.layout >= 1 ? ImGui::GetFrameHeight() + 2 * 9 * g_dpi + ImGui::GetStyle().ItemSpacing.y : 0;
    avail_h -= player_h;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 12 * g_dpi));
    // Disposition B : une action qui remplit le formulaire (« Modifier et renvoyer », « Utiliser cette partition »…) ouvre la page de création
    if (settings.layout == 2 && (form_tab_request >= 0 || scroll_form_top)) b_page = 0;
    if (settings.layout == 2) {
        // Disposition B (1.1.003) : menu à gauche ; la page choisie (création ou bibliothèque) prend toute la largeur.
        ImGui::BeginChild("nav", ImVec2(std::floor(210 * g_dpi), avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_nav(songs);
        ImGui::EndChild();
        ImGui::SameLine();
        if (b_page == 0) {
            ImGui::BeginChild("form", ImVec2(0, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            draw_form();
        } else if (b_page == 2) {  // 1.1.005 : Réglages en page
            ImGui::BeginChild("settings_page", ImVec2(0, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            draw_settings_body(true);
        } else if (b_page == 3) {  // 1.1.005 : Aide en page
            ImGui::BeginChild("help_page", ImVec2(0, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            draw_help_page();
        } else {
            ImGui::BeginChild("library", ImVec2(0, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            // 1.1.005 : deux onglets, Morceaux et Albums (ex-fenêtre « Albums… »), chacun en pleine page
            if (ImGui::BeginTabBar("##lib_tabs")) {
                const bool to0 = lib_tab_request == 0, to1 = lib_tab_request == 1;
                lib_tab_request = -1;
                if (ImGui::BeginTabItem((trf("Morceaux (%d)", (int) songs.size()) + "###lib_songs").c_str(), nullptr, to0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                    lib_tab = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(tr("Albums"), nullptr, to1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                    lib_tab = 1;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (lib_tab == 1) draw_albums_body(true);
            else draw_library(songs);
        }
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("form", ImVec2(left_w, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_form();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("library", ImVec2(0, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_library(songs);
        ImGui::EndChild();
    }
    ImGui::PopStyleVar();
    if (settings.layout >= 1) draw_fixed_player(songs, player_h - ImGui::GetStyle().ItemSpacing.y);
    if (settings.layout != 2 || b_page != 3) helpview_release();  // 1.1.005 : page Aide quittée : navigateur libéré (mémoire)

    if (ImGui::GetTime() < toast_until) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.accent, "●");
        ImGui::SameLine();
        ImGui::TextUnformatted(toast.c_str());
    }
    draw_modals();
    ImGui::End();

    if (form.dirty && ImGui::GetTime() - last_draft_save > 1.0) {
        save_draft(form, draft_path);
        last_draft_save = ImGui::GetTime();
    }
}
