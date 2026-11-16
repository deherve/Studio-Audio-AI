// Albums, aperçus, tri (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

// Essai : les captures de l'aide ne montrent aucun nom de personne réelle (balises de plusieurs mots à majuscule)
std::string App::test_clean_style(const std::string & style) {
    std::vector<std::string> kept;
    for (const std::string & tag : split_style(style))
        if (!(isupper((unsigned char) tag[0]) && tag.find(' ') != std::string::npos)) kept.push_back(tag);
    return join_style(kept);
}

void App::delete_album() {
    fs::path dir = album_folder(delete_album_artist, delete_album_name);
    for (const Song & s : studio->snapshot())
        if (!s.album.empty() && album_key(s.artist, s.album) == delete_album_key && player.id() == s.id) player.close();
    std::string err;
    if (!studio->remove_folder(dir, err)) {
        notify(err);
        return;
    }
    for (const std::string & suffix : { std::string(), std::string("#youtube"), std::string("#tiktok") }) settings.covers.erase(delete_album_key + suffix);
    settings.cover_prompts.erase(delete_album_key);
    settings.album_sounds.erase(delete_album_key);
    if (!ai_album_name.empty() && album_key(ai_album_band, ai_album_name) == delete_album_key) {
        ai_album_band.clear();
        ai_album_name.clear();
        ai_album_titles.clear();
        save_last_album();
    }
    save_settings(settings, settings_path);
    exists_cache.clear();
    notify(trf("Album « %s » envoyé à la corbeille.", delete_album_name.c_str()));
}

void App::draw_album_tool_modals() {
    // Aperçu agrandi : la plus grande taille qui tient dans 85 % de la fenêtre ; clic ou Échap pour fermer.
    const char * pv = "##apercu_pochette";
    if (preview_open) {
        ImGui::OpenPopup(pv);
        preview_open = false;
    }
    ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(pv, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImTextureID tex = 0;
        ImVec2      tsize;
        if (thumb_texture(preview_image, tex, tsize, 1600)) {
            float k = std::min({ vp.x * 0.85f / tsize.x, vp.y * 0.85f / tsize.y, 1.f });
            ImGui::Image(tex, ImVec2(tsize.x * k, tsize.y * k));
            if (ImGui::IsItemClicked()) ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextUnformatted(tr("Image illisible."));
        }
        ImGui::TextColored(P.muted, "%s", tr("Cliquez sur l'image ou appuyez sur Échap pour fermer."));
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Toutes les pochettes : certaines existent déjà
    const char * ac = tr("Toutes les pochettes de l'album");
    if (all_covers_open) {
        ImGui::OpenPopup(ac);
        all_covers_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(480 * g_dpi, 0));
    if (ImGui::BeginPopupModal(ac, nullptr, ImGuiWindowFlags_NoResize)) {
        int existing = 0;
        for (const CoverTask & t : all_covers_tasks) existing += !cover_for(t.slot).empty();
        ImGui::TextWrapped("%s", trf("L'album « %s » a déjà %d image(s) sur 3. Créer seulement les images manquantes, ou tout refaire ?",
                                     all_covers_label.c_str(), existing).c_str());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(existing == (int) all_covers_tasks.size());
        if (ImGui::Button(tr("Seulement les manquantes"))) {
            std::vector<CoverTask> missing;
            for (const CoverTask & t : all_covers_tasks)
                if (cover_for(t.slot).empty()) missing.push_back(t);
            start_covers(missing);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (accent_button(tr("Tout remplacer"), ImVec2(0, 0), &P.pink)) {
            start_covers(all_covers_tasks);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // Suppression d'un album : tout son dossier à la corbeille de Windows
    const char * da = tr("Supprimer l'album ?");
    if (delete_album_open) {
        ImGui::OpenPopup(da);
        delete_album_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
    if (ImGui::BeginPopupModal(da, nullptr, ImGuiWindowFlags_NoResize)) {
        fs::path        dir = album_folder(delete_album_artist, delete_album_name);
        int             songs = 0, files = 0;
        std::error_code ec;
        for (const Song & s : studio->snapshot()) songs += !s.album.empty() && album_key(s.artist, s.album) == delete_album_key;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            files += it->is_regular_file(ec);
        ImGui::TextWrapped("%s", trf("L'album « %s » (%d morceau(x), %d fichier(s) : audio, pochettes, vidéos, exports) sera envoyé à la corbeille de Windows : vous pourrez l'y récupérer.",
                                     delete_album_name.c_str(), songs, files).c_str());
        path_line(dir);
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (accent_button(tr("Mettre à la corbeille"), ImVec2(0, 0), &P.danger)) {
            delete_album();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::rename_album(const std::string & old_key, const std::string & artist, const std::string & album) {
    std::string new_key = album_key(artist, album);
    fs::path    old_dir;
    for (const Song & s : studio->snapshot())
        if (!s.album.empty() && album_key(s.artist, s.album) == old_key) {
            if (old_dir.empty()) old_dir = album_folder(s.artist, s.album);
            studio->edit_metadata(s.id, s.title, artist, album, s.track);  // le dossier du morceau suit (1.0.43)
        }
    // 1.0.43 : pochettes et vidéos de l'album suivent ; les dossiers de morceaux pas encore déplaçables (en file)
    // restent et suivront à leur prochain enregistrement.
    fs::path        new_dir = album_folder(artist, album);
    std::error_code ec;
    if (!old_dir.empty() && old_dir != new_dir && fs::is_directory(old_dir, ec)) {
        std::vector<fs::path> rest;
        for (const auto & e : fs::directory_iterator(old_dir, ec))
            if (!fs::exists(e.path() / L"meta.json", ec)) rest.push_back(e.path());
        for (const fs::path & p : rest) merge_folder(p, new_dir / p.filename());
        // chemins mémorisés des images de l'album : même fichier dans le nouveau dossier
        std::wstring old_prefix = old_dir.wstring() + L"\\";
        for (auto & [slot, path] : settings.covers) {
            std::wstring w = widen(path);
            if (w.rfind(old_prefix, 0) == 0 && fs::exists(new_dir / w.substr(old_prefix.size()), ec))
                path = narrow((new_dir / w.substr(old_prefix.size())).wstring());
        }
    }
    exists_cache.clear();
    if (new_key != old_key) {
        for (const std::string & suffix : { std::string(), std::string("#youtube"), std::string("#tiktok") }) {
            auto it = settings.covers.find(old_key + suffix);
            if (it == settings.covers.end()) continue;
            // album fusionné avec un album existant : ses propres images sont gardées
            if (settings.covers.find(new_key + suffix) == settings.covers.end()) settings.covers[new_key + suffix] = it->second;
            settings.covers.erase(it);
        }
        for (auto * m : { &settings.cover_prompts, &settings.album_sounds }) {  // 1.0.47 : le son de l'album suit aussi
            auto p = m->find(old_key);
            if (p == m->end()) continue;
            std::string v = p->second;
            m->erase(p);
            if (trim((*m)[new_key]).empty()) (*m)[new_key] = v;
        }
    }
    if (!ai_album_name.empty() && album_key(ai_album_band, ai_album_name) == old_key) {
        ai_album_band = artist;
        ai_album_name = album;
        save_last_album();
    }
    save_settings(settings, settings_path);
    notify(trf("Album renommé : « %s » (%s).", album.c_str(), artist.empty() ? tr("(sans groupe)") : artist.c_str()));
}

void App::draw_rename_album_modal() {
    const char * title = tr("Renommer l'album");
    if (rename_open) {
        ImGui::OpenPopup(title);
        rename_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextColored(P.muted, "%s", tr("Groupe / chanteur"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    tx::InputText("##rename_artist", &rename_artist);
    ImGui::TextColored(P.muted, "%s", tr("Titre de l'album"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    tx::InputText("##rename_album", &rename_title);
    muted_wrapped(tr("Les étiquettes des morceaux sont réécrites ; les pochettes et les images des vidéos sont gardées."));
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    if (modal_buttons(tr("Renommer"), !trim(rename_title).empty())) {
        rename_album(rename_key, trim(rename_artist), trim(rename_title));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::draw_albums_modal() {
    if (open_albums && settings.layout == 2) {  // 1.1.005 : onglet Albums de la page Bibliothèque
        open_albums = false;
        b_page      = 1;
        lib_tab     = 1;
        lib_tab_request = 1;
    }
    if (open_albums) {
        ImGui::OpenPopup(tr("Albums"));
        open_albums = false;
    }
    albums_drawn = false;
    ImGui::SetNextWindowSize(ImVec2(1080 * g_dpi, 0));  // 1.0.51 : plus large pour la ligne compacte (voyants + actions)
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));  // 1.0.78 : reste centrée (Studio agrandi, rétabli)
    if (!ImGui::BeginPopupModal(tr("Albums"), nullptr, ImGuiWindowFlags_NoResize)) return;
    albums_drawn = true;
    draw_albums_body(false);
    ImGui::EndPopup();
}

// 1.1.005 : contenu de « Albums », dans la fenêtre (page = false) ou dans l'onglet Albums de la Bibliothèque (disposition B :
// la liste prend toute la hauteur ; la fenêtre « Créer des vidéos » et l'écran des pochettes sont dessinés au niveau principal).
void App::draw_albums_body(bool page) {
    std::vector<Song> all    = studio->snapshot();
    auto              albums = list_albums(all);
    muted_wrapped(tr("Un album regroupe les morceaux terminés ayant le même groupe et le même titre d'album (renseignés à l'import ou avec « Modifier »). La pochette (carrée) est intégrée aux fichiers exportés ; les images YouTube (16:9) et TikTok (9:16) servent aux vidéos."));
    ImGui::Checkbox(tr("Seulement les morceaux validés"), &exp_validated);
    ImGui::SameLine(0, 18 * g_dpi);
    ImGui::TextColored(P.muted, tr("Formats d'export"));
    ImGui::SameLine();
    ImGui::Checkbox("FLAC", &exp_flac);
    ImGui::SameLine();
    ImGui::Checkbox("MP3", &exp_mp3);
    ImGui::SameLine();
    ImGui::Checkbox("WAV", &exp_wav);
    {  // 1.0.82 : Shorts manquants de toute la bibliothèque, en une seule file ; 1.0.83 : contrôle qualité
        const char * lib_label = tr("Shorts manquants (bibliothèque)…");
        const char * qc_label  = tr("Contrôle qualité (bibliothèque)");
        const char * dup_label = tr("Doublons…");
        const float  pad       = ImGui::GetStyle().FramePadding.x * 2;
        const bool   busy      = studio->local_active() || ai_local_busy();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(lib_label).x - ImGui::CalcTextSize(qc_label).x - ImGui::CalcTextSize(dup_label).x - pad * 3 -
                        ImGui::GetStyle().ItemSpacing.x * 2);
        if (soft_button(dup_label, P.pink, false)) start_dups(all);  // 1.0.97
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Cherche les morceaux dont la composition ou les paroles sont presque identiques (rapport « doublons.txt »)."));
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (soft_button(qc_label, P.amber, false)) start_quality_check(all, std::string(), tr("Toute la bibliothèque"));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", busy ? tr("Le contrôle se fait au repos : attendez la fin de la génération en cours.")
                                         : tr("Sans réécoute : durée, paroles chantées (Whisper), silences anormaux, partie trop courte. Les morceaux à refaire reçoivent un pouce rouge ; rapport « controle_qualite.txt »."));
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (soft_button(lib_label, P.teal, false)) ask_missing_shorts(all, std::string(), tr("Toute la bibliothèque"));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", busy ? tr("Les images des clips se font au repos : attendez la fin de la génération en cours.")
                                         : tr("Crée un Short pour chaque morceau terminé qui n'en a pas encore, dans tous les albums."));
    }
    if (!quality_test.empty() && !all.empty()) {
        start_quality_check(all, quality_test, quality_test);
        quality_test.clear();
    }
    if (shorts_test && !all.empty()) {  // essai : bibliothèque lue
        shorts_test = false;
        ask_missing_shorts(all, std::string(), tr("Toute la bibliothèque"));
    }
    draw_shorts_confirm();
    ImGui::BeginChild("albums_list", ImVec2(0, page ? 0.f : 470 * g_dpi), ImGuiChildFlags_Borders);
    if (albums.empty()) {
        ImGui::Dummy(ImVec2(0, 20 * g_dpi));
        muted_wrapped(tr("Aucun album pour l'instant."));
    }
    if (cover_job) draw_cover_progress();  // 1.0.36
    if (desc_job) {  // 1.0.57
        std::string cur;
        {
            std::lock_guard<std::mutex> lock(desc_job->mutex);
            cur = desc_job->current;
        }
        progress_bar((float) desc_job->done / (float) desc_job->total, ImVec2(-FLT_MIN, 0), trf("Description : %s", cur.c_str()).c_str());
        if (soft_button(tr("Arrêter"), P.danger)) desc_job->cancel = true;
    }
    // 1.0.51 : une ligne compacte par album (vignette, voyants des images et des vidéos, actions) ; un clic sur la flèche
    // ou la vignette déplie la fiche à onglets « Pochettes » / « Vidéos ». Une seule fiche dépliée à la fois.
    for (const AlbumInfo & a : albums) {
        ImGui::PushID(a.key.c_str());
        // 1.0.36 : description de l'image (écrite par l'IA du plan d'album, sinon tirée du style), modifiable dans l'onglet
        std::string       prompt      = cover_prompt_for(a.key, a.album, a.style);
        fs::path          vdir        = album_folder(a.artist, a.album) / L"videos";
        const std::string slots[3]    = { a.key, slot_youtube(a.key), slot_tiktok(a.key) };
        const char *      img_tags[3] = { "1:1", "16:9", "9:16" };
        // sous-dossiers écrits par les vidéos (1.0.43) ; 1.0.79 : 4e format, les Shorts (videos\Shorts)
        const wchar_t *   vid_dirs[4] = { L"YouTube", L"TikTok", L"IG", L"Shorts" };
        const char *      vid_tags[4] = { "YouTube", "TikTok", "Instagram", "Shorts" };
        int               imgs = 0, vids = 0, vcount[4];
        for (int i = 0; i < 4; i++) {
            if (i < 3) imgs += !cover_for(slots[i]).empty();
            vcount[i] = video_count(vdir / vid_dirs[i]);
            vids += vcount[i] > 0;
        }
        bool open   = album_open == a.key;
        auto toggle = [&] {
            album_open = open ? std::string() : a.key;
            open       = !open;
        };
        float row_y = ImGui::GetCursorPosY();
        if (ImGui::ArrowButton("##ouvrir", open ? ImGuiDir_Down : ImGuiDir_Right)) toggle();
        ImGui::SameLine();
        {  // vignette de la pochette 1:1 (clic = déplier)
            float       t  = 56 * g_dpi;
            ImVec2      p0 = ImGui::GetCursorScreenPos();
            std::string c  = cover_for(a.key);
            if (ImGui::InvisibleButton("##vignette", ImVec2(t, t))) toggle();
            ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(t, t), ImGui::GetColorU32(P.chip), 4 * g_dpi);
            ImTextureID tex = 0;
            ImVec2      ts;
            if (!c.empty() && thumb_texture(fs::path(widen(c)), tex, ts, 256)) ImGui::GetWindowDrawList()->AddImage(tex, p0, p0 + ImVec2(t, t));
        }
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::BeginGroup();
        ImGui::PushFont(g_bold);
        ImGui::TextUnformatted(a.album.c_str());
        ImGui::PopFont();
        ImGui::TextColored(P.blue, "%s", a.artist.empty() ? tr("(sans groupe)") : a.artist.c_str());
        if (!a.artist.empty()) {  // 1.1.011 : style personnalisé au nom du groupe
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Style perso…"))) open_group_style(all, a.artist);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Crée un style personnalisé au nom du groupe, avec les balises communes à ses morceaux."));
        }
        ImGui::TextColored(P.muted, tr("%d terminé(s) · %d validé(s)"), a.done, a.validated);
        if (a.pending > 0) {
            ImGui::SameLine();
            ImGui::TextColored(P.amber, "%s", trf("· en attente : %d", a.pending).c_str());
        }
        ImGui::EndGroup();
        float row_end = ImGui::GetCursorPosY();  // bas de la ligne (vignette ou textes)
        // Voyants : vert = fait, gris = pas encore (positions fixes : deux lignes alignées sous « Images » / « Vidéos »)
        float img_end = 0;  // 1.0.79 : bord droit des voyants « Images » (le bouton « Pochettes » se place après)
        {
            float  lx = 370 * g_dpi, px = lx + 62 * g_dpi, lh = ImGui::GetTextLineHeight() + 8 * g_dpi;
            ImVec2 tip0 = ImGui::GetCursorScreenPos();
            for (int line = 0; line < 2; line++) {
                float y = row_y + 8 * g_dpi + line * lh, x = px;
                ImGui::SetCursorPos(ImVec2(lx, y + 1 * g_dpi));
                ImGui::TextColored(P.muted, "%s", line == 0 ? tr("Images") : tr("Vidéos"));
                for (int i = 0; i < (line == 0 ? 3 : 4); i++) {
                    const char * tag  = line == 0 ? img_tags[i] : vid_tags[i];
                    bool         done = line == 0 ? !cover_for(slots[i]).empty() : vcount[i] > 0;
                    ImGui::SetCursorPos(ImVec2(x, y));
                    pill(tag, done ? P.ok : P.muted);
                    x += pill_width(tag) + 4 * g_dpi;
                }
                if (line == 0) img_end = x;
            }
            (void) tip0;
            ImVec2 win = ImGui::GetWindowPos() - ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            if (ImGui::IsMouseHoveringRect(win + ImVec2(lx, row_y), win + ImVec2(px + 250 * g_dpi, row_y + 8 * g_dpi + 2 * lh)))
                ImGui::SetTooltip("%s", trf("Pochettes %d/3 · Vidéos %d/4", imgs, vids).c_str());
        }
        // Actions courantes à droite ; Renommer, Supprimer et Ouvrir le dossier dans « … »
        // Au bout de la ligne « Images » : 1.0.78 : une pochette manque → « Pochettes » (seulement les manquantes) ;
        // 1.0.80 : les 3 existent → « Refaire les pochettes » (les 3 formats, après confirmation « Tout remplacer »)
        const bool missing = imgs < 3;
        {
            ImGui::SetCursorPos(ImVec2(img_end + 6 * g_dpi, row_y + 6 * g_dpi));
            ImGui::BeginDisabled(cover_job != nullptr);
            if (soft_button(missing ? tr("Pochettes") : tr("Refaire les pochettes"), P.pink, false)) {
                int               skipped = 0;
                const std::string prompt  = album_image_prompt(a.key, a.album, a.artist);
                if (missing) {
                    start_covers(album_cover_tasks(a.key, a.album, prompt, true, true, true, skipped));
                } else {
                    all_covers_tasks = album_cover_tasks(a.key, a.album, prompt, true, true, false, skipped);
                    all_covers_key   = a.key;
                    all_covers_label = a.album;
                    all_covers_open  = true;  // fenêtre de confirmation : « Tout remplacer »
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", missing ? trf("Crée les images manquantes de l'album (%d sur 3) avec le modèle d'images des Réglages.", 3 - imgs).c_str()
                                                : tr("Recrée la pochette 1:1 et les images YouTube 16:9 et TikTok 9:16 de l'album (après confirmation), avec le modèle d'images des Réglages et la description de l'album."));
        }
        ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - 330 * g_dpi, row_y + 14 * g_dpi));
        int usable = exp_validated ? a.validated : a.done;
        ImGui::BeginDisabled(usable == 0);
        if (soft_button(tr("Exporter…"), P.green, false)) export_album(a.key, a.artist, a.album, all);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(usable == 0);
        if (soft_button(tr("Vidéos…"), P.teal, false)) open_video_dialog(album_songs(all, a.key, exp_validated), a.album, a.key);
        ImGui::EndDisabled();
        ImGui::SameLine();
        // 1.0.47 (b2) : ajouter des morceaux à cet album avec « Composer avec l'IA » (même groupe, même son)
        ImGui::BeginDisabled(ai_local_busy());
        if (soft_button(tr("Compléter…"), P.violet, false)) {
            settings.ai_album_mode  = true;
            settings.ai_band        = a.artist;
            settings.ai_album_title = a.album;
            for (const Song & s : all) {  // LoRA du premier morceau de l'album, cochées dans le formulaire
                if (s.album.empty() || album_key(s.artist, s.album) != a.key) continue;
                form.loras.clear();
                for (const auto & [name, scale] : s.adapters) form.loras.push_back({ name, (float) scale, true });
                scan_loras(true);
                sync_lora_triggers();
                break;
            }
            save_settings(settings, settings_path);
            form_tab_request = 1;
            ImGui::CloseCurrentPopup();
            notify(trf("Composer avec l'IA : choisissez le nombre de morceaux à ajouter à « %s », puis lancez l'album.", a.album.c_str()));
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Ajoute des morceaux à cet album avec « Composer avec l'IA » : même groupe, même son, nouveaux titres, pistes à la suite."));
        ImGui::SameLine();
        if (soft_button(" … ", P.muted, false)) ImGui::OpenPopup("##plus");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Plus d'actions"));
        if (ImGui::BeginPopup("##plus")) {
            bool generating = std::any_of(all.begin(), all.end(), [&](const Song & s) {
                return s.status == "running" && !s.album.empty() && album_key(s.artist, s.album) == a.key;
            });
            bool busy_album = std::any_of(all.begin(), all.end(), [&](const Song & s) {
                return s.active() && !s.album.empty() && album_key(s.artist, s.album) == a.key;
            });
            if (ImGui::MenuItem(tr("Renommer…"), nullptr, false, !generating && !cover_job)) {
                rename_key    = a.key;
                rename_artist = a.artist;
                rename_title  = a.album;
                rename_open   = true;
            }
            if (generating && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Un morceau de cet album est en cours de génération."));
            if (ImGui::MenuItem(tr("Ouvrir le dossier de l'album")))
                ShellExecuteW(nullptr, L"open", album_folder(a.artist, a.album).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            {   // 1.1.016 : image de référence du groupe (FLUX.3 klein) pour les pochettes et les clips
                const fs::path ref = a.artist.empty() ? fs::path() : group_reference(a.artist);
                if (ImGui::MenuItem(tr("Image de référence du groupe…"), nullptr, false, !a.artist.empty())) {
                    const fs::path pick = ask_image(wtr("Image de référence du groupe").c_str());
                    if (!pick.empty()) {
                        if (set_group_reference(a.artist, pick)) {
                            journal::event("style", "Image de référence du groupe « " + a.artist + " » : " + narrow(pick.wstring()));
                            notify(trf("Image de référence enregistrée pour « %s ».", a.artist.c_str()));
                        } else {
                            notify(tr("Impossible d'enregistrer l'image de référence."));
                        }
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("Une image (pochette, portrait du chanteur…) copiée dans le dossier du groupe : FLUX.3 klein s'en inspire pour garder le même style et les mêmes sujets dans les prochaines pochettes et dans les images des clips (plus lent : ≈ 90 s par image au lieu de ≈ 40 s)."));
                if (!ref.empty() && ImGui::MenuItem(tr("Retirer l'image de référence"))) {
                    std::string rerr;
                    recycle_to_bin({ ref }, rerr);
                }
            }
            if (ImGui::MenuItem(tr("Contrôle qualité de l'album"), nullptr, false, !ai_local_busy() && !studio->local_active()))
                start_quality_check(all, a.key, a.album);  // 1.0.83
            if (ImGui::MenuItem(tr("Shorts manquants de l'album…"), nullptr, false, !ai_local_busy() && !studio->local_active()))
                ask_missing_shorts(all, a.key, a.album);  // 1.0.82
            if (ImGui::MenuItem(tr("Rédiger les descriptions (IA locale)"), nullptr, false, !ai_local_busy() && !studio->local_active()))
                start_descriptions(a.key, all);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Écrit un fichier « description.txt » pour le groupe, l'album et chaque morceau. Un fichier déjà présent n'est jamais remplacé : supprimez-le pour le refaire."));
            ImGui::Separator();
            draw_tools_menu(done_songs_of(all, a.key), a.album, true);  // 1.0.97
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, P.danger);
            bool del = ImGui::MenuItem(tr("Supprimer l'album…"), nullptr, false, !busy_album && !cover_job);
            ImGui::PopStyleColor();
            if (del) {
                delete_album_key    = a.key;
                delete_album_artist = a.artist;
                delete_album_name   = a.album;
                delete_album_open   = true;
            }
            if (busy_album && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Des morceaux de cet album sont en cours ou en attente de génération : annulez-les d'abord."));
            ImGui::EndPopup();
        }
        ImGui::SetCursorPosY(std::max(row_end, ImGui::GetCursorPosY()));
        if (open && album_scroll) {  // essai : amener l'album déplié en vue
            ImGui::SetScrollHereY(0.1f);
            album_scroll = false;
        }
        if (open) {
            // Onglets : tous déclarés à chaque image, barre fermée, puis le contenu (règle de la 1.0.27)
            std::string t1 = trf("Pochettes %d/3", imgs) + "###pochettes", t2 = trf("Vidéos %d/4", vids) + "###videos";
            if (ImGui::BeginTabBar("##onglets")) {
                if (ImGui::BeginTabItem(t1.c_str(), nullptr, album_tab_request == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                    album_tab = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(t2.c_str(), nullptr, album_tab_request == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                    album_tab = 1;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
                album_tab_request = -1;
            }
            if (album_tab == 0) {
                float h = 120 * g_dpi;
                draw_image_slot(a.key, tr("Pochette 1:1"), h, h, &prompt, a.album);
                ImGui::SameLine(0, 10 * g_dpi);
                draw_image_slot(slot_youtube(a.key), "YouTube 16:9", h * 16 / 9, h, &prompt, a.album);
                ImGui::SameLine(0, 10 * g_dpi);
                draw_image_slot(slot_tiktok(a.key), "TikTok 9:16", h * 9 / 16, h, &prompt, a.album);
                ImGui::SameLine(0, 16 * g_dpi);
                // Description de la pochette (1.0.36), en anglais pour les modèles d'images ; plus haute en 1.0.51
                ImGui::BeginGroup();
                ImGui::TextColored(P.muted, "%s", tr("Description de la pochette (anglais, sans texte dans l'image)"));
                // ImGui 1.91 ne coupe pas les lignes : les espaces deviennent des retours à la ligne (soft_wrap), enregistrés en espaces
                prompt_wrap_w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ScrollbarSize - 4 * g_dpi;
                std::string edit = prompt;
                soft_wrap(edit, prompt_wrap_w);
                auto rewrap = [](ImGuiInputTextCallbackData * d) -> int {
                    std::string text(d->Buf, d->BufTextLen), out = text;
                    soft_wrap(out, *(float *) d->UserData);
                    if (out != text) {  // même longueur : le curseur reste à sa place
                        int c = d->CursorPos;
                        d->DeleteChars(0, d->BufTextLen);
                        d->InsertChars(0, out.c_str());
                        d->CursorPos      = c;
                        d->SelectionStart = d->SelectionEnd = c;
                    }
                    return 0;
                };
                if (tx::InputTextMultiline("##cover_prompt", &edit, ImVec2(-FLT_MIN, h + ImGui::GetFrameHeight()), ImGuiInputTextFlags_CallbackEdit,
                                           rewrap, &prompt_wrap_w)) {
                    std::replace(edit.begin(), edit.end(), '\n', ' ');
                    settings.cover_prompts[a.key] = edit;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
                // 1.0.46 : toutes les pochettes de l'album d'un coup (1:1, YouTube, TikTok) ; 1.0.51 : choix du modèle
                ImGui::BeginDisabled(cover_job != nullptr || trim(prompt).empty());
                if (soft_button((std::string(tr("Toutes les pochettes")) + " ▾").c_str(), P.pink, false)) ImGui::OpenPopup("##toutes");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("Crée la pochette 1:1 et les images YouTube et TikTok de l'album avec sa description."));
                if (ImGui::BeginPopup("##toutes")) {
                    std::string model;
                    if (image_model_menu(model)) {
                        int skipped = 0;
                        all_covers_tasks = album_cover_tasks(a.key, a.album, prompt, true, true, false, skipped);
                        for (CoverTask & t : all_covers_tasks) t.model = model;
                        int existing = 0;
                        for (const CoverTask & t : all_covers_tasks) existing += !cover_for(t.slot).empty();
                        all_covers_key   = a.key;
                        all_covers_label = a.album;
                        if (existing == 0) start_covers(all_covers_tasks);
                        else all_covers_open = true;  // confirmation : remplacer ou seulement les manquantes
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndGroup();
            } else {
                for (int i = 0; i < 4; i++) {
                    ImGui::TextColored(vcount[i] > 0 ? P.ok : P.muted, "●");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(vid_tags[i]);
                    ImGui::SameLine(140 * g_dpi);
                    if (vcount[i] > 0) ImGui::TextColored(P.ok, "%s", trf("%d vidéo(s)", vcount[i]).c_str());
                    else ImGui::TextColored(P.muted, "%s", tr("pas encore créée"));
                }
                ImGui::Dummy(ImVec2(0, 4 * g_dpi));
                bool has_dir = cached_exists(vdir);
                ImGui::BeginDisabled(!has_dir);
                if (soft_button(tr("Ouvrir le dossier des vidéos"), P.teal, false))
                    ShellExecuteW(nullptr, L"open", vdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextColored(P.muted, "%s", tr("Les vidéos se créent avec « Vidéos… »."));
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!page) {
        if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
        draw_video_modal();  // dans la portée de « Albums » : s'ouvre par-dessus au lieu de la fermer
        draw_cover_splash();  // 1.0.37 : au-dessus de « Albums »
    }
    draw_rename_album_modal();  // 1.0.41
    if (!page) draw_ai_style_modal();  // 1.1.011 : « Style perso… » d'un groupe, dans la fenêtre Albums (en page : dessinée plus bas)
    draw_album_tool_modals();   // 1.0.46 : aperçu, toutes les pochettes, suppression
    // « Régénérer » (1.0.36) : remplacer une image existante seulement après confirmation
    const char * regen_title = tr("Remplacer l'image ?");
    if (pending_regen_open) {
        ImGui::OpenPopup(regen_title);
        pending_regen_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(regen_title, nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", trf("L'image « %s » sera remplacée par une nouvelle image créée par l'IA.", pending_regen.label.c_str()).c_str());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (accent_button(tr("Remplacer"), ImVec2(0, 0), &P.pink)) {
            start_covers({ pending_regen });
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

  // dernier appel de remove_inside refusé (déjà signalé à l'écran)
bool App::remove_inside(const fs::path & p, std::error_code & ec) {
    ec.clear();
    remove_refused = false;
    if (!fs::exists(p, ec)) return false;
    if (!inside_app_dir(p)) {
        remove_refused = true;
        notify(trf("Suppression refusée : « %s » est hors du dossier du programme.", narrow(p.wstring()).c_str()));
        return false;
    }
    return fs::remove(p, ec);
}

// Chemin complet affiché dans les confirmations de suppression (1.0.32).
void App::path_line(const fs::path & p) {
    ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
    ImGui::TextWrapped("%s", narrow(p.wstring()).c_str());
    ImGui::PopStyleColor();
}

// Suppression d'un modèle (fichier GGUF) : définitive, il peut être téléchargé de nouveau.
void App::delete_model_file(const std::string & file) {
    std::error_code ec;
    remove_inside(studio->paths().gguf_dir / widen(file), ec);
    if (remove_refused) return;  // déjà signalé
    if (ec) {
        notify(tr("Suppression impossible : ") + narrow(widen(ec.message())));
        return;
    }
    const std::string keep = current_model == file ? std::string() : current_model;
    if (settings.model == file) {
        settings.model.clear();
        save_settings(settings, settings_path);
    }
    init_models();  // liste à jour ; un autre modèle remplace celui qui vient d'être supprimé
    if (!keep.empty() && std::find(models.begin(), models.end(), keep) != models.end()) {
        current_model = keep;
        studio->set_active_model(keep);
    }
    notify(tr("Modèle supprimé : ") + model_label(file));
}

// Suppression des fichiers d'une LoRA (models/adapters), définitive.
void App::delete_lora_files(const std::vector<std::string> & files, const std::string & name) {
    std::error_code ec;
    for (const std::string & f : files) {
        remove_inside(studio->paths().adapters / widen(f), ec);
        if (remove_refused) return;  // déjà signalé
        if (ec) {
            notify(tr("Suppression impossible : ") + narrow(widen(ec.message())));
            break;
        }
    }
    scan_loras(true);
    if (!ec) notify(tr("LoRA supprimée : ") + name);
}

// Petit bouton rouge des tableaux ; grisé pendant une génération (les fichiers sont alors utilisés).
bool App::delete_button(const char * tip) {
    bool busy = studio->local_active();
    ImGui::BeginDisabled(busy);
    bool clicked = soft_button(tr("Supprimer"), P.danger);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", busy ? tr("Indisponible pendant une génération.") : tip);
    }
    return clicked;
}

// Tableau des LoRA du catalogue : téléchargement direct dans models/adapters.
void App::draw_lora_catalog() {
    scan_loras();
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.pink, tr("LoRA"));
    ImGui::PopFont();
    muted_wrapped(tr("Extensions qui orientent le style, la voix ou l'ambiance d'un morceau. Le téléchargement va directement dans le dossier de l'application ; la LoRA se coche ensuite dans le formulaire (plusieurs possibles à la fois). Licences propres à chaque LoRA (le plus souvent usage non commercial)."));
    ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##lora_catalog", 6, tf, ImVec2(0, 230 * g_dpi))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("LoRA"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Type"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
        ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
        ImGui::TableSetupColumn(tr("Mot"), ImGuiTableColumnFlags_WidthFixed, 124 * g_dpi);
        ImGui::TableSetupColumn(tr("Téléchargement"), ImGuiTableColumnFlags_WidthFixed, 210 * g_dpi);
        ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
        ImGui::TableHeadersRow();
        for (const LoraEntry & e : LORA_CATALOG) {
            ImGui::PushID(e.id);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(tr(e.name));
            if (ImGui::IsItemHovered()) {
                std::string tip = std::string(tr(e.description)) + tr("\nAuteur : ") + e.author;
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("↗")) ShellExecuteW(nullptr, L"open", widen(e.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Ouvrir la page de la LoRA (auteur, licence, exemples)"));
            ImGui::TableSetColumnIndex(1);
            const char * kind = !strcmp(e.kind, "style") ? tr("Style") : !strcmp(e.kind, "artist") ? tr("Artiste") : !strcmp(e.kind, "slider") ? tr("Curseur") : !strcmp(e.kind, "instrument") ? tr("Instrum.") : tr("Compos.");
            ImGui::TextColored(P.muted, "%s", kind);
            ImGui::TableSetColumnIndex(2);
            unsigned long long total = 0;
            for (int i = 0; i < e.file_count; i++) total += e.files[i].bytes;
            ImGui::TextUnformatted(fmt_mb(total).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(P.muted, "%s", e.trigger[0] ? e.trigger : "—");
            ImGui::TableSetColumnIndex(4);
            auto job = downloader.find(e.id);
            if (job && job->state == 0) {
                unsigned long long d = job->done, t = std::max<unsigned long long>(1, job->total);
                progress_bar((float) ((double) d / (double) t), ImVec2(80 * g_dpi, 0));
                ImGui::SameLine();
                if (ImGui::SmallButton("✕")) job->cancel = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
            } else if (lora_installed(e)) {
                ImGui::TextColored(P.ok, tr("● installée"));
                if (lora_update_available(e)) {
                    ImGui::SameLine(0, 6 * g_dpi);
                    ImGui::BeginDisabled(studio->local_active());
                    if (soft_button(tr("Mise à jour ↓"), P.orange)) start_lora_update(e);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", studio->local_active() ? tr("Indisponible pendant une génération.") : tr("Une nouvelle version de cette LoRA est disponible : cliquer pour la télécharger et remplacer les fichiers."));
                    }
                }
            } else if (job && job->state == 0) {
                unsigned long long d = job->done, t = std::max<unsigned long long>(1, job->total);
                progress_bar((float) ((double) d / (double) t), ImVec2(80 * g_dpi, 0));
                ImGui::SameLine();
                if (ImGui::SmallButton("✕")) job->cancel = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
            } else {
                bool failed = job && job->state == 2;
                if (ImGui::SmallButton(failed ? tr("Réessayer ↓") : tr("Télécharger ↓"))) start_lora_download(e);
                if (failed && ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Échec : %s"), job->error.c_str());
            }
            ImGui::TableSetColumnIndex(5);
            if (lora_installed(e) && delete_button(tr("Supprimer les fichiers de cette LoRA du disque"))) {
                pending_lora_delete.clear();
                for (int i = 0; i < e.file_count; i++) pending_lora_delete.push_back(e.files[i].file);
                pending_lora_name = tr(e.name);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // Mes LoRA (1.0.24) : LoRA personnelles (créées avec la Forge ou déposées à la main), en tableau comme le catalogue.
    int custom = 0;
    for (const auto & [file, size] : lora_files) custom += !lora_ref(file).entry;
    if (custom > 0) {
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.pink, "%s", tr("Mes LoRA"));
        ImGui::PopFont();
        muted_wrapped(tr("LoRA personnelles : créées avec la Forge ou déposées à la main dans modeles\\lora. Survolez un nom pour voir sa description. Un fichier .json du même nom, posé à côté, peut fournir le nom, la description et le mot déclencheur."));
        float rows = std::min(custom, 6) + 1.4f;
        if (ImGui::BeginTable("##lora_custom", 6, tf, ImVec2(0, rows * ImGui::GetFrameHeightWithSpacing()))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(tr("LoRA"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("Type"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
            ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
            ImGui::TableSetupColumn(tr("Mot"), ImGuiTableColumnFlags_WidthFixed, 124 * g_dpi);
            ImGui::TableSetupColumn(tr("Détails"), ImGuiTableColumnFlags_WidthFixed, 210 * g_dpi);
            ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
            ImGui::TableHeadersRow();
            for (const auto & [file, size] : lora_files) {
                if (lora_ref(file).entry) continue;
                const CustomLora * c = custom_lora(file);
                ImGui::PushID(file.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(custom_label(file).c_str());
                if (ImGui::IsItemHovered()) {
                    std::string tip = c ? custom_description(*c) : std::string(tr("LoRA personnelle déposée dans modeles\\lora."));
                    if (c && !c->author.empty()) tip += tr("\nAuteur : ") + c->author;
                    if (c && !c->tags.empty()) tip += tr("\nBalises : ") + c->tags;
                    if (c && (c->songs || c->clips)) tip += trf("\nMorceaux : %d · extraits : %d", c->songs, c->clips);
                    if (c && !c->date.empty()) tip += tr("\nDate : ") + c->date;
                    if (c && !c->sheet_trigger.empty())
                        tip += trf("\nMot « %s » de la fiche ignoré : la LoRA a appris « %s » (il faut la réentraîner pour changer de mot).",
                                   c->sheet_trigger.c_str(), c->trigger.c_str());
                    if (c && c->trigger.empty()) tip += tr("\nSans mot déclencheur : la LoRA est quand même appliquée à la génération.");
                    tip += tr("\nFichier : ") + file;
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(P.muted, "%s", custom_kind(c));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(fmt_mb(size).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(c && !c->sheet_trigger.empty() ? P.warn : P.muted, "%s", c && !c->trigger.empty() ? c->trigger.c_str() : "—");
                ImGui::TableSetColumnIndex(4);
                std::string details = c ? c->details : std::string();
                if (c && c->forge) details = details.empty() ? std::string(tr("Forge")) : std::string(tr("Forge")) + " · " + details;
                ImGui::TextColored(P.muted, "%s", details.empty() ? "—" : details.c_str());
                if (!details.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", details.c_str());
                ImGui::TableSetColumnIndex(5);
                if (delete_button(tr("Supprimer ce fichier du disque"))) {
                    std::string stem = file.size() > 12 ? file.substr(0, file.size() - 12) : file;  // sans « .safetensors »
                    pending_lora_delete = { file, stem + ".json" };  // la fiche .json part avec la LoRA (absente : ignorée)
                    pending_lora_name   = custom_label(file);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    path_line(studio->paths().adapters);  // 1.0.42 : toujours <programme>\modeles\lora
    if (ImGui::SmallButton(tr("Ouvrir le dossier des LoRA"))) {
        std::error_code ec;
        fs::create_directories(studio->paths().adapters, ec);
        ShellExecuteW(nullptr, L"open", studio->paths().adapters.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    ImGui::TextColored(P.muted, tr("Une LoRA déposée à la main est reconnue automatiquement."));
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
}

// Ouvre la page d'aide (dossier « aide » à côté de l'application) dans le navigateur par défaut.
void App::open_help() {
    if (settings.layout == 2) {  // 1.1.005 : page Aide (navigateur intégré)
        if (b_page == 3) helpview_navigate(help_url());
        b_page = 3;
        return;
    }
    // Page d'aide de la langue courante (aide.<code>.html), sinon l'anglais, sinon le français (aide.html)
    fs::path     dir = app_resource_dir(L"aide");  // ressources\aide (1.0.30), sinon aide
    fs::path     page = dir / "aide.html";
    std::error_code ec;
    if (i18n::current() != "fr") {
        for (const std::string & code : { i18n::current(), std::string("en") }) {
            fs::path candidate = dir / ("aide." + code + ".html");
            if (fs::exists(candidate, ec)) {
                page = candidate;
                break;
            }
        }
    }
    if (!fs::exists(page, ec)) {
        notify(tr("Page d'aide introuvable : ") + narrow(page.wstring()));
        return;
    }
    ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::draw_whisper_settings() {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.teal, tr("Transcription des paroles"));
    ImGui::PopFont();
    muted_wrapped(tr("Whisper (OpenAI, licence MIT, moteur whisper.cpp) écrit les paroles d'un fichier audio chanté et en détecte la langue. Modèle « medium » quantifié (environ 540 Mo), exécuté par la carte graphique : environ 35 secondes pour un morceau de 5 minutes. La transcription fait partie de l'analyse d'un fichier de reprise ; sur de la musique elle n'est pas parfaite, relisez les paroles avant de générer."));
    refresh_whisper();
    auto wjob = downloader.find("whisper");
    ImGui::AlignTextToFramePadding();
    if (wjob && wjob->state == 0) {
        unsigned long long dd = wjob->done, tt = std::max<unsigned long long>(1, wjob->total);
        progress_bar((float) ((double) dd / (double) tt), ImVec2(200 * g_dpi, 0));
        ImGui::SameLine();
        if (ImGui::SmallButton("✕##wh_cancel")) wjob->cancel = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
    } else if (whisper_present) {
        ImGui::TextColored(P.ok, tr("● installé"));
        ImGui::SameLine(0, 8 * g_dpi);
        ImGui::TextColored(P.muted, "%s", fmt_mb(whisper_local_size()).c_str());
        ImGui::SameLine(0, 12 * g_dpi);
        if (whisper_update_available()) {
            if (soft_button(tr("Mise à jour ↓"), P.orange)) start_whisper_download(true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trf("Nouvelle version disponible (%s, installée : %s) : cliquer pour la télécharger et remplacer le fichier.",
                                                                    fmt_mb(remote->get(whisper_file().name)).c_str(), fmt_mb(whisper_local_size()).c_str()).c_str());
        } else if (remote->state == 1) {
            ImGui::TextColored(P.muted, "%s", tr("vérification…"));
        } else if (remote->state == 2 && remote->get(whisper_file().name) > 0) {
            ImGui::TextColored(P.ok, "%s", tr("● à jour"));
        }
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::BeginDisabled(remote->state == 1);
        if (ImGui::SmallButton(tr("Vérifier les mises à jour"))) {
            std::vector<std::pair<std::string, std::string>> items = { { whisper_file().name, whisper_file().url } };
            std::thread(check_remote_sizes, remote, items).detach();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 12 * g_dpi);
        if (delete_button(tr("Supprimer le modèle de transcription du disque"))) pending_whisper_delete = true;
    } else {
        if (!fs::exists(studio->paths().whisper)) {
            ImGui::TextColored(P.warn, "%s", tr("Moteur de transcription (whisper-cli.exe) absent."));
        } else {
            if (accent_button(tr("Télécharger le modèle de transcription"), ImVec2(0, 0), &P.teal)) start_whisper_download(false);
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", trf("environ %s", fmt_mb(whisper_file().bytes).c_str()).c_str());
        }
        if (wjob && wjob->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", wjob->error.c_str()).c_str());
    }
    if (whisper_present) {
        if (lang_running) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", trf("Vérification en cours : %d / %d", (int) lang_checked, (int) lang_total).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Arrêter"))) lang_stop = true;
        } else {
            std::string reason  = busy_reason();
            int         pending = language_check_pending();
            ImGui::BeginDisabled(!reason.empty() || pending == 0);
            if (ImGui::Button(tr("Vérifier la langue des morceaux"))) start_language_check();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", !reason.empty() ? trf("Indisponible : %s.", reason.c_str()).c_str()
                                                   : pending == 0 ? tr("Tous les morceaux chantés ont déjà été vérifiés.")
                                                   : trf("Whisper écoute trois extraits de chaque morceau non encore vérifié (%d) : il corrige la langue des paroles des morceaux chantés si elle est manifestement différente, et signale les morceaux marqués instrumentaux où il entend des paroles. Une fois par morceau.", pending).c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", trf("%d à vérifier", pending).c_str());
        }
    }
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
}

void App::draw_analysis_settings() {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.pink, tr("Analyse audio"));
    ImGui::PopFont();
    muted_wrapped(tr("Quand vous choisissez un fichier audio pour une reprise, ce modèle d'étiquetage (Discogs-EffNet et ses classificateurs, Essentia / Music Technology Group, licence CC BY-NC-SA 4.0, usage non commercial) détecte genre, ambiance, instruments et type de voix ; la partition (moteur de transcription) donne tempo, tonalité, tessiture et structure. Le tout propose des balises de style et la structure des paroles."));
    refresh_analysis();
    auto ajob = downloader.find("analysis");
    ImGui::AlignTextToFramePadding();
    if (ajob && ajob->state == 0) {
        unsigned long long dd = ajob->done, tt = std::max<unsigned long long>(1, ajob->total);
        progress_bar((float) ((double) dd / (double) tt), ImVec2(200 * g_dpi, 0));
        ImGui::SameLine();
        if (ImGui::SmallButton("✕##an_cancel")) ajob->cancel = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
    } else if (analysis_present) {
        ImGui::TextColored(P.ok, tr("● installé"));
        ImGui::SameLine(0, 8 * g_dpi);
        ImGui::TextColored(P.muted, "%s", fmt_mb(analysis_total_bytes()).c_str());
        int upd = analysis_update_count();
        ImGui::SameLine(0, 12 * g_dpi);
        if (upd > 0) {
            if (soft_button(tr("Mise à jour ↓"), P.orange)) start_analysis_download(true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trf("%d fichier(s) du modèle ont une nouvelle version : cliquer pour les télécharger.", upd).c_str());
        } else if (remote->state == 1) {
            ImGui::TextColored(P.muted, "%s", tr("vérification…"));
        } else if (remote->state == 2 && remote->get(analysis_files()[0].name) > 0) {
            ImGui::TextColored(P.ok, "%s", tr("● à jour"));
        }
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::BeginDisabled(remote->state == 1);
        if (ImGui::SmallButton(tr("Vérifier les mises à jour"))) check_analysis_updates();
        ImGui::EndDisabled();
        ImGui::SameLine(0, 12 * g_dpi);
        if (delete_button(tr("Supprimer le modèle d'analyse du disque"))) pending_analysis_delete = true;
    } else {
        if (accent_button(tr("Télécharger le modèle d'analyse"), ImVec2(0, 0), &P.pink)) start_analysis_download(false);
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", trf("environ %s", fmt_mb(analysis_total_bytes()).c_str()).c_str());
        if (ajob && ajob->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", ajob->error.c_str()).c_str());
    }
    if (ImGui::Checkbox(tr("Analyser automatiquement le fichier choisi pour une reprise"), &settings.analysis_auto)) save_settings(settings, settings_path);
    if (ImGui::Checkbox(tr("Chercher les paroles sur Internet pour corriger la transcription"), &settings.lyrics_web)) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Envoie le groupe et le titre du morceau (étiquettes ou nom du fichier) à lrclib.net, une base de paroles libre et gratuite. Sans connexion, la transcription Whisper est gardée."));
    }
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
}

// Bloc de réglages : cadre teinté, barre de la couleur du bloc à gauche (comme les cartes du formulaire).
void App::begin_card(const ImVec4 & color, const char * id) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.card, color, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * g_dpi, 10 * g_dpi));
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    card_color = color;
}

void App::end_card() {
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(5 * g_dpi, ws.y), ImGui::GetColorU32(card_color), ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
}

void App::draw_settings_modal() {
    if (settings.layout == 2) {  // 1.1.005 : page de droite
        if (open_settings) {
            open_settings = false;
            models        = studio->paths().list_models();
            b_page        = 2;
        }
        return;
    }
    if (open_settings) {
        ImGui::OpenPopup(tr("Réglages"));
        open_settings = false;
        models        = studio->paths().list_models();  // modèles ajoutés depuis le dernier affichage
    }
    {   // taille par défaut : 1100 px de large (au moins 1024 si l'écran le permet) ; redimensionnable, le contenu s'adapte
        ImVec2 vp = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(640 * g_dpi, vp.x - 20 * g_dpi), std::min(420 * g_dpi, vp.y - 20 * g_dpi)),
                                            ImVec2(vp.x - 20 * g_dpi, vp.y - 20 * g_dpi));
        // fenêtre ouverte pendant que le Studio était réduit (essais) : taille et place reprises quand la fenêtre change de taille
        static ImVec2 last_vp;
        const bool    vp_changed = vp.x != last_vp.x || vp.y != last_vp.y;
        last_vp = vp;
        ImGui::SetNextWindowSize(ImVec2(std::min(1100 * g_dpi, vp.x - 40 * g_dpi), std::min(820 * g_dpi, vp.y - 60 * g_dpi)), vp_changed ? ImGuiCond_Always : ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), vp_changed ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }
    if (!ImGui::BeginPopupModal(tr("Réglages"), nullptr, ImGuiWindowFlags_NoCollapse)) return;
    settings_shown = true;
    draw_settings_body(false);
    ImGui::EndPopup();
}

// 1.1.005 : contenu des Réglages, dans la fenêtre (page = false) ou dans la page de droite de la disposition B
// (catégories dans le menu de gauche, pas de bouton « Fermer »).
void App::draw_settings_body(bool page) {
    int & cat = settings_cat;
    if (open_settings_cat >= 0) {  // bouton « Réglages › IA… » de l'onglet Composer avec l'IA
        cat               = open_settings_cat;
        open_settings_cat = -1;
    }
    {   // navigation par catégories : colonne à gauche, ou rangée en haut quand la fenêtre est étroite
        struct Cat { const char * label; const ImVec4 * color; };
        const Cat cats[] = { { N_("Interface"), &P.accent }, { N_("Génération"), &P.teal }, { N_("Analyse et paroles"), &P.pink },
                             { N_("LoRA"), &P.violet }, { N_("Système"), &P.blue }, { N_("Bibliothèque"), &P.danger },
                             { N_("IA (paroles)"), &P.amber }, { N_("Styles"), &P.green } };
        const float foot   = page ? 0.f : ImGui::GetFrameHeight() + 12 * g_dpi;
        const bool  narrow = ImGui::GetContentRegionAvail().x < 820 * g_dpi;
        if (page) {
            ImGui::PushFont(g_header);
            ImGui::TextUnformatted((std::string(tr("Réglages")) + "  ·  " + tr(cats[std::clamp(cat, 0, IM_ARRAYSIZE(cats) - 1)].label)).c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        } else if (narrow) {
            for (int i = 0; i < IM_ARRAYSIZE(cats); i++) {
                if (i > 0) {
                    float w = ImGui::CalcTextSize(tr(cats[i].label)).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
                    if (ImGui::GetCursorPosX() + w < ImGui::GetContentRegionMax().x) ImGui::SameLine();
                }
                ImGui::PushID(i);
                if (cat == i ? accent_button(tr(cats[i].label), ImVec2(0, 0), cats[i].color) : ImGui::Button(tr(cats[i].label))) cat = i;
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        } else {
            ImGui::BeginChild("##set_nav", ImVec2(200 * g_dpi, -foot), ImGuiChildFlags_Borders);
            ImDrawList * dl = ImGui::GetWindowDrawList();
            for (int i = 0; i < IM_ARRAYSIZE(cats); i++) {
                const ImVec4 & c   = *cats[i].color;
                ImVec2         p0  = ImGui::GetCursorScreenPos();
                float          w   = ImGui::GetContentRegionAvail().x, h = 34 * g_dpi;
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Header, alpha(c, 0.22f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, alpha(c, 0.14f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, alpha(c, 0.30f));
                if (ImGui::Selectable("##nav", cat == i, 0, ImVec2(w, h))) cat = i;
                ImGui::PopStyleColor(3);
                ImGui::PopID();
                if (cat == i) dl->AddRect(p0, p0 + ImVec2(w, h), ImGui::GetColorU32(alpha(c, 0.6f)), 4 * g_dpi);
                dl->AddRectFilled(p0 + ImVec2(0, 6 * g_dpi), p0 + ImVec2(4 * g_dpi, h - 6 * g_dpi), ImGui::GetColorU32(c), 2 * g_dpi);
                const char * label = tr(cats[i].label);
                dl->AddText(ImVec2(p0.x + 14 * g_dpi, p0.y + (h - ImGui::GetTextLineHeight()) / 2), ImGui::GetColorU32(cat == i ? c : P.text), label);
                ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            }
            ImGui::EndChild();
            ImGui::SameLine();
        }
        ImGui::BeginChild("##set_content", ImVec2(0, -foot), 0);
    if (test_scroll_frames > 0) {
        ImGui::SetScrollY(test_scroll);
        test_scroll_frames--;
    }
    }

    if (cat == 0) {
    begin_card(P.accent, "c_lang");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, "%s", tr("Langue de l'application"));
    ImGui::PopFont();
    {
        std::string shown = i18n::current();
        for (const i18n::Language & l : i18n::languages())
            if (l.code == i18n::current()) shown = l.name;
        ImGui::SetNextItemWidth(260 * g_dpi);
        if (ImGui::BeginCombo("##ui_language", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (const i18n::Language & l : i18n::languages()) {
                if (ImGui::Selectable(l.name.c_str(), l.code == i18n::current())) pending_language = l.code;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Ouvrir le dossier des langues"))) {
            std::error_code ec;
            fs::create_directories(app_resource_dir(L"langues"), ec);
            ShellExecuteW(nullptr, L"open", app_resource_dir(L"langues").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        muted_wrapped(tr("Chaque langue est un fichier du dossier « langues » : pour corriger une traduction ou en ajouter une, modifiez ou copiez un de ces fichiers (voir _modele.json). Une langue ajoutée apparaît ici au prochain démarrage."));
    }
    end_card();
    }

    if (cat == 0) {
    begin_card(P.accent, "c_theme");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, tr("Thème graphique"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(260 * g_dpi);
    if (ImGui::BeginCombo("##theme", tr(THEME_NAMES[settings.theme]))) {
        for (int i = 0; i < THEME_COUNT; i++) {
            if (ImGui::Selectable(tr(THEME_NAMES[i]), settings.theme == i)) {
                settings.theme = i;
                g_theme        = i;
                theme_pending  = true;  // 1.1.004 : appliqué au début de l'image suivante (voir frame)
                save_settings(settings, settings_path);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(THEME_HINTS[i]));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    {   // aperçu de la palette du thème actif
        ImDrawList * dl = ImGui::GetWindowDrawList();
        ImVec2       p0 = ImGui::GetCursorScreenPos();
        const ImVec4 sw[] = { P.bg, P.panel, P.card, P.accent, P.ok, P.warn, P.danger, P.violet, P.pink };
        float        sz = ImGui::GetFrameHeight() * 0.75f, y = p0.y + (ImGui::GetFrameHeight() - sz) / 2;
        for (int i = 0; i < IM_ARRAYSIZE(sw); i++) {
            ImVec2 a(p0.x + i * (sz + 3 * g_dpi), y);
            dl->AddRectFilled(a, a + ImVec2(sz, sz), ImGui::GetColorU32(sw[i]), 3 * g_dpi);
            dl->AddRect(a, a + ImVec2(sz, sz), ImGui::GetColorU32(P.line), 3 * g_dpi);
        }
        ImGui::Dummy(ImVec2(IM_ARRAYSIZE(sw) * (sz + 3 * g_dpi), ImGui::GetFrameHeight()));
    }
    muted_wrapped(tr("Le thème par défaut suit le mode clair ou sombre de Windows ; les autres thèmes sont fixes. Le changement est immédiat."));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    {   // 1.1.002 : disposition de l'écran principal (maquettes A et B de la revue 1.0.98)
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Disposition"));
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Actuelle"), settings.layout == 0)) { settings.layout = 0; save_settings(settings, settings_path); }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("A : rangée"), settings.layout == 1)) { settings.layout = 1; save_settings(settings, settings_path); }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("B : menu à gauche"), settings.layout == 2)) { settings.layout = 2; save_settings(settings, settings_path); }  // 1.1.003
        muted_wrapped(tr("A : bouton Générer toujours visible en bas du formulaire, file de génération repliée quand elle est vide, lecteur fixe en bas de la fenêtre, cartes avec 4 boutons et 3 menus (Télécharger, Refaire, Vidéos) au lieu de 14 boutons."));
        muted_wrapped(tr("B : comme A, avec un menu à gauche ; la création et la bibliothèque occupent chacune toute la largeur."));
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, tr("Marques des morceaux"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(340 * g_dpi);
    if (ImGui::Combo("##marques", &settings.mark_style, tr_list(MARK_STYLES, (int) IM_ARRAYSIZE(MARK_STYLES)).data(), IM_ARRAYSIZE(MARK_STYLES))) save_settings(settings, settings_path);
    {   // aperçu des trois états dans le style choisi
        ImGui::TextColored(P.muted, tr("Aperçu :"));
        const struct { const char * label; bool v, f; } states[] = { { tr("à écouter"), false, false }, { tr("validé"), true, false }, { tr("à problème"), false, true } };
        for (auto & st : states) {
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(P.muted, "%s", st.label);
            ImGui::SameLine(0, 6 * g_dpi);
            ImGui::PushID(st.label);
            draw_marks(settings.mark_style, st.v, st.f, 22 * g_dpi);
            ImGui::PopID();
        }
    }
    end_card();
    }

    if (cat == 0) {
    begin_card(P.accent, "c_notif");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, tr("Notifications"));
    ImGui::PopFont();
    if (ImGui::Checkbox(tr("Pastilles à côté de l'horloge"), &settings.notifications)) save_settings(settings, settings_path);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Compteurs de l'en-tête"));
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Sport (grands cadrans)"), settings.gauge_style == 0)) { settings.gauge_style = 0; save_settings(settings, settings_path); }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Compact (plus de place pour la bibliothèque)"), settings.gauge_style == 1)) { settings.gauge_style = 1; save_settings(settings, settings_path); }
    if (ImGui::Checkbox(tr("Application réduite : jauges et compteurs à côté de l'horloge"), &settings.tray_gauges)) save_settings(settings, settings_path);
    muted_wrapped(tr("Quand la fenêtre est réduite, deux icônes s'ajoutent près de l'horloge : trois barres (CPU, GPU, mémoire GPU) et deux nombres (morceaux en attente en violet, terminés en vert). Le survol donne les valeurs exactes. Sous Windows 11, épinglez-les via Paramètres › Personnalisation › Barre des tâches › Autres icônes de la barre d'état système si elles restent cachées dans le menu ^."));
    end_card();
    }

    if (cat == 4) {
    begin_card(P.orange, "c_games");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.orange, tr("Jeux"));
    ImGui::PopFont();
    if (ImGui::Checkbox(tr("Suspendre la file de génération quand un jeu est lancé"), &settings.game_pause)) {
        save_settings(settings, settings_path);
    }
    muted_wrapped(tr("La génération en cours est arrêtée (elle repartira du début) et la file reprend 10 secondes après la fermeture du jeu. Les jeux en plein écran sont détectés automatiquement ; indiquez ci-dessous les autres programmes à surveiller (noms de fichiers séparés par des virgules)."));
    if (input_line("##games", "cs2.exe, VRising.exe", settings.game_list)) save_settings(settings, settings_path);
    if (!settings.game_pause) ImGui::TextColored(P.muted, tr("Détection désactivée."));
    else if (game_active) ImGui::TextColored(P.orange, tr("● Jeu détecté : %s — file suspendue"), game_name.c_str());
    else ImGui::TextColored(P.ok, tr("● Aucun jeu détecté — la file peut tourner"));
    end_card();
    }

    if (cat == 1) {
    begin_card(P.teal, "c_model");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.teal, tr("Modèle de génération"));
    ImGui::PopFont();
    draw_model_combo("##modele_defaut", 260 * g_dpi, true);
    muted_wrapped(tr("Modèle par défaut au démarrage. Aux essais, les trois quantifications vont aussi vite l'une que l'autre ; Q6_K et Q5_K_M économisent seulement 0,3 à 0,4 Go de mémoire vidéo au pic. Le choix rapide de la barre du haut s'applique aux prochains morceaux ; chaque morceau retient le modèle qui l'a produit."));
    {
        struct Quant { const char * file; const char * label; const char * size; const char * note; };
        static const Quant quants[] = {
            { "YuE2-3B-Q8_0.gguf", "Q8_0", N_("3,81 Go"), N_("qualité maximale (presque sans perte)") },
            { "YuE2-3B-Q6_K.gguf", "Q6_K", N_("2,94 Go"), N_("recommandé : qualité jugée équivalente, plus léger") },
            { "YuE2-3B-Q5_K_M.gguf", "Q5_K_M", N_("2,62 Go"), N_("le plus léger") },
        };
        ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##quants", 5, tf)) {
            ImGui::TableSetupColumn(tr("Modèle"), ImGuiTableColumnFlags_WidthFixed, 80 * g_dpi);
            ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 70 * g_dpi);
            ImGui::TableSetupColumn(tr("Remarque"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("Téléchargement"), ImGuiTableColumnFlags_WidthFixed, 200 * g_dpi);
            ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
            ImGui::TableHeadersRow();
            for (const Quant & q : quants) {
                bool installed = std::find(models.begin(), models.end(), q.file) != models.end();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(q.label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(tr(q.size));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(P.muted, "%s", tr(q.note));
                ImGui::TableSetColumnIndex(3);
                auto mjob = downloader.find(std::string("model:") + q.file);
                if (mjob && mjob->state == 0) {
                    unsigned long long dd = mjob->done, tt = std::max<unsigned long long>(1, mjob->total);
                    progress_bar((float) ((double) dd / (double) tt), ImVec2(80 * g_dpi, 0));
                    ImGui::SameLine();
                    ImGui::PushID(q.file);
                    if (ImGui::SmallButton("✕")) mjob->cancel = true;
                    ImGui::PopID();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
                } else if (installed) {
                    ImGui::TextColored(P.ok, tr("● installé"));
                    if (model_update_available(q.file)) {
                        ImGui::SameLine(0, 6 * g_dpi);
                        ImGui::PushID(q.file);
                        ImGui::BeginDisabled(studio->local_active());
                        if (soft_button(tr("Mise à jour ↓"), P.orange)) start_model_update(q.file);
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("%s", studio->local_active() ? tr("Indisponible pendant une génération.")
                                                                           : trf("Nouvelle version disponible (%s, installée : %s) : cliquer pour la télécharger et remplacer le fichier.",
                                                                                 fmt_mb(remote->get(q.file)).c_str(), fmt_mb(model_local_size(q.file)).c_str()).c_str());
                        }
                    }
                } else {
                    ImGui::PushID(q.file);
                    if (ImGui::SmallButton(tr("Télécharger ↗"))) {
                        std::wstring url = L"https://huggingface.co/Serveurperso/YuE2-GGUF/resolve/main/" + widen(q.file);
                        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    ImGui::PopID();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(tr("Ouvre le téléchargement dans le navigateur (Hugging Face).\nPlacez ensuite le fichier dans :\n%s"), narrow(studio->paths().gguf_dir.wstring()).c_str());
                    }
                }
                ImGui::TableSetColumnIndex(4);
                if (installed) {
                    ImGui::PushID(q.file);
                    if (delete_button(tr("Supprimer ce modèle du disque"))) pending_model_delete = q.file;
                    ImGui::PopID();
                }
            }
            // modèles déposés à la main, hors de cette liste
            for (const std::string & m : models) {
                bool known = false;
                for (const Quant & q : quants) known = known || m == q.file;
                if (known) continue;
                std::error_code ec;
                auto            size = fs::file_size(studio->paths().gguf_dir / widen(m), ec);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(model_label(m).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ec ? "" : fmt_mb(size).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(P.muted, tr("ajouté à la main"));
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(P.ok, tr("● installé"));
                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(m.c_str());
                if (delete_button(tr("Supprimer ce modèle du disque"))) pending_model_delete = m;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        path_line(studio->paths().gguf_dir);  // 1.0.42 : toujours <programme>\modeles\musique
        if (ImGui::SmallButton(tr("Ouvrir le dossier des modèles"))) {
            ShellExecuteW(nullptr, L"open", studio->paths().gguf_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        ImGui::TextColored(P.muted, tr("La liste se met à jour à chaque ouverture des Réglages."));
    }
    end_card();
    }

    if (cat == 1) {
    begin_card(P.orange, "c_end");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.orange, tr("Fin des morceaux"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(260 * g_dpi);
    if (ImGui::SliderInt("##fondu", &settings.fade_seconds, 1, 30, tr("fondu de %d s"))) studio->set_fade_seconds(settings.fade_seconds);
    if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
    muted_wrapped(tr("Longueur du fondu appliqué quand un morceau atteint le plafond de durée sans conclure (instrumentaux, durée fixe). Dans le formulaire, « Fin en fondu sur la structure » remplace ce réglage par la durée de la dernière section, avec une courbe plus douce."));
    end_card();
    }

    if (cat == 1) {
        begin_card(P.green, "c_packs");
        draw_packs_settings();
        end_card();
    }

    if (cat == 1) {
        begin_card(P.warn, "c_negative");
        draw_negative_tags_settings();
        end_card();
    }

    if (cat == 1) {
    begin_card(P.accent, "c_solver");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, tr("Solveur de la phase audio (NAR)"));
    ImGui::PopFont();
    draw_solver_combo("##solveur_defaut", 260 * g_dpi, true);
    muted_wrapped(tr("Solveur par défaut au démarrage : méthode de calcul et nombre de pas de la phase audio, qui domine le temps de génération. Moins il y a d'évaluations du réseau, plus c'est rapide. Le nom donne la méthode puis le nombre de pas (AB3 24 = Adams-Bashforth d'ordre 3, 24 pas ; MP 32 = midpoint, 32 pas, la référence). Le choix rapide de la barre du haut s'applique aux prochains morceaux ; chaque morceau retient son solveur."));
    if (ImGui::BeginTable("##solveurs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Solveur"), ImGuiTableColumnFlags_WidthFixed, 90 * g_dpi);
        ImGui::TableSetupColumn(tr("Éval."), ImGuiTableColumnFlags_WidthFixed, 50 * g_dpi);
        ImGui::TableSetupColumn(tr("Vitesse"), ImGuiTableColumnFlags_WidthFixed, 80 * g_dpi);
        ImGui::TableSetupColumn(tr("Remarque"));
        ImGui::TableHeadersRow();
        for (const SolverSpec & sp : solver_specs()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sp.label);
            ImGui::TableNextColumn();
            ImGui::Text("%d", sp.evals);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f ×", 64.0 / sp.evals);
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(P.muted, "%s", tr(sp.note));
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    end_card();
    }

    if (cat == 2) {
        begin_card(P.pink, "c_analysis");
        draw_analysis_settings();
        end_card();
        begin_card(P.teal, "c_whisper");
        draw_whisper_settings();
        end_card();
        begin_card(P.violet, "c_translate");
        draw_translate_settings();
        end_card();
    }
    if (cat == 3) {
        begin_card(P.pink, "c_lora");
        draw_lora_catalog();
        end_card();
    }

    if (cat == 4) {
    begin_card(P.blue, "c_gpu");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.blue, tr("Version"));
    ImGui::PopFont();
    draw_update_settings();
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.blue, "%s", tr("Journal de débogage"));
    ImGui::PopFont();
    muted_wrapped(tr("Le Studio note ce qui se passe (démarrage, clics, travaux lancés, erreurs et ce qui les a précédées) : à joindre à un message en cas de problème."));
    if (soft_button(tr("Ouvrir le journal"), P.blue)) {
        std::error_code jec;
        if (fs::exists(journal::path(), jec)) ShellExecuteW(nullptr, L"open", journal::path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.blue, tr("Recommandations pour ce PC"));
    ImGui::PopFont();
    draw_advice();
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.blue, tr("Carte graphique"));
    ImGui::PopFont();
    draw_gpu_profile();
    draw_gpu_limit();
    ImGui::TextColored(P.muted, tr("Pilote : %s"), driver_version.empty() ? "inconnu" : driver_version.c_str());
    if (driver_update->state == 3) {  // pilote AMD plus récent trouvé au démarrage
        ImGui::SameLine(0, 12 * g_dpi);
        if (soft_button(tr("Mise à jour disponible"), P.orange)) {
            ShellExecuteW(nullptr, L"open", L"https://www.amd.com/en/support/download/drivers.html", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", trf("AMD Software installé : %s · dernière version stable : %s\nOuvre la page de téléchargement des pilotes AMD.",
                                        driver_update->installed.c_str(), driver_update->latest.c_str()).c_str());
        }
    } else if (driver_update->state == 2) {
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::TextColored(P.ok, "%s", tr("● pilote à jour"));
    }
    if (gpu_profile().gpu.vendor == GpuVendor::Nvidia) {  // pas de recherche automatique : lien vers la page Nvidia
        ImGui::SameLine(0, 12 * g_dpi);
        if (soft_button(tr("Pilotes Nvidia"), P.teal)) {
            ShellExecuteW(nullptr, L"open", L"https://www.nvidia.com/fr-fr/drivers/", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ouvre la page de téléchargement des pilotes Nvidia."));
    }
    if (driver_changed) {
        colored_wrapped(P.warn, tr("Le pilote a changé depuis le dernier test : lancez le test de santé."));
    }
    HealthResult h = studio->health();
    ImVec4       hc = h.state == 2 ? P.ok : h.state == 3 ? P.danger : h.state == 4 ? P.warn : P.muted;
    if (h.state == 0) muted_wrapped(tr("Le test rejoue deux fois un court extrait et compare les résultats : il détecte une carte graphique ou un pilote qui produirait des morceaux saturés ou parasités."));
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, hc);
        ImGui::TextWrapped("%s", h.message.c_str());
        ImGui::PopStyleColor();
    }
    bool busy = h.state == 1 || studio->local_active();
    ImGui::BeginDisabled(busy);
    if (accent_button(h.state == 1 ? tr("Test en cours…") : tr("Lancer le test de santé (≈ 1 min)"), ImVec2(0, 0), &P.blue)) {
        studio->start_health_check();
    }
    ImGui::EndDisabled();
    if (studio->local_active() && h.state != 1) {
        ImGui::SameLine();
        ImGui::TextColored(P.muted, tr("Indisponible pendant une génération."));
    }
    end_card();
    begin_card(P.amber, "c_model_licences");  // 1.0.84 (demande de Herve 5/11)
    draw_model_licences();
    end_card();
    }

    if (cat == 5) {
    begin_card(P.danger, "c_library");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.danger, tr("Bibliothèque"));
    ImGui::PopFont();
    {
        const int total = (int) studio->snapshot().size();
        muted_wrapped(tr("Supprime tous les morceaux générés (audio, fiches, journaux). Ils sont envoyés à la corbeille de Windows : vous pourrez les y restaurer. Les images d'albums et les vidéos ne sont pas touchées."));
        ImGui::BeginDisabled(total == 0 || studio->local_active());
        if (accent_button(tr("Supprimer tous les morceaux…"), ImVec2(0, 0), &P.danger)) pending_delete_all = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(P.muted, total > 1 ? tr("%d morceaux dans la bibliothèque") : tr("%d morceau dans la bibliothèque"), total);
    }
    end_card();
    }
    if (cat == 6) draw_ai_settings();
    if (cat == 7) draw_style_tags_settings();
    ImGui::EndChild();  // contenu
    if (!page) {
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
    }

    // Confirmations (fenêtres au-dessus des Réglages)
    if (!pending_model_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer le modèle ?"))) ImGui::OpenPopup(tr("Supprimer le modèle ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer le modèle ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("Le fichier « %s » sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau ; les morceaux déjà générés ne sont pas touchés."), pending_model_delete.c_str());
        path_line(studio->paths().gguf_dir / widen(pending_model_delete));
        if (models.size() <= 1) {
            colored_wrapped(P.warn, tr("C'est le seul modèle installé : il faudra en télécharger un pour générer."));
        }
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_model_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            delete_model_file(pending_model_delete);
            pending_model_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (pending_analysis_delete && !ImGui::IsPopupOpen(tr("Supprimer le modèle d'analyse ?"))) ImGui::OpenPopup(tr("Supprimer le modèle d'analyse ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer le modèle d'analyse ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", tr("Les fichiers du modèle d'analyse audio seront supprimés définitivement du disque. Vous pourrez les télécharger de nouveau ; les morceaux déjà générés ne sont pas touchés."));
        path_line(analysis_dir());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_analysis_delete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            delete_analysis_files();
            pending_analysis_delete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (pending_whisper_delete && !ImGui::IsPopupOpen(tr("Supprimer le modèle de transcription ?"))) ImGui::OpenPopup(tr("Supprimer le modèle de transcription ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer le modèle de transcription ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", tr("Le fichier du modèle de transcription des paroles sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau ; les morceaux déjà générés ne sont pas touchés."));
        path_line(whisper_dir() / widen(whisper_file().name));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_whisper_delete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            delete_whisper_file();
            pending_whisper_delete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (pending_translate_delete && !ImGui::IsPopupOpen(tr("Supprimer le modèle de traduction ?"))) ImGui::OpenPopup(tr("Supprimer le modèle de traduction ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer le modèle de traduction ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", tr("Le modèle de traduction local sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau ; la traduction par Internet reste disponible."));
        path_line(translate_dir() / widen(translate_model_file().name));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_translate_delete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            std::error_code ec;
            remove_inside(translate_dir() / widen(translate_model_file().name), ec);
            refresh_translator(true);
            if (!remove_refused) notify(tr("Modèle de traduction supprimé."));
            pending_translate_delete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!pending_lora_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer la LoRA ?"))) ImGui::OpenPopup(tr("Supprimer la LoRA ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer la LoRA ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("La LoRA « %s » sera supprimée définitivement du disque. Vous pourrez la télécharger de nouveau (si elle est dans le catalogue) ; les morceaux déjà générés ne sont pas touchés."), pending_lora_name.c_str());
        for (const std::string & f : pending_lora_delete) {
            std::error_code ec;
            if (fs::exists(studio->paths().adapters / widen(f), ec)) path_line(studio->paths().adapters / widen(f));
        }
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_lora_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            delete_lora_files(pending_lora_delete, pending_lora_name);
            pending_lora_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (pending_delete_all && !ImGui::IsPopupOpen(tr("Supprimer tous les morceaux ?"))) ImGui::OpenPopup(tr("Supprimer tous les morceaux ?"));
    ImGui::SetNextWindowSize(ImVec2(480 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer tous les morceaux ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("Les %d morceaux de la bibliothèque seront envoyés à la corbeille de Windows. Vous pourrez les y restaurer tant que la corbeille n'est pas vidée."), (int) studio->snapshot().size());
        path_line(studio->paths().library);
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_delete_all = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Tout mettre à la corbeille"), ImVec2(0, 0), &P.danger)) {
            player.close();
            std::string err;
            int         n = studio->remove_all(err);
            if (n < 0) notify(err);
            else notify(trf("%d morceau(x) envoyé(s) à la corbeille.", n));
            pending_delete_all = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!page) {  // en page, dessinées au niveau principal (draw_modals)
        draw_pack_modals();  // export, import, suppression d'un pack : au-dessus des Réglages
        draw_cover_splash();  // 1.0.37 : pochettes créées pendant que les Réglages sont ouverts
    }
}

// ------------------------------------------------------------------ Tri
void App::sort_shown(std::vector<const Song *> & v) const {
    std::stable_sort(v.begin(), v.end(), [&](const Song * a, const Song * b) {
        if (a->active() != b->active()) return a->active();  // la file de travail reste en tête
        switch (settings.sort) {
            case 1: return a->created < b->created;
            case 2: return fold(a->title) < fold(b->title);
            case 3: {
                int x = track_number(*a), y = track_number(*b);
                if (x != y) return (x ? x : 100000) < (y ? y : 100000);
                return fold(a->title) < fold(b->title);
            }
            case 4: return a->audio_seconds > b->audio_seconds;
            case 5:
                if (a->validated != b->validated) return a->validated;
                return a->created > b->created;
            default: return a->created > b->created;
        }
    });
}

void App::draw_sort_bar() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, tr("Trier"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230 * g_dpi);
    if (ImGui::Combo("##tri", &settings.sort, tr_list(SORTS, (int) IM_ARRAYSIZE(SORTS)).data(), IM_ARRAYSIZE(SORTS))) save_settings(settings, settings_path);
    ImGui::SameLine(0, 18 * g_dpi);
    ImGui::TextColored(P.muted, tr("Cartes"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150 * g_dpi);
    static const char * CARD_MODES[] = { N_("Détaillées"), N_("Compactes"), N_("Avec pochette"), N_("Pochette compacte") };
    if (ImGui::Combo("##cartes", &settings.card_mode, tr_list(CARD_MODES, (int) IM_ARRAYSIZE(CARD_MODES)).data(), IM_ARRAYSIZE(CARD_MODES))) save_settings(settings, settings_path);
    {   // la case passe à la ligne suivante quand le texte ne tient pas (langues à textes longs)
        float cb_w  = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(tr("Lecture continue")).x;
        float right = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + 18 * g_dpi + cb_w <= right) ImGui::SameLine(0, 18 * g_dpi);
    }
    if (ImGui::Checkbox(tr("Lecture continue"), &settings.autoplay)) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("À la fin d'un morceau, lit le suivant de la liste affichée (morceaux terminés).\nAvec le tri « N° de piste » et le filtre « Validés », on écoute un album dans l'ordre."));
    }
}
