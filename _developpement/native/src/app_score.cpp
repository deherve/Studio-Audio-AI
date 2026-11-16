// Partition, Atelier partition, aide interrogeable, disque, tonalité, mise à jour, stems, sons (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

  // dossier du morceau -> (heure de lecture, partition)
const ScoreInfo & App::score_info(const Song & s) const {
    static const ScoreInfo none;
    if (s.status != "done") return none;
    const fs::path f   = song_folder(s.id) / "score.abc";
    const std::string k = narrow(f.wstring());
    const double now = ImGui::GetTime();
    auto it = score_cache.find(k);
    if (it != score_cache.end() && now - it->second.first < 10.0) return it->second.second;
    ScoreInfo info;
    std::error_code ec;
    if (fs::exists(f, ec)) {
        std::ifstream in(f, std::ios::binary);
        info = parse_score(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    }
    auto & slot = score_cache[k];
    slot = { now, info };
    return slot.second;
}

// Tonalité ABC (« Em », « F#m », « Bb ») en clair : « Mi mineur » / « E minor »
std::string App::key_name(const std::string & key) {
    if (key.empty()) return std::string();
    static const char * notes[] = { N_("Do"), N_("Ré"), N_("Mi"), N_("Fa"), N_("Sol"), N_("La"), N_("Si") };
    const char * letters = "CDEFGAB";
    const char * at = strchr(letters, toupper((unsigned char) key[0]));
    if (!at) return key;
    std::string out = tr(notes[at - letters]);
    size_t i = 1;
    if (i < key.size() && key[i] == '#') { out += std::string(" ") + tr("dièse"); i++; }
    else if (i < key.size() && key[i] == 'b') { out += std::string(" ") + tr("bémol"); i++; }
    std::string rest = key.substr(i);
    for (char & c : rest) c = (char) tolower((unsigned char) c);
    const bool minor = rest.rfind("m", 0) == 0 && rest.rfind("maj", 0) != 0 && rest.rfind("mix", 0) != 0;
    return out + " " + (minor ? tr("mineur") : tr("majeur"));
}

void App::open_atelier(const std::string & abc, const std::string & title, const std::string & subtitle, const std::string & name, const fs::path & dir, const std::string & lyrics) {  // 1.0.94 : paroles proposées pour l'édition
    if (atelier_runtime_version().empty()) {
        notify(tr("L'Atelier partition a besoin du moteur Microsoft Edge WebView2 (fourni avec Windows 10 et 11). Installez « WebView2 Runtime » depuis le site de Microsoft."));
        return;
    }
    atelier_dir = dir;
    const ScoreInfo  sc  = parse_score(abc);
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    const std::string shown = trim(abc).empty() ? std::string() : abc_for_display(abc, title, subtitle);
    const std::string info  = !sc.present ? std::string()
                                          : trf("Tonalité : %s (%s)", key_name(sc.key).c_str(), sc.key.c_str()) + "   ·   " + trf("Tempo : %d BPM", sc.tempo) +
                                                "   ·   " + trf("Mesure : %s", sc.meter.c_str());
    yyjson_mut_obj_add_str(doc, o, "type", "load");
    yyjson_mut_obj_add_strcpy(doc, o, "abc", shown.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "title", title.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "subtitle", subtitle.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "info", info.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "name", name.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "lyrics", lyrics.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "composer", subtitle.substr(0, subtitle.find(" — ")).c_str());  // 1.0.92 : auteur du MusicXML
    yyjson_mut_val * t = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, t, "export_error", tr("Export impossible :"));
    yyjson_mut_obj_add_strcpy(doc, t, "import", tr("Importer…"));
    yyjson_mut_obj_add_strcpy(doc, t, "import_tip", tr("Ouvrir une partition : MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) ou ABC (.abc). Elle est mise au format du moteur YuE2 (voix chantée et instrument)."));
    yyjson_mut_obj_add_strcpy(doc, t, "use", tr("Utiliser dans le Studio"));
    yyjson_mut_obj_add_strcpy(doc, t, "use_tip", tr("Envoie cette partition dans l'onglet « Partition » du Studio, pour générer un morceau qui la suit."));
    yyjson_mut_obj_add_strcpy(doc, t, "empty", tr("Aucune partition : « Importer… » pour ouvrir un fichier MusicXML, MIDI ou ABC."));
    yyjson_mut_obj_add_strcpy(doc, t, "imported", tr("Partition importée"));
    yyjson_mut_obj_add_strcpy(doc, t, "key", tr("Tonalité"));
    yyjson_mut_obj_add_strcpy(doc, t, "import_error", tr("Import impossible :"));
    yyjson_mut_obj_add_strcpy(doc, t, "midi_tip", tr("Fichier MIDI : les notes et le tempo, pour un logiciel de musique ou un clavier."));
    yyjson_mut_obj_add_strcpy(doc, t, "xml_tip", tr("Fichier MusicXML : la partition complète (notes, mesures, accords) pour MuseScore, Finale, Sibelius, Dorico…"));
    yyjson_mut_obj_add_strcpy(doc, t, "abc_tip", tr("Fichier ABC : la partition au format texte du moteur YuE2."));
    yyjson_mut_obj_add_strcpy(doc, t, "print", tr("Imprimer…"));
    yyjson_mut_obj_add_strcpy(doc, t, "pdf", tr("Exporter en PDF…"));
    yyjson_mut_obj_add_strcpy(doc, t, "zoom_in", tr("Agrandir"));
    yyjson_mut_obj_add_strcpy(doc, t, "zoom_out", tr("Réduire"));
    yyjson_mut_obj_add_strcpy(doc, t, "warnings", tr("Remarques de lecture de la partition :"));
    // 1.0.94 : édition
    yyjson_mut_obj_add_strcpy(doc, t, "note", tr("Note choisie :"));
    yyjson_mut_obj_add_strcpy(doc, t, "rest", tr("Silence"));
    yyjson_mut_obj_add_strcpy(doc, t, "duplicate", tr("Dupliquer"));
    yyjson_mut_obj_add_strcpy(doc, t, "delete", tr("Supprimer"));
    yyjson_mut_obj_add_strcpy(doc, t, "chord", tr("Accord…"));
    yyjson_mut_obj_add_strcpy(doc, t, "lyrics", tr("Paroles…"));
    yyjson_mut_obj_add_strcpy(doc, t, "abc_text", tr("Texte ABC"));
    yyjson_mut_obj_add_strcpy(doc, t, "lyrics_help", tr("Paroles placées sous la mélodie chantée, une syllabe par note (séparez les syllabes d'un mot par « - », ex. : « le ma-tin se lè-ve »). Les sections entre crochets sont ignorées."));
    yyjson_mut_obj_add_strcpy(doc, t, "place_lyrics", tr("Placer sous les notes"));
    yyjson_mut_obj_add_strcpy(doc, t, "remove_lyrics", tr("Retirer les paroles"));
    yyjson_mut_obj_add_strcpy(doc, t, "abc_help", tr("Texte de la partition (format ABC) : chaque modification est redessinée aussitôt."));
    yyjson_mut_obj_add_strcpy(doc, t, "lyrics_placed", tr("%d syllabe(s) placée(s) sur %d."));
    yyjson_mut_obj_add_strcpy(doc, t, "bad_measures", tr("Mesures de durée incorrecte :"));
    yyjson_mut_obj_add_strcpy(doc, t, "chord_prompt", tr("Accord (ex. : Dm, G7, Cmaj7, F#m, Bb/D) ; vide pour l'enlever :"));
    yyjson_mut_obj_add_strcpy(doc, t, "not_editable", tr("(non modifiable ici : utilisez le texte ABC)"));
    yyjson_mut_obj_add_strcpy(doc, t, "down", tr("Note plus basse"));
    yyjson_mut_obj_add_strcpy(doc, t, "up", tr("Note plus haute"));
    yyjson_mut_obj_add_strcpy(doc, t, "oct_down", tr("Une octave plus bas"));
    yyjson_mut_obj_add_strcpy(doc, t, "oct_up", tr("Une octave plus haut"));
    yyjson_mut_obj_add_strcpy(doc, t, "sharp", tr("Dièse"));
    yyjson_mut_obj_add_strcpy(doc, t, "flat", tr("Bémol"));
    yyjson_mut_obj_add_strcpy(doc, t, "natural", tr("Bécarre"));
    yyjson_mut_obj_add_strcpy(doc, t, "dot", tr("Note pointée"));
    yyjson_mut_obj_add_strcpy(doc, t, "undo", tr("Annuler la dernière modification"));
    yyjson_mut_obj_add_strcpy(doc, t, "redo", tr("Rétablir la modification annulée"));
    yyjson_mut_obj_add_strcpy(doc, t, "rest_tip", tr("Remplacer la note par un silence"));
    yyjson_mut_obj_add_strcpy(doc, t, "duplicate_tip", tr("Répéter la note juste après"));
    yyjson_mut_obj_add_strcpy(doc, t, "delete_tip", tr("Retirer la note (la mesure devient plus courte)"));
    yyjson_mut_obj_add_strcpy(doc, t, "chord_tip", tr("Accord affiché au-dessus de la note"));
    yyjson_mut_obj_add_strcpy(doc, t, "whole", tr("Ronde"));
    yyjson_mut_obj_add_strcpy(doc, t, "half", tr("Blanche"));
    yyjson_mut_obj_add_strcpy(doc, t, "quarter", tr("Noire"));
    yyjson_mut_obj_add_strcpy(doc, t, "eighth", tr("Croche"));
    yyjson_mut_obj_add_strcpy(doc, t, "sixteenth", tr("Double croche"));
    yyjson_mut_obj_add_strcpy(doc, t, "instruments", tr("Instruments :"));
    yyjson_mut_obj_add_strcpy(doc, t, "hq_play", tr("Écouter (banque SoundFont)"));
    yyjson_mut_obj_add_strcpy(doc, t, "hq_wav", tr("Exporter en audio…"));
    yyjson_mut_obj_add_strcpy(doc, t, "sons_manage", tr("Gérer les sons…"));
    yyjson_mut_obj_add_strcpy(doc, t, "hq_play_tip", tr("Lecture avec la banque SoundFont choisie dans Réglages › Système › Sons de l'Atelier partition : rendu plus réaliste que l'écoute rapide, avec l'instrument de chaque portée."));
    yyjson_mut_obj_add_strcpy(doc, t, "sons_manage_tip", tr("Ouvre Réglages › Système pour télécharger des instruments (↓ = à télécharger) et des banques de sons."));
    yyjson_mut_obj_add_val(doc, o, "t", t);
    {  // 1.0.97 : instruments disponibles et banque SoundFont
        yyjson_mut_val * arr = yyjson_mut_arr(doc);
        for (const SoundInst & si : sound_instruments()) {
            yyjson_mut_val * x = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, x, "id", si.id);
            yyjson_mut_obj_add_strcpy(doc, x, "label", tr(si.label));
            yyjson_mut_obj_add_int(doc, x, "program", si.program);
            yyjson_mut_obj_add_bool(doc, x, "installed", instrument_installed(si.id));
            yyjson_mut_arr_add_val(arr, x);
        }
        yyjson_mut_obj_add_val(doc, o, "sons", arr);
        const fs::path bank = current_bank();
        yyjson_mut_obj_add_strcpy(doc, o, "banque", bank.empty() ? "" : narrow(bank.filename().wstring()).c_str());
    }
    size_t      len  = 0;
    char *      json = yyjson_mut_write(doc, 0, &len);
    std::string msg(json ? json : "{}", len);
    free(json);
    yyjson_mut_doc_free(doc);
    AtelierCallbacks cb;
    cb.on_message = [this](const std::string & m) { atelier_message(m); };
    cb.on_pdf = [this](bool ok, const std::wstring & path) {
        notify(ok ? trf("Partition enregistrée en PDF : %s", narrow(path).c_str()) : std::string(tr("Export PDF impossible.")));
        atelier_post("{\"type\":\"pdf_done\"}");  // la page reprend l'affichage normal
    };
    cb.on_ready = [this](const std::string &) { atelier_ready_at = ImGui::GetTime(); };
    const std::string wtitle = title.empty() || title == tr("Atelier partition") ? std::string(tr("Atelier partition")) : tr("Atelier partition") + std::string(" — ") + title;
    atelier_set_audio_dir((studio->paths().cache / L"partition").wstring());  // 1.0.97 : sons rendus
    std::error_code ec_audio;
    fs::create_directories(studio->paths().cache / L"partition", ec_audio);
    if (!atelier_open(hwnd, widen(wtitle), app_resource_dir(L"partition").wstring(),
                      (studio->paths().cache / L"webview2").wstring(), msg, cb))
        notify(tr("Impossible d'ouvrir l'Atelier partition."));
}

// Messages de la page de l'atelier (JSON) : « pdf » (export PDF fait par WebView2), « save » (fichier fourni par la page :
// MIDI en base 64, MusicXML ou ABC en texte), enregistré où l'utilisateur le choisit.
void App::atelier_message(const std::string & m) {
    yyjson_doc * doc = yyjson_read(m.data(), m.size(), 0);
    if (!doc) return;
    yyjson_val * root = yyjson_doc_get_root(doc);
    auto         str  = [&](const char * k) { const char * v = yyjson_get_str(yyjson_obj_get(root, k)); return std::string(v ? v : ""); };
    const std::string type = str("type"), name = str("name").empty() ? std::string("partition") : str("name");
    if (type == "pdf") {
        const fs::path path = ask_export_path(name, "pdf");
        if (path.empty()) atelier_post("{\"type\":\"pdf_done\"}");  // annulé : la page reprend l'affichage normal
        else if (!atelier_print_pdf(path.wstring())) notify(tr("Export PDF impossible."));
    } else if (type == "use") {  // 1.0.93 : partition (importée ou retouchée) envoyée dans l'onglet « Partition »
        const std::string abc = str("abc");
        if (!trim(abc).empty()) {
            score_abc = score_original = abc;
            score_source               = name;
            score_tempo_edit           = parse_score(abc).tempo;
            form_tab_request           = 2;
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            notify(trf("Partition « %s » placée dans l'onglet « Partition » : choisissez le style et les paroles dans « Nouveau morceau », puis « Générer avec cette partition ».", name.c_str()));
        }
    } else if (type == "sons_settings") {  // 1.0.97 : « Gérer les sons… » : Réglages › Système
        open_settings_cat = 4;
        open_settings     = true;
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    } else if (type == "render") {  // 1.0.97 : lecture ou export WAV avec la banque SoundFont
        start_render(str("midi"), yyjson_get_bool(yyjson_obj_get(root, "play")), name);
    } else if (type == "save") {
        const std::string ext = str("ext"), data = str("data");
        const bool        b64 = yyjson_get_bool(yyjson_obj_get(root, "base64"));
        std::string       bytes = data;
        if (b64) {  // décodage base 64 (crypt32)
            DWORD n = 0;
            CryptStringToBinaryA(data.c_str(), (DWORD) data.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr);
            bytes.assign(n, '\0');
            if (!n || !CryptStringToBinaryA(data.c_str(), (DWORD) data.size(), CRYPT_STRING_BASE64, (BYTE *) bytes.data(), &n, nullptr, nullptr)) bytes.clear();
        }
        fs::path path;
        if (const char * dir = getenv("STUDIO_TEST_EXPORT_DIR")) path = fs::path(widen(dir)) / widen(safe_filename(name) + "." + ext);  // essai sans clic
        else if (!bytes.empty()) path = ask_export_path(name, ext);
        if (!path.empty()) {
            std::ofstream out(path, std::ios::binary);
            out.write(bytes.data(), (std::streamsize) bytes.size());
            notify(out ? trf("Partition enregistrée : %s", narrow(path.wstring()).c_str()) : std::string(tr("Enregistrement impossible.")));
        } else if (bytes.empty()) {
            notify(tr("Export impossible : fichier vide."));
        }
    }
    yyjson_doc_free(doc);
}

fs::path App::ask_export_path(const std::string & name, const std::string & ext) {
    std::wstring  wext           = widen(ext);
    std::wstring  def            = widen(safe_filename(name) + "." + ext);
    wchar_t       file[MAX_PATH] = {};
    wcsncpy_s(file, def.c_str(), _TRUNCATE);
    const std::string label = ext == "pdf" ? "PDF" : ext == "mid" ? "MIDI" : ext == "wav" ? "WAV" : ext == "musicxml" ? "MusicXML" : "ABC";
    std::wstring  filter = widen(label) + L" (*." + wext + L")" + L'\0' + L"*." + wext + L'\0' + L'\0';
    std::wstring  title  = ext == "pdf" ? wtr("Exporter la partition en PDF") : wtr("Exporter la partition");
    std::wstring  dir    = atelier_dir.wstring();
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner       = nullptr;
    ofn.lpstrFilter     = filter.c_str();
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrDefExt     = wext.c_str();
    ofn.lpstrTitle      = title.c_str();
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? fs::path(file) : fs::path();
}

std::vector<Song> App::done_songs_of(const std::vector<Song> & all, const std::string & key) {
    std::vector<Song> out;
    for (const Song & s : all) {
        if (s.status != "done" || !s.long_rest.empty()) continue;
        if (!key.empty() && (s.album.empty() || album_key(s.artist, s.album) != key)) continue;
        out.push_back(s.long_join > 0 ? with_full_lyrics(s, all) : s);
    }
    return out;
}

bool App::stems_engine(fs::path & exe, fs::path & model) const {
    std::error_code ec;
    exe.clear();
    model.clear();
    const fs::path d = app_dir() / L"outils" / L"demucs";
    if (fs::exists(d, ec)) for (const auto & e : fs::directory_iterator(d, ec)) if (e.path().extension() == L".exe") { exe = e.path(); break; }
    const fs::path m = app_dir() / L"modeles" / L"stems";
    if (fs::exists(m, ec)) for (const auto & e : fs::directory_iterator(m, ec)) if (e.path().extension() == L".bin") { model = e.path(); break; }
    return !exe.empty() && !model.empty();
}

void App::tool_started(const std::string & what) { notify(what); }

void App::poll_stems_download() {
    if (!stems_dl_wait) return;
    auto j = downloader.find("stems");
    if (!j || j->state == 0) return;
    stems_dl_wait = false;
    notify(j->state == 1 ? tr("Modèle de séparation téléchargé.") : tr("Échec du téléchargement du modèle de séparation."));
}

// Entrées de menu communes : un morceau (label = son titre) ou tout un album (label = album, key = clé de l'album).
void App::draw_tools_menu(const std::vector<Song> & songs, const std::string & label, bool album) {
    if (songs.empty()) return;
    const bool busy_gpu = studio->local_active() || ai_local_busy();
    if (ImGui::MenuItem(tr("Masteriser (volume normalisé)…"))) {
        tool_songs = songs; tool_label = label; open_master = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Volume et crête adaptés à YouTube, TikTok ou aux plateformes de streaming. Les fichiers sont écrits dans le dossier « master » de chaque morceau ; l'original n'est pas modifié."));
    if (ImGui::MenuItem(tr("Paroles synchronisées (.lrc, .srt)"), nullptr, false, !busy_gpu)) {
        auto j = new_job(tr("Paroles synchronisées — ") + label);
        start_lrc(j, songs, studio->paths().whisper, studio->paths().models_dir / "voix" / whisper_file().name);
        tool_started(tr("Calage des paroles lancé."));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", busy_gpu ? tr("Le calage se fait au repos : attendez la fin de la génération en cours.") : tr("Whisper cale chaque ligne sur le chant : fichiers « paroles.lrc » (karaoké des lecteurs) et « paroles.srt » (sous-titres) dans le dossier du morceau."));
    fs::path dx, dm;
    const bool stems_ok = stems_engine(dx, dm);
    if (!stems_ok && !dx.empty() && dm.empty()) {  // moteur présent, modèle absent : téléchargement au clic (84 Mo)
        if (ImGui::MenuItem(tr("Télécharger le modèle de séparation (84 Mo)…"))) {
            downloader.start("stems", tr("Modèle de séparation des pistes"), studio->paths().models_dir / "stems",
                             { DlFile{ "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/ggml-model-htdemucs-4s-f16.bin", "ggml-model-htdemucs-4s-f16.bin", 83994361ull } });
            stems_dl_wait = true;
            notify(tr("Téléchargement du modèle de séparation lancé (84 Mo)."));
        }
    }
    if (ImGui::MenuItem(tr("Séparer les pistes (voix, batterie, basse, autres)"), nullptr, false, stems_ok && !busy_gpu)) {
        auto j = new_job(tr("Séparation des pistes — ") + label);
        start_stems(j, songs, dx, dm);
        tool_started(tr("Séparation des pistes lancée (quelques minutes par morceau)."));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", stems_ok ? tr("Quatre fichiers WAV (voix, batterie, basse, autres) dans le dossier « stems » du morceau.") : tr("Moteur de séparation absent (dossiers « outils\\demucs » et « modeles\\stems »)."));
    if (!album && songs.size() == 1) {  // 1.0.97 : partition de tous les instruments
        const bool st_ok = song_has_stems(songs[0]);
        if (ImGui::MenuItem(tr("Partition de tous les instruments (stems)…"), nullptr, false, st_ok && !busy_gpu)) start_stem_score(songs[0]);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !st_ok ? tr("Séparez d'abord les pistes (voix, batterie, basse, autres) de ce morceau.") : tr("SheetSage2 transcrit chaque instrument séparé (voix, basse, autres ; la batterie est ignorée) et l'Atelier partition les affiche sur des portées distinctes, toutes modifiables. Environ 1 minute par instrument ; l'alignement est indicatif."));
    }
    if (ImGui::MenuItem(album ? tr("Contrôle tonalité et tempo de l'album (SheetSage2)") : tr("Contrôle tonalité et tempo de ce morceau (SheetSage2)"), nullptr, false, !busy_gpu)) start_key_check(songs, label);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", busy_gpu ? tr("Le contrôle se fait au repos : attendez la fin de la génération en cours.") : tr("SheetSage2 réécoute le morceau et compare sa tonalité et son tempo à la partition (ou au BPM du style) : les morceaux qui dérivent reçoivent un pouce rouge. Environ 1 minute pour 3 minutes de musique."));
    if (ImGui::MenuItem(tr("Exporter pour un logiciel de musique…"))) {
        fs::path dest = pick_folder(hwnd, L"Dossier de destination");
        if (!dest.empty()) {
            auto j = new_job(tr("Export logiciel de musique — ") + label);
            start_daw_export(j, songs, dest);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Copie l'audio (version masterisée si elle existe), la partition ABC, les paroles et les stems, avec le tempo et la tonalité dans le nom du fichier."));
    // 1.0.98 (atelier clip, point 4) : fiches texte des anciens morceaux, sans attendre un clip. Rapide (fichiers texte seuls) :
    // fait tout de suite. paroles.txt déjà présent n'est jamais remplacé (il a pu être corrigé à la main).
    if (ImGui::MenuItem(tr("Écrire les fiches texte (paroles, morceau, album, groupe)"))) {
        int n = 0;
        for (const Song & s : songs) {
            if (!s.long_rest.empty()) continue;  // partie 1 d'un morceau long : la fiche est celle du morceau final
            write_fiches(s, song_folder(s.id));
            n++;
        }
        notify(trf("Fiches texte écrites pour %d morceau(x).", n));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("« paroles.txt » et « fiche.txt » dans le dossier de chaque morceau, « fiche_album.txt » et « fiche_groupe.txt » au-dessus. Le clip s'en sert pour son scénario. Un « paroles.txt » déjà présent n'est jamais remplacé."));
    if (ImGui::MenuItem(tr("Fiche de publication (IA locale)"), nullptr, false, !busy_gpu)) start_publication(songs);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr("Titre, description et hashtags pour YouTube, TikTok ou Instagram, écrits par l'IA locale dans « publication.txt ». Un fichier déjà présent n'est jamais remplacé. Rien n'est publié : vous copiez et publiez vous-même."));
    if (album && ImGui::MenuItem(tr("Livret PDF de l'album"))) {
        std::vector<Song> ordered = songs;
        std::stable_sort(ordered.begin(), ordered.end(), [](const Song & a, const Song & b) { return track_number(a) < track_number(b); });
        const std::string key = album_key(ordered[0].artist, ordered[0].album);
        std::string cover = cover_for(key);
        if (cover.empty()) cover = cover_for(slot_youtube(key));
        auto j = new_job(tr("Livret — ") + label);
        start_booklet(j, ordered, ordered[0].artist, ordered[0].album, cover.empty() ? fs::path() : fs::path(widen(cover)), album_folder(ordered[0].artist, ordered[0].album));
    }
    if (album && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Pochette, liste des morceaux et paroles, au format A5 : « livret.pdf » dans le dossier de l'album."));
}

void App::start_publication(const std::vector<Song> & songs) {
    LocalLlm llm;
    fs::path model;
    int      layers = 0;
    if (pub_job && !pub_job->finished) { notify(tr("Des fiches de publication sont déjà en cours.")); return; }
    if (!local_text_ready(llm, model, layers)) { notify(tr("IA locale de texte non prête (Réglages › IA).")); return; }
    auto job   = std::make_shared<DescJob>();
    job->total = (int) songs.size();
    pub_job    = job;
    pub_ids.clear();
    for (const Song & s : songs) pub_ids.push_back(s.id);
    const std::string lang = i18n::current();
    std::thread([job, songs, lang, llm, model, layers] {
        std::atomic<int> stage{ 0 };
        std::string      err;
        AiCallResult     r = ai_local_session(llm, model, layers, &stage, &job->cancel, [&](const AiAsk & ask) {
            write_publication(songs, lang, ask, [&](const std::string & what) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->current = what;
                job->done++;
            }, job->cancel, job->written, err);
        });
        std::lock_guard<std::mutex> lock(job->mutex);
        job->error    = !err.empty() ? err : r.ok ? std::string() : r.error;
        job->finished = true;
    }).detach();
    notify(tr("Fiches de publication lancées (IA locale)."));
}

void App::load_pub_text() {
    pub_text.clear();
    if (pub_sel < 0 || pub_sel >= (int) pub_ids.size()) return;
    std::ifstream f(song_folder(pub_ids[pub_sel]) / L"publication.txt", std::ios::binary);
    if (f) pub_text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void App::poll_publication() {
    if (!pub_job || !pub_job->finished) return;
    auto job = pub_job;
    pub_job.reset();
    if (!job->error.empty()) { notify(trf("Fiches de publication : %s", job->error.c_str())); return; }
    notify(trf("%d fiche(s) de publication écrite(s) (fichiers « publication.txt »).", job->written.load()));
    pub_sel = 0;
    load_pub_text();
    open_pub = !pub_ids.empty();
}

void App::draw_pub_modal() {
    if (open_pub) { ImGui::OpenPopup(tr("Fiche de publication")); open_pub = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Fiche de publication"), nullptr, ImGuiWindowFlags_NoResize)) return;
    const std::vector<Song> all = studio->snapshot();
    auto name_of = [&](const std::string & id) { for (const Song & s : all) if (s.id == id) return clean_title(display_title(s.title)); return id; };
    if (pub_ids.size() > 1) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##pubsong", name_of(pub_ids[std::clamp(pub_sel, 0, (int) pub_ids.size() - 1)]).c_str())) {
            for (int i = 0; i < (int) pub_ids.size(); i++)
                if (ImGui::Selectable(name_of(pub_ids[i]).c_str(), i == pub_sel)) { pub_sel = i; load_pub_text(); }
            ImGui::EndCombo();
        }
    } else if (!pub_ids.empty()) {
        ImGui::PushFont(g_bold);
        ImGui::TextUnformatted(name_of(pub_ids[0]).c_str());
        ImGui::PopFont();
    }
    if (pub_text.empty()) muted_wrapped(tr("Aucune fiche pour ce morceau (le texte de l'IA n'a pas pu être utilisé : relancez la fiche)."));
    else lyrics_box("##pubtext", pub_text, 240 * g_dpi);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::BeginDisabled(pub_text.empty());
    if (soft_button(tr("Tout copier"), P.accent)) ImGui::SetClipboardText(pub_text.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (soft_button(tr("Ouvrir YouTube Studio"), P.teal)) ShellExecuteW(nullptr, L"open", L"https://studio.youtube.com", nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (soft_button(tr("Ouvrir TikTok"), P.teal)) ShellExecuteW(nullptr, L"open", L"https://www.tiktok.com/upload", nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::draw_master_modal() {
    if (open_master) { ImGui::OpenPopup(tr("Mastering")); open_master = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Mastering"), nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(tool_label.c_str());
    ImGui::PopFont();
    muted_wrapped(trf("%d morceau(x). Volume normalisé en deux passes (loudnorm) avec limiteur de crête ; les fichiers sont écrits dans le dossier « master » de chaque morceau, l'original reste intact.", (int) tool_songs.size()));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::RadioButton(tr("Streaming et YouTube : −14 LUFS"), &master_preset, 0);
    ImGui::RadioButton(tr("Réseaux courts (TikTok, Instagram) : −11 LUFS"), &master_preset, 1);
    ImGui::RadioButton(tr("Doux, dynamique préservée : −16 LUFS"), &master_preset, 2);
    ImGui::Checkbox(tr("Écrire aussi un MP3 320 kb/s"), &master_mp3);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (accent_button(tr("Masteriser"), ImVec2(0, 0), &P.green)) {
        static const double lufs[] = { -14, -11, -16 };
        auto j = new_job(tr("Mastering — ") + tool_label);
        start_master(j, tool_songs, lufs[std::clamp(master_preset, 0, 2)], -1.0, master_mp3);
        notify(tr("Mastering lancé."));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::start_dups(const std::vector<Song> & all) {
    if (dup_job && dup_job->state == 0) { notify(tr("La recherche de doublons est déjà en cours.")); return; }
    dup_found = std::make_shared<std::vector<DupPair>>();
    dup_shown = false;
    dup_job   = new_job(tr("Doublons — toute la bibliothèque"));
    start_duplicates(dup_job, all, dup_found, studio->paths().library / L"doublons.txt", analysis_dir());
    notify(tr("Recherche de doublons lancée."));
}

void App::draw_dup_modal() {
    if (dup_job && dup_job->state != 0 && dup_found && !dup_shown) { dup_shown = true; open_dup = true; }
    if (open_dup) { ImGui::OpenPopup(tr("Doublons")); open_dup = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(820 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Doublons"), nullptr, ImGuiWindowFlags_NoResize)) return;
    std::vector<DupPair> found;
    if (dup_found && dup_job) { std::lock_guard<std::mutex> lock(dup_job->mutex); found = *dup_found; }
    if (found.empty()) muted_wrapped(tr("Aucun couple de morceaux trop proches : compositions, paroles, sons et mélodies sont tous différents."));
    else {
        muted_wrapped(tr("Couples dont la composition (jetons du modèle), les paroles, le son ou la mélodie (empreinte audio) se ressemblent beaucoup. Un clic sur un titre le cherche dans la bibliothèque ; rapport complet : « doublons.txt »."));
        if (ImGui::BeginChild("##dups", ImVec2(0, 340 * g_dpi), ImGuiChildFlags_Borders)) {
            int n = 0;
            for (const DupPair & d : found) {
                ImGui::PushID(n++);
                if (ImGui::Selectable(d.label_a.c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0))) { filter_text = d.label_a; ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                ImGui::TextUnformatted("↔");
                ImGui::SameLine();
                if (ImGui::Selectable(d.label_b.c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x * 0.75f, 0))) { filter_text = d.label_b; ImGui::CloseCurrentPopup(); }
                ImGui::TextColored(P.muted, "   %s — %d %% %s, %d %% %s", d.why.c_str(), (int) (d.tokens * 100), tr("composition"), (int) (d.lyrics * 100), tr("paroles"));
                if (d.sound > -1.5 || d.melody >= 0) {  // 1.1.007 : empreinte audio
                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(P.muted, ", %d %% %s, %d %% %s", (int) (std::max(0.0, d.sound) * 100), tr("son"), (int) (std::max(0.0, d.melody) * 100), tr("mélodie"));
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Essais : quand toutes les versions d'un même envoi sont finies, la meilleure est désignée (durée proche de la cible, pas de silence anormal).
void App::poll_trials() {
    const std::vector<Song> all = studio->snapshot();
    for (size_t g = 0; g < trial_groups.size();) {
        std::vector<Song> songs;
        bool pending = false;
        for (const std::string & id : trial_groups[g])
            for (const Song & s : all)
                if (s.id == id) { if (s.active()) pending = true; else if (s.status == "done") songs.push_back(s); }
        if (pending) { g++; continue; }
        trial_groups.erase(trial_groups.begin() + g);
        if (songs.size() < 2) continue;
        auto tj   = std::make_shared<TrialJob>();
        tj->songs = songs;
        trial_jobs.push_back(tj);
        std::thread([tj] { tj->best = best_trial(tj->songs, tj->problems); tj->finished = true; }).detach();
    }
    for (size_t i = 0; i < trial_jobs.size();) {
        if (!trial_jobs[i]->finished) { i++; continue; }
        auto tj = trial_jobs[i];
        trial_jobs.erase(trial_jobs.begin() + i);
        std::string report;
        for (size_t k = 0; k < tj->songs.size(); k++)
            report += std::string(k == (size_t) tj->best ? "★ " : "  ") + display_title(tj->songs[k].title) + (tj->problems[k].empty() ? "" : " : " + tj->problems[k]) + "\n";
        std::ofstream(studio->paths().library / L"meilleurs_essais.txt", std::ios::binary | std::ios::app) << report << "\n";
        for (size_t k = 0; k < tj->songs.size(); k++)
            if ((int) k != tj->best && !tj->songs[k].validated && !tj->songs[k].flagged && !tj->problems[k].empty()) studio->set_flagged(tj->songs[k].id, true);
        notify(trf("Meilleur essai : « %s » (les autres versions sont conservées ; celles qui ont un défaut ont un pouce rouge).", display_title(tj->songs[tj->best].title).c_str()));
    }
}

// Session de nuit : à la fin du travail de la file, rapport puis veille ou extinction (60 s pour annuler).
void App::poll_night() {
    const std::vector<Song> all = studio->snapshot();
    int active = 0;
    for (const Song & s : all) active += s.active();
    if (active > 0) {
        if (!night_busy) { night_busy = true; night_start = (double) time(nullptr); night_done = night_failed = 0; }
        return;
    }
    if (!night_busy) return;
    night_busy = false;
    std::ostringstream r;
    for (const Song & s : all) {
        if (s.finished < night_start) continue;
        if (s.status == "done") { night_done++; r << "OK     " << display_title(s.title) << "\n"; }
        else if (s.status == "error") { night_failed++; r << "ERREUR " << display_title(s.title) << " : " << s.message << "\n"; }
    }
    if (night_done + night_failed == 0) return;
    if (settings.key_check_auto == "1") {  // 1.0.98 : contrôle tonalité et tempo des morceaux de la session
        std::vector<Song> fresh;
        for (const Song & s : all) if (s.status == "done" && s.finished >= night_start) fresh.push_back(s);
        start_key_check(fresh, tr("session"));
    }
    std::ofstream(studio->paths().library / L"rapport_nuit.txt", std::ios::binary | std::ios::trunc)
        << "\xEF\xBB\xBF" << night_done << " terminé(s), " << night_failed << " en erreur.\n\n" << r.str();
    notify(trf("File terminée : %d morceau(x) fini(s), %d en erreur (rapport « rapport_nuit.txt »).", night_done, night_failed));
    if (atoi(settings.night_action.c_str()) > 0) night_deadline = ImGui::GetTime() + 60;
}

void App::draw_night_modal() {
    if (night_deadline > 0 && !ImGui::IsPopupOpen(tr("Arrêt automatique"))) ImGui::OpenPopup(tr("Arrêt automatique"));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Arrêt automatique"), nullptr, ImGuiWindowFlags_NoResize)) return;
    const int left = std::max(0, (int) std::ceil(night_deadline - ImGui::GetTime()));
    const int act  = atoi(settings.night_action.c_str());
    ImGui::TextWrapped("%s", (act == 1 ? trf("La file est terminée. Mise en veille du PC dans %d s.", left) : trf("La file est terminée. Extinction du PC dans %d s.", left)).c_str());
    if (accent_button(tr("Annuler"), ImVec2(0, 0), &P.amber)) { night_deadline = 0; ImGui::CloseCurrentPopup(); }
    if (night_deadline > 0 && left == 0) {
        night_deadline = 0;
        ImGui::CloseCurrentPopup();
        if (act == 1) ShellExecuteW(nullptr, L"open", L"rundll32.exe", L"powrprof.dll,SetSuspendState 0,1,0", nullptr, SW_HIDE);
        else if (act == 2) ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/s /t 5", nullptr, SW_HIDE);
    }
    ImGui::EndPopup();
}

std::string App::fold_text(const std::string & s) {  // minuscules sans accents (lettres latines accentuées d'UTF-8)
    static const char map[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char) s[i];
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = (unsigned char) s[++i];
            out += d >= 0x80 && d <= 0xBF ? (char) tolower((unsigned char) map[d - 0x80]) : ' ';
        } else if (c < 0x80) out += (char) tolower(c);
        else out += ' ';
    }
    return out;
}

fs::path App::help_page_path() {
    fs::path        dir  = app_resource_dir(L"aide");
    fs::path        page = dir / "aide.html";
    std::error_code ec;
    if (i18n::current() != "fr")
        for (const std::string & code : { i18n::current(), std::string("en") }) {
            fs::path candidate = dir / ("aide." + code + ".html");
            if (fs::exists(candidate, ec)) { page = candidate; break; }
        }
    return page;
}

void App::load_help_sections() {
    if (help_loaded_lang == i18n::current() && !help_sections.empty()) return;
    help_sections.clear();
    help_loaded_lang = i18n::current();
    std::ifstream f(help_page_path(), std::ios::binary);
    std::string   html((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto strip = [](const std::string & h) {
        std::string out;
        bool        tag = false;
        for (size_t i = 0; i < h.size(); i++) {
            const char c = h[i];
            if (c == '<') {
                tag = true;
                const std::string t = h.substr(i, 5);
                if (t.compare(0, 3, "<br") == 0 || t.compare(0, 3, "<p>") == 0 || t.compare(0, 3, "<li") == 0 || t.compare(0, 3, "<tr") == 0 || t.compare(0, 4, "</p>") == 0) out += ' ';
            } else if (c == '>') tag = false;
            else if (!tag) out += c;
        }
        for (auto & [a, b] : std::vector<std::pair<std::string, std::string>>{ { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&nbsp;", " " }, { "&#39;", "'" } })
            for (size_t p; (p = out.find(a)) != std::string::npos;) out.replace(p, a.size(), b);
        std::string res;
        bool        space = false;
        for (char c : out) {
            if (isspace((unsigned char) c)) { space = true; continue; }
            if (space && !res.empty()) res += ' ';
            space = false;
            res += c;
        }
        return res;
    };
    std::vector<size_t> heads;
    for (size_t p = 0; (p = html.find("<h", p)) != std::string::npos; p += 2)
        if (p + 2 < html.size() && (html[p + 2] == '2' || html[p + 2] == '3') && html[p + 3] != 'r') heads.push_back(p);
    for (size_t k = 0; k < heads.size(); k++) {
        const size_t a = heads[k], b = k + 1 < heads.size() ? heads[k + 1] : html.size();
        const size_t tag_end = html.find('>', a);
        if (tag_end == std::string::npos || tag_end > b) continue;
        HelpSection s;
        const std::string tag = html.substr(a, tag_end - a);
        size_t            i   = tag.find("id=\"");
        if (i != std::string::npos) s.id = tag.substr(i + 4, tag.find('"', i + 4) - i - 4);
        const size_t close = html.find("</h", tag_end);
        s.title = strip(html.substr(tag_end + 1, (close == std::string::npos || close > b ? b : close) - tag_end - 1));
        s.text  = strip(html.substr(close == std::string::npos || close > b ? tag_end + 1 : close + 5, b - (close == std::string::npos || close > b ? tag_end + 1 : close + 5)));
        if (fold_text(s.title).find("journal") != std::string::npos) {  // le journal des évolutions : une rubrique par ligne du tableau
            const std::string part = html.substr(tag_end, b - tag_end);
            for (size_t r = 0; (r = part.find("<tr>", r)) != std::string::npos;) {
                const size_t e = part.find("</tr>", r);
                const std::string row = part.substr(r, e == std::string::npos ? std::string::npos : e - r);
                const size_t mid = row.find("</td>");
                if (mid != std::string::npos) {
                    HelpSection h;
                    h.title = "Nouveautés " + strip(row.substr(0, mid));
                    h.text  = strip(row.substr(mid));
                    h.id    = s.id;
                    if (h.text.size() > 20) help_sections.push_back(h);
                }
                if (e == std::string::npos) break;
                r = e;
            }
            continue;
        }
        if (!s.title.empty() && s.text.size() > 20) help_sections.push_back(s);
    }
}

void App::start_help_question() {
    if (help_q.empty() || (help_job && !help_job->finished)) return;
    LocalLlm llm;
    fs::path model;
    int      layers = 0;
    if (studio->local_active() || ai_local_busy()) { help_error = tr("L'IA locale se lance au repos : attendez la fin de la génération en cours."); return; }
    if (!local_text_ready(llm, model, layers)) { help_error = tr("IA locale de texte non prête (Réglages › IA)."); return; }
    load_help_sections();
    std::set<std::string> words;
    {
        static const std::set<std::string> stop = { "les", "des", "une", "que", "qui", "est", "pour", "dans", "sur", "comment", "peut", "puis", "avec", "the", "and", "for", "how", "what", "with", "can", "est-ce", "fait", "faire", "dois", "veux" };
        std::string cur;
        for (char c : fold_text(help_q) + " ") {
            if (isalnum((unsigned char) c)) cur += c;
            else { if (cur.size() >= 3 && !stop.count(cur)) words.insert(cur); cur.clear(); }
        }
    }
    std::vector<std::pair<int, int>> scored;
    {  // rareté des mots (IDF) : un mot présent dans toutes les rubriques ne compte presque pas ; le journal des évolutions est écarté
        std::vector<std::string> bodies, titles;
        for (const HelpSection & h : help_sections) { bodies.push_back(fold_text(h.text)); titles.push_back(fold_text(h.title)); }
        std::map<std::string, double> idf;
        for (const std::string & w : words) {
            int df = 0;
            for (size_t i = 0; i < bodies.size(); i++) df += bodies[i].find(w) != std::string::npos || titles[i].find(w) != std::string::npos;
            idf[w] = df ? std::log(1.0 + (double) bodies.size() / df) : 0;
        }
        for (int i = 0; i < (int) help_sections.size(); i++) {
            double score = 0;
            for (const std::string & w : words) {
                int n = 0;
                for (size_t p = 0; (p = bodies[i].find(w, p)) != std::string::npos && n < 6; p += w.size()) n++;
                score += idf[w] * (n + (titles[i].find(w) != std::string::npos ? 6 : 0));
            }
            if (score > 0) scored.push_back({ (int) (score * 100), i });
        }
    }
    std::sort(scored.begin(), scored.end(), [](auto & a, auto & b) { return a.first > b.first; });
    help_hits.clear();
    std::string context;
    for (size_t k = 0; k < scored.size() && k < 3; k++) {
        help_hits.push_back(scored[k].second);
        context += "## " + help_sections[scored[k].second].title + "\n" + help_sections[scored[k].second].text.substr(0, 1700) + "\n\n";
    }
    help_error.clear();
    help_answer.clear();
    if (context.empty()) { help_answer = tr("Aucune rubrique de l'aide ne parle de cela : reformulez avec d'autres mots, ou parcourez l'aide complète."); return; }
    auto job = std::make_shared<HelpJob>();
    help_job = job;
    const std::string system = "You answer questions about the desktop application Studio Audio AI using ONLY the help excerpts provided. "
                               "Answer in the same language as the question, in a few short sentences or steps. Name the buttons and menus exactly as written in the excerpts. "
                               "If the excerpts do not contain the answer, say so briefly. Never invent features.";
    const std::string user   = "Help excerpts:\n" + context + "Question: " + help_q;
    std::thread([job, llm, model, layers, system, user] {
        AiCallResult r = ai_local_chat(llm, model, layers, system, user, &job->stage, &job->cancel);
        job->text      = r.ok ? r.text : std::string();
        job->error     = r.ok ? std::string() : r.error;
        job->finished  = true;
    }).detach();
}

void App::poll_help() {
    if (!help_job || !help_job->finished) return;
    auto job = help_job;
    help_job.reset();
    help_answer = job->text;
    if (!job->error.empty()) help_error = tr(job->error.c_str());
}

// 1.1.005 : page Aide de la disposition B : question à l'IA en haut, aide affichée dessous par le navigateur intégré.
std::wstring App::help_url(const std::string & anchor) {
    return L"https://aide.studio/" + help_page_path().filename().wstring() + (anchor.empty() ? std::wstring() : L"#" + widen(anchor));
}

  // curseur dans le champ de question (« Question… »)
void App::draw_help_page() {
    ImGui::AlignTextToFramePadding();
    if (ImGui::Button("←")) helpview_back();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Page précédente"));
    ImGui::SameLine();
    if (ImGui::Button("→")) helpview_forward();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Page suivante"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Sommaire"))) helpview_navigate(help_url());
    ImGui::SameLine();
    const bool busy = help_job && !help_job->finished;
    const char * ask = tr("Demander à l'IA");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ask).x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    if (help_focus) { ImGui::SetKeyboardFocusHere(); help_focus = false; }
    if (tx::InputTextWithHint("##hq_page", tr("Ex. : comment faire un Short de 30 secondes ?"), &help_q, ImGuiInputTextFlags_EnterReturnsTrue) && !busy) start_help_question();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || help_q.empty());
    if (accent_button(ask, ImVec2(0, 0), &P.green)) start_help_question();
    ImGui::EndDisabled();
    if (busy) ImGui::TextColored(P.accent, "%s", help_job->stage == 0 ? tr("Chargement du modèle…") : tr("Réponse en cours…"));
    if (!help_error.empty()) colored_wrapped(P.amber, help_error);
    if (!help_answer.empty()) {
        lyrics_box("##ha_page", help_answer, 110 * g_dpi);
        if (!help_hits.empty() && !help_sections.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", tr("Rubriques :"));
            for (int h : help_hits) {
                if (h >= (int) help_sections.size()) continue;
                ImGui::SameLine();
                ImGui::PushID(h);
                if (ImGui::SmallButton(help_sections[h].title.c_str())) helpview_navigate(help_url(help_sections[h].id));
                ImGui::PopID();
            }
        }
    }
    // Le navigateur (fenêtre Windows posée sur le Studio) occupe le reste de la page ; caché quand une fenêtre ou un menu du Studio
    // s'ouvre, sinon il passerait par-dessus.
    const ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
    ImGui::Dummy(sz);
    const ImVec2 org = ImGui::GetMainViewport()->Pos;  // coordonnées client de la fenêtre principale
    RECT r{ (LONG) (p0.x - org.x), (LONG) (p0.y - org.y), (LONG) (p0.x - org.x + sz.x), (LONG) (p0.y - org.y + sz.y) };
    if (!helpview_show(hwnd, r, app_resource_dir(L"aide").wstring(), (studio->paths().cache / L"webview2").wstring(), help_url())) {
        ImGui::SetCursorScreenPos(p0);
        muted_wrapped(tr("L'aide intégrée a besoin du moteur Microsoft Edge WebView2 (fourni avec Windows 10 et 11)."));
        if (ImGui::Button(tr("Ouvrir l'aide dans le navigateur"))) ShellExecuteW(nullptr, L"open", help_page_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    helpview_set_visible(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
}

void App::draw_help_question() {
    if (open_helpq && settings.layout == 2) { open_helpq = false; b_page = 3; help_focus = true; return; }  // 1.1.005 : page Aide
    if (open_helpq) { ImGui::OpenPopup(tr("Aide interrogeable")); open_helpq = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Aide interrogeable"), nullptr, ImGuiWindowFlags_NoResize)) return;
    muted_wrapped(tr("Posez une question sur le Studio : l'IA locale répond d'après l'aide intégrée (rien n'est envoyé sur Internet)."));
    const bool busy = help_job && !help_job->finished;
    ImGui::SetNextItemWidth(-110 * g_dpi);
    if (tx::InputTextWithHint("##hq", tr("Ex. : comment faire un Short de 30 secondes ?"), &help_q, ImGuiInputTextFlags_EnterReturnsTrue) && !busy) start_help_question();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || help_q.empty());
    if (accent_button(tr("Demander"), ImVec2(0, 0), &P.green)) start_help_question();
    ImGui::EndDisabled();
    if (busy) ImGui::TextColored(P.accent, "%s", help_job->stage == 0 ? tr("Chargement du modèle…") : tr("Réponse en cours…"));
    if (!help_error.empty()) colored_wrapped(P.amber, help_error);
    if (!help_answer.empty()) lyrics_box("##ha", help_answer, 200 * g_dpi);
    if (!help_hits.empty() && !help_sections.empty()) {
        ImGui::TextColored(P.muted, "%s", tr("Pour en savoir plus (ouvre l'aide à la bonne rubrique) :"));
        for (int h : help_hits)
            if (h < (int) help_sections.size() && ImGui::Selectable(("  " + help_sections[h].title).c_str())) {
                const std::string url = "file:///" + narrow(help_page_path().wstring()) + (help_sections[h].id.empty() ? "" : "#" + help_sections[h].id);
                std::string fixed = url;
                for (char & c : fixed) if (c == '\\') c = '/';
                ShellExecuteW(nullptr, L"open", widen(fixed).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (ImGui::Button(tr("Fermer"))) { if (help_job) help_job->cancel = true; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

unsigned long long App::tree_bytes(const fs::path & p) {
    std::error_code ec;
    unsigned long long n = 0;
    if (fs::is_regular_file(p, ec)) return fs::file_size(p, ec);
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) n += it->file_size(ec);
    return n;
}

void App::start_disk_scan() {
    if (disk_scan && !disk_scan->finished) return;
    auto               scan = std::make_shared<DiskScan>();
    disk_scan = scan;
    disk_msg.clear();
    const fs::path    root = app_dir(), cache = studio->paths().cache, backups = backups_dir();
    std::vector<fs::path> folders;
    for (const Song & s : studio->snapshot()) folders.push_back(song_folder(s.id));
    std::thread([scan, root, cache, backups, folders] {
        auto add = [&](const std::string & label, std::vector<fs::path> paths, bool deletable) {
            DiskItem it;
            it.label = label;
            for (const fs::path & p : paths) { std::error_code ec; if (fs::exists(p, ec)) { it.paths.push_back(p); it.bytes += tree_bytes(p); } }
            it.deletable = deletable && !it.paths.empty();
            scan->items.push_back(it);
        };
        std::vector<fs::path> scenes, masters, stems;
        for (const fs::path & f : folders) { scenes.push_back(f / L"clip_scenes"); masters.push_back(f / L"master"); stems.push_back(f / L"stems"); }
        std::vector<fs::path> old_backups;
        {
            std::vector<fs::path> all;
            std::error_code ec;
            for (const auto & e : fs::directory_iterator(backups, ec)) if (e.is_directory(ec)) all.push_back(e.path());
            std::sort(all.begin(), all.end());
            for (size_t i = 0; i + 3 < all.size(); i++) old_backups.push_back(all[i]);
        }
        add(tr("Fichiers temporaires (cache)"), { cache / L"temp" }, true);
        add(tr("Données de l'Atelier partition (WebView2)"), { cache / L"webview2" }, true);
        add(tr("Anciennes sauvegardes (les 3 dernières sont gardées)"), old_backups, true);
        add(tr("Images et scénarios des clips (refaits au prochain clip)"), scenes, true);
        add(tr("Versions masterisées (dossiers « master »)"), masters, true);
        add(tr("Pistes séparées (dossiers « stems »)"), stems, true);
        add(tr("Morceaux (Audios), vidéos comprises"), { root / L"Audios" }, false);
        add(tr("Modèles"), { root / L"modeles" }, false);
        add(tr("Programmes (outils, ressources)"), { root / L"outils", root / L"ressources" }, false);
        add(tr("Sauvegardes (total)"), { backups }, false);
        scan->finished = true;
    }).detach();
}

std::string App::fmt_bytes(unsigned long long b) {
    char buf[48];
    if (b >= (1ull << 30)) snprintf(buf, sizeof buf, "%.2f Go", b / 1073741824.0);
    else if (b >= (1ull << 20)) snprintf(buf, sizeof buf, "%.0f Mo", b / 1048576.0);
    else snprintf(buf, sizeof buf, "%.0f Ko", b / 1024.0);
    return buf;
}

void App::draw_disk_panel() {
    if (disk_scan && disk_scan->finished && disk_items.empty()) disk_items = disk_scan->items;
    if (getenv("STUDIO_TEST_DISK") && !disk_scan) start_disk_scan();
    if (getenv("STUDIO_TEST_DISK")) ImGui::SetNextItemOpen(true, ImGuiCond_Once);  // essai : panneau ouvert
    if (!ImGui::CollapsingHeader(tr("Espace disque et ménage"))) return;
    const bool scanning = disk_scan && !disk_scan->finished;
    ImGui::BeginDisabled(scanning);
    if (ImGui::SmallButton(tr("Analyser l'espace utilisé"))) { disk_items.clear(); disk_confirm = false; start_disk_scan(); }
    ImGui::EndDisabled();
    if (scanning) { ImGui::SameLine(); ImGui::TextColored(P.muted, "%s", tr("Analyse en cours…")); }
    if (!disk_msg.empty()) colored_wrapped(P.ok, disk_msg);
    unsigned long long sel = 0;
    for (DiskItem & it : disk_items) {
        ImGui::PushID(it.label.c_str());
        if (it.deletable) ImGui::Checkbox("##c", &it.checked);
        else { ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 1)); }
        ImGui::SameLine();
        ImGui::TextUnformatted(it.label.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 90 * g_dpi);
        ImGui::TextColored(it.deletable ? P.amber : P.muted, "%s", fmt_bytes(it.bytes).c_str());
        if (it.deletable && it.checked) sel += it.bytes;
        ImGui::PopID();
    }
    if (!disk_items.empty()) {
        ImGui::BeginDisabled(sel == 0 || studio->local_active());
        if (!disk_confirm) {
            if (soft_button(trf("Mettre à la Corbeille (%s)…", fmt_bytes(sel).c_str()).c_str(), P.amber)) disk_confirm = true;
        } else {
            colored_wrapped(P.amber, trf("Envoyer %s à la Corbeille de Windows ? Les fichiers restent récupérables tant que la Corbeille n'est pas vidée.", fmt_bytes(sel).c_str()));
            if (accent_button(tr("Oui, mettre à la Corbeille"), ImVec2(0, 0), &P.amber)) {
                std::vector<fs::path> paths;
                for (const DiskItem & it : disk_items) if (it.deletable && it.checked) paths.insert(paths.end(), it.paths.begin(), it.paths.end());
                std::string err;
                if (recycle_to_bin(paths, err)) { disk_msg = trf("%s envoyés à la Corbeille.", fmt_bytes(sel).c_str()); exists_cache.clear(); }
                else disk_msg = err;
                disk_confirm = false;
                disk_items.clear();
                disk_scan.reset();
                start_disk_scan();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Annuler"))) disk_confirm = false;
        }
        ImGui::EndDisabled();
    }
}

int App::style_bpm(const std::string & style) {
    const size_t p = style.find("BPM");
    if (p == std::string::npos) return 0;
    size_t e = p;
    while (e > 0 && style[e - 1] == ' ') e--;
    size_t b = e;
    while (b > 0 && isdigit((unsigned char) style[b - 1])) b--;
    return b < e ? atoi(style.substr(b, e - b).c_str()) : 0;
}

void App::start_key_check(const std::vector<Song> & songs, const std::string & label) {
    if (key_job && !key_job->finished) { notify(tr("Un contrôle de tonalité et de tempo est déjà en cours.")); return; }
    if (!studio->can_transcribe() || plan_busy() || studio->local_active() || ai_local_busy()) {
        notify(tr("Le contrôle de tonalité et de tempo se fait au repos (moteur de partition requis) : attendez la fin de la génération en cours."));
        return;
    }
    std::vector<std::pair<Song, fs::path>> work;
    for (const Song & s : songs)
        if (s.status == "done" && s.long_rest.empty() && s.long_join <= 0) work.push_back({ s, playable_file(s.id) });
    if (work.empty()) { notify(tr("Aucun morceau à contrôler.")); return; }
    auto job   = std::make_shared<KeyJob>();
    job->total = (int) work.size();
    key_job    = job;
    Studio * st = studio;
    notify(trf("Contrôle de tonalité et de tempo lancé (%d morceau(x)) : %s", (int) work.size(), label.c_str()));
    std::thread([job, work, st] {
        std::ostringstream rep, det;
        for (const auto & [s, audio] : work) {
            if (job->cancel) break;
            std::string err;
            const std::string abc = st->transcribe_score(audio, false, err);
            job->done++;
            const ScoreInfo heard = parse_score(abc);
            if (!heard.present) continue;
            std::ifstream sf(song_folder(s.id) / L"score.abc", std::ios::binary);
            const ScoreInfo want = parse_score(std::string((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>()));
            const int want_bpm = want.present && want.tempo > 0 ? want.tempo : style_bpm(s.style);
            det << display_title(s.title) << " : entendu " << heard.key << " " << heard.tempo << " BPM ; attendu " << (want.present ? want.key : std::string("?")) << " " << want_bpm << " BPM\n";
            std::string problems;
            if (want_bpm > 0 && heard.tempo > 0) {
                const double r = (double) heard.tempo / want_bpm;
                const bool ok = std::fabs(r - 1) <= 0.08 || std::fabs(r - 2) <= 0.16 || std::fabs(r - 0.5) <= 0.04;  // la moitié ou le double d'un tempo est le même battement
                if (!ok) problems += trf("tempo entendu %d BPM pour %d demandés", heard.tempo, want_bpm) + "; ";
            }
            if (want.present && !want.key.empty() && !heard.key.empty() && key_distance(want.key, heard.key) != 0)
                problems += trf("tonalité entendue %s pour %s écrite", heard.key.c_str(), want.key.c_str()) + "; ";
            if (!problems.empty()) {
                rep << display_title(s.title) << " : " << problems << "\n";
                std::lock_guard<std::mutex> lock(job->mutex);
                job->flagged.push_back(s.id);
            }
        }
        std::lock_guard<std::mutex> lock(job->mutex);
        job->report   = rep.str();
        job->details  = det.str();
        job->finished = true;
    }).detach();
}

void App::poll_key_check() {
    if (!key_job || !key_job->finished) return;
    auto job = key_job;
    key_job.reset();
    std::vector<std::string> flagged;
    std::string              report, details;
    { std::lock_guard<std::mutex> lock(job->mutex); flagged = job->flagged; report = job->report; details = job->details; }
    std::ofstream(studio->paths().library / L"controle_tonalite_tempo.txt", std::ios::binary | std::ios::trunc)
        << "\xEF\xBB\xBF" << (report.empty() ? "Aucune dérive de tonalité ni de tempo.\n" : report) << "\nDétail (entendu / attendu) :\n" << details;
    for (const Song & s : studio->snapshot())
        for (const std::string & id : flagged)
            if (s.id == id && !s.validated && !s.flagged) studio->set_flagged(id, true);
    notify(flagged.empty() ? tr("Tonalité et tempo : aucun morceau ne dérive.")
                           : trf("Tonalité et tempo : %d morceau(x) dérivent (pouce rouge, rapport « controle_tonalite_tempo.txt »).", (int) flagged.size()));
}

std::string App::sha256_file(const fs::path & file) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    std::string out;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return out;
    if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        std::ifstream f(file, std::ios::binary);
        std::vector<char> buf(1 << 20);
        while (f) {
            f.read(buf.data(), (std::streamsize) buf.size());
            if (f.gcount() > 0) CryptHashData(hash, (const BYTE *) buf.data(), (DWORD) f.gcount(), 0);
        }
        BYTE  h[32];
        DWORD n = 32;
        if (CryptGetHashParam(hash, HP_HASHVAL, h, &n, 0))
            for (DWORD i = 0; i < n; i++) { char b[4]; snprintf(b, sizeof b, "%02x", h[i]); out += b; }
        CryptDestroyHash(hash);
    }
    CryptReleaseContext(prov, 0);
    return out;
}

// Prépare l'installation : téléchargement, vérification, décompression (dans cache\maj). Tout échec arrête là : rien n'est modifié.
void App::start_update_download(const std::shared_ptr<UpdateInfo> & info) {
    if (update_job && update_job->state == 0) return;
    auto job = std::make_shared<UpdateJob>();
    update_job = job;
    const fs::path work = studio->paths().cache / L"maj";
    const std::string url = info->zip_url, digest = info->zip_digest, name = info->zip_name;
    const unsigned long long size = info->zip_size;
    std::thread([job, work, url, digest, name, size] {
        std::error_code ec;
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);
        auto fail = [&](const std::string & why) { job->error = why; job->state = 2; };
        if (url.empty() || name.empty()) return fail(tr("Cette publication n'a pas de fichier à télécharger."));
        if (digest.rfind("sha256:", 0) != 0) return fail(tr("Cette publication n'indique pas l'empreinte SHA-256 du fichier : installation refusée."));
        const fs::path zip = work / widen(name);
        std::wstring   err;
        job->total = size;
        if (!http_fetch(widen(url), zip, size, job->done, job->cancel, err)) return fail(job->cancel ? std::string(tr("Téléchargement annulé.")) : trf("Téléchargement impossible : %s", narrow(err).c_str()));
        if (sha256_file(zip) != digest.substr(7)) return fail(tr("L'empreinte du fichier téléchargé ne correspond pas : installation refusée (fichier supprimé)."));
        const fs::path out = work / L"extrait";
        fs::create_directories(out, ec);
        std::wstring cmd = L"tar.exe -xf " + quote_arg(zip.wstring()) + L" -C " + quote_arg(out.wstring());
        if (run_command(cmd, nullptr) != 0) return fail(tr("Décompression impossible."));
        fs::path root;
        for (const auto & e : fs::recursive_directory_iterator(out, ec))
            if (e.path().filename() == L"Studio Audio AI.exe") { root = e.path().parent_path(); break; }
        if (root.empty()) return fail(tr("Le fichier téléchargé ne contient pas le Studio."));
        job->extracted = narrow(root.wstring());
        fs::remove(zip, ec);
        job->state = 1;
    }).detach();
}

// Lance le script d'installation (après la fermeture du Studio) puis ferme le Studio.
bool App::launch_update_install(const std::string & src_dir, const std::string & from_version, std::string & error) {
    const fs::path app = app_dir(), work = studio->paths().cache / L"maj";
    std::error_code ec;
    const fs::path backup = backups_dir() / widen("avant_maj_" + from_version);
    fs::create_directories(backup, ec);
    for (const wchar_t * d : { L"outils", L"ressources", L"LICENSES" })
        if (fs::exists(app / d, ec)) fs::copy(app / d, backup / d, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    for (const auto & e : fs::directory_iterator(app, ec))
        if (e.is_regular_file(ec)) fs::copy_file(e.path(), backup / e.path().filename(), fs::copy_options::overwrite_existing, ec);
    if (!fs::exists(backup / L"Studio Audio AI.exe", ec)) { error = tr("Sauvegarde avant mise à jour impossible : installation annulée."); return false; }
    const fs::path script = work / L"maj.ps1";
    std::ofstream s(script, std::ios::binary | std::ios::trunc);
    s << "\xEF\xBB\xBF"
         "param([int]$PidApp,[string]$Src,[string]$Dst,[string]$Bak)\r\n"
         "$log = Join-Path $Dst 'cache\\maj\\maj.log'\r\n"
         "try { Wait-Process -Id $PidApp -Timeout 90 -ErrorAction SilentlyContinue } catch {}\r\n"
         "Start-Sleep -Seconds 1\r\n"
         "robocopy $Src $Dst /E /XD config cache donnees Audios modeles sauvegardes _sauvegardes /R:3 /W:1 /NFL /NDL /NJH /NJS | Out-Null\r\n"
         "$code = $LASTEXITCODE\r\n"
         "if ($code -ge 8) { robocopy $Bak $Dst /E /XD config cache donnees Audios modeles sauvegardes _sauvegardes /R:3 /W:1 /NFL /NDL /NJH /NJS | Out-Null; \"echec ($code) : version precedente restauree\" | Out-File $log }\r\n"
         "else { \"mise a jour installee ($code)\" | Out-File $log }\r\n"
         "Start-Process (Join-Path $Dst 'Studio Audio AI.exe')\r\n";
    s.close();
    std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File " + quote_arg(script.wstring()) + L" -PidApp " + std::to_wstring(GetCurrentProcessId()) +
                        L" -Src " + quote_arg(widen(src_dir)) + L" -Dst " + quote_arg(app.wstring()) + L" -Bak " + quote_arg(backup.wstring());
    ShellExecuteW(nullptr, L"open", L"powershell.exe", args.c_str(), nullptr, SW_HIDE);
    return true;
}

void App::draw_update_panel() {
    if (app_update->state != 3 || app_update->zip_url.empty()) return;
    ImGui::AlignTextToFramePadding();
    if (!update_job) {
        if (soft_button(trf("Mettre à jour en un clic (%s)…", app_update->latest.c_str()).c_str(), P.amber)) update_confirm = true;
    } else if (update_job->state == 0) {
        ImGui::TextColored(P.accent, "%s", trf("Téléchargement : %s / %s", fmt_bytes(update_job->done).c_str(), fmt_bytes(update_job->total).c_str()).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Annuler"))) update_job->cancel = true;
    } else if (update_job->state == 2) {
        colored_wrapped(P.danger, update_job->error);
        if (ImGui::SmallButton(tr("Réessayer"))) update_job.reset();
    } else {
        colored_wrapped(P.amber, tr("Téléchargement vérifié. Le Studio va se fermer, installer la nouvelle version puis se rouvrir. Vos réglages, morceaux, modèles et LoRA sont gardés ; une sauvegarde des programmes est faite avant."));
        ImGui::BeginDisabled(studio->local_active());
        if (accent_button(tr("Installer et redémarrer"), ImVec2(0, 0), &P.green)) {
            std::string err;
            if (launch_update_install(update_job->extracted, STUDIO_VERSION, err)) { g_force_close = true; PostMessageW(hwnd, WM_CLOSE, 0, 0); }
            else { update_job->error = err; update_job->state = 2; }
        }
        ImGui::EndDisabled();
        if (studio->local_active() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Une génération est en cours : attendez sa fin."));
    }
    if (update_confirm) {
        ImGui::OpenPopup(tr("Mettre à jour"));
        update_confirm = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(tr("Mettre à jour"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(520 * g_dpi);
        ImGui::TextWrapped("%s", trf("Télécharger la version %s depuis GitHub (%s) ? Le fichier est vérifié par son empreinte SHA-256 avant toute installation.", app_update->latest.c_str(), fmt_bytes(app_update->zip_size).c_str()).c_str());
        ImGui::PopTextWrapPos();
        if (accent_button(tr("Télécharger"), ImVec2(0, 0), &P.green)) { start_update_download(app_update); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool App::song_has_stems(const Song & s) const {
    std::error_code ec;
    const fs::path  d = song_folder(s.id) / L"stems";
    return fs::exists(d / L"voix.wav", ec) || fs::exists(d / L"basse.wav", ec) || fs::exists(d / L"autres.wav", ec);
}

void App::open_stem_score(const Song & s, const std::string & abc) {
    open_atelier(abc, display_title(s.title), s.artist + (s.album.empty() ? std::string() : " — " + s.album), display_title(s.title) + "_stems", song_folder(s.id), s.lyrics);
}

void App::start_stem_score(const Song & s) {
    const fs::path saved = song_folder(s.id) / L"score_stems.abc";
    std::error_code ec;
    if (fs::exists(saved, ec)) {  // déjà faite : ouverte telle quelle (supprimez le fichier pour la refaire)
        std::ifstream f(saved, std::ios::binary);
        open_stem_score(s, std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()));
        return;
    }
    if (stem_score_job && !stem_score_job->finished) { notify(tr("Une partition des instruments est déjà en cours.")); return; }
    if (!studio->can_transcribe() || plan_busy() || studio->local_active() || ai_local_busy()) {
        notify(tr("La partition des instruments se fait au repos (moteur de partition requis) : attendez la fin de la génération en cours."));
        return;
    }
    auto job = std::make_shared<StemScoreJob>();
    job->id  = s.id;
    stem_score_job = job;
    Studio * st = studio;
    const std::string title = display_title(s.title);
    const fs::path dir = song_folder(s.id) / L"stems";
    notify(trf("Partition des instruments lancée : « %s » (environ 1 minute par instrument).", title.c_str()));
    std::thread([job, st, title, dir, saved] {
        std::vector<std::array<std::string, 3>> parts;
        std::error_code ec;
        const std::pair<const char *, std::pair<const char *, const wchar_t *>> list[] = { { "Vocal", { "Voix", L"voix.wav" } }, { "Other", { "Autres", L"autres.wav" } }, { "Bass", { "Basse", L"basse.wav" } } };
        for (const auto & [id, info] : list) {
            if (!fs::exists(dir / info.second, ec)) continue;
            std::string err;
            const std::string abc = st->transcribe_score(dir / info.second, true, err);
            job->done++;
            if (abc.empty()) { job->notes += std::string(info.first) + " : " + err + " "; continue; }
            parts.push_back({ id, info.first, abc });
        }
        std::string notes;
        const std::string combined = abc_combine_stems(title, parts, notes);
        std::lock_guard<std::mutex> lock(job->mutex);
        job->notes += notes;
        if (combined.empty()) job->error = tr("Aucune partition n'a pu être écrite d'après les stems.");
        else {
            job->abc = combined;
            std::ofstream(saved, std::ios::binary | std::ios::trunc) << combined;
        }
        job->finished = true;
    }).detach();
}

void App::poll_stem_score() {
    if (!stem_score_job || !stem_score_job->finished) return;
    auto job = stem_score_job;
    stem_score_job.reset();
    if (!job->error.empty()) { notify(job->error); return; }
    for (const Song & s : studio->snapshot())
        if (s.id == job->id) { open_stem_score(s, job->abc); break; }
    if (!job->notes.empty()) notify(job->notes);
}

const std::vector<App::SoundInst> & App::sound_instruments() {
    static const std::vector<SoundInst> v = {
        { "acoustic_grand_piano", N_("Piano à queue"), 0 },          { "electric_piano_1", N_("Piano électrique"), 4 },
        { "acoustic_guitar_nylon", N_("Guitare classique (nylon)"), 24 }, { "acoustic_guitar_steel", N_("Guitare acoustique (acier)"), 25 },
        { "electric_guitar_clean", N_("Guitare électrique claire"), 27 }, { "electric_guitar_muted", N_("Guitare électrique étouffée"), 28 },
        { "overdriven_guitar", N_("Guitare saturée"), 29 },         { "distortion_guitar", N_("Guitare distordue"), 30 },
        { "acoustic_bass", N_("Contrebasse"), 32 },                  { "electric_bass_finger", N_("Basse électrique (doigts)"), 33 },
        { "electric_bass_pick", N_("Basse électrique (médiator)"), 34 }, { "slap_bass_1", N_("Basse slap"), 36 },
        { "synth_bass_1", N_("Basse synthé"), 38 },                  { "violin", N_("Violon"), 40 },
        { "cello", N_("Violoncelle"), 42 },                          { "string_ensemble_1", N_("Ensemble de cordes"), 48 },
        { "choir_aahs", N_("Chœur (aah)"), 52 },                     { "voice_oohs", N_("Voix (ooh)"), 53 },
        { "synth_choir", N_("Chœur synthétique"), 54 },              { "trumpet", N_("Trompette"), 56 },
        { "alto_sax", N_("Saxophone alto"), 65 },                    { "flute", N_("Flûte"), 73 },
        { "lead_1_square", N_("Synthé lead (carré)"), 80 },          { "pad_2_warm", N_("Nappe chaude"), 89 },
    };
    return v;
}

fs::path App::sons_dir() const { return app_resource_dir(L"partition") / L"sons"; }

bool     App::instrument_installed(const std::string & id) const {
    std::error_code ec;
    return id == "acoustic_grand_piano" || fs::exists(sons_dir() / widen(id + "-mp3") / L"ok.txt", ec);
}

void App::download_instrument(const std::string & id) {
    std::vector<DlFile> files;
    std::error_code     ec;
    for (const auto & e : fs::directory_iterator(sons_dir() / L"acoustic_grand_piano-mp3", ec))  // mêmes noms de notes pour tous les instruments
        if (e.path().extension() == L".mp3" && e.path().filename().wstring().find(L'#') == std::wstring::npos) {  // les « F# » sont des copies des « Gb »
            const std::string f = narrow(e.path().filename().wstring());
            files.push_back({ "https://gleitz.github.io/midi-js-soundfonts/FluidR3_GM/" + id + "-mp3/" + f, f, 0 });
        }
    if (files.empty()) return;
    std::string label = id;
    for (const SoundInst & s : sound_instruments()) if (id == s.id) label = tr(s.label);
    downloader.start("son-" + id, label, sons_dir() / widen(id + "-mp3"), files);
    sound_dl_wait.insert(id);
}

void App::poll_sound_downloads() {
    for (auto it = sound_dl_wait.begin(); it != sound_dl_wait.end();) {
        auto j = downloader.find(it->rfind("bank-", 0) == 0 ? *it : "son-" + *it);
        if (j && j->state != 0) {
            if (j->state == 1 && it->rfind("bank-", 0) != 0) {
                const fs::path d = sons_dir() / widen(*it + "-mp3");
                std::error_code ec;
                for (int o = 1; o <= 7; o++) fs::copy_file(d / (L"Gb" + std::to_wstring(o) + L".mp3"), d / (L"F#" + std::to_wstring(o) + L".mp3"), fs::copy_options::overwrite_existing, ec);
                std::ofstream(d / L"ok.txt") << "ok";
            }
            const bool bank = it->rfind("bank-", 0) == 0;
            notify(j->state == 1 ? (bank ? trf("Banque de sons téléchargée : %s", j->label.c_str()) : trf("Instrument téléchargé : %s", j->label.c_str()))
                                 : trf("Échec du téléchargement : %s", j->label.c_str()));
            it = sound_dl_wait.erase(it);
        } else ++it;
    }
}

const std::vector<App::SoundBank> & App::sound_banks() {
    static const std::vector<SoundBank> v = {
        { "gu", N_("GeneralUser GS (31 Mo, bon compromis, usage libre y compris commercial)"), "https://github.com/mrbumpy409/GeneralUser-GS/raw/main/GeneralUser-GS.sf2", "GeneralUser-GS.sf2", 32319396ull,
          "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main/documentation/LICENSE.txt", "LICENSE-GeneralUser-GS.txt" },
        { "fluid", N_("FluidR3 GM (142 Mo, généraliste, licence permissive)"), "https://github.com/pianobooster/fluid-soundfont/releases/download/v3.1/FluidR3_GM.sf2", "FluidR3_GM.sf2", 148398306ull, nullptr, nullptr },
    };
    return v;
}

fs::path App::bank_dir() const { return studio->paths().models_dir / L"sons" / L"banques"; }

fs::path App::current_bank() const {  // banque choisie si elle est installée, sinon la première installée
    std::error_code ec;
    const fs::path  chosen = bank_dir() / widen(settings.soundfont_bank);
    if (!settings.soundfont_bank.empty() && fs::exists(chosen, ec)) return chosen;
    for (const SoundBank & b : sound_banks()) if (fs::exists(bank_dir() / widen(b.file), ec) && fs::file_size(bank_dir() / widen(b.file), ec) == b.bytes) return bank_dir() / widen(b.file);
    return fs::path();
}

void App::download_bank(const SoundBank & b) {
    std::vector<DlFile> files = { { b.url, b.file, b.bytes } };
    if (b.license_url) files.push_back({ b.license_url, b.license_file, 0 });
    downloader.start(std::string("bank-") + b.id, tr(b.label), bank_dir(), files);
    sound_dl_wait.insert(std::string("bank-") + b.id);
}

void App::poll_render() {
    if (!render_job || !render_job->finished) return;
    auto job = render_job;
    render_job.reset();
    if (job->play) {
        std::string msg = job->ok ? "{\"type\":\"audio\",\"url\":\"https://rendu.atelier.studio/" + narrow(job->out.filename().wstring()) + "\"}"
                                  : "{\"type\":\"audio\",\"error\":\"" + job->error + "\"}";
        atelier_post(msg);
    } else {
        notify(job->ok ? trf("Audio enregistré : %s", narrow(job->out.wstring()).c_str()) : trf("Rendu audio impossible : %s", job->error.c_str()));
    }
}

std::string App::decode_b64(const std::string & data) {
    DWORD n = 0;
    CryptStringToBinaryA(data.c_str(), (DWORD) data.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr);
    std::string bytes(n, '\0');
    if (!n || !CryptStringToBinaryA(data.c_str(), (DWORD) data.size(), CRYPT_STRING_BASE64, (BYTE *) bytes.data(), &n, nullptr, nullptr)) bytes.clear();
    return bytes;
}

void App::start_render(const std::string & midi_b64, bool play, const std::string & name) {
    if (render_job && !render_job->finished) { notify(tr("Un rendu audio est déjà en cours.")); return; }
    const fs::path bank = current_bank();
    if (bank.empty()) { atelier_post("{\"type\":\"audio\",\"error\":\"" + std::string(tr("Aucune banque de sons installée : Réglages › Système › Sons de l'Atelier partition.")) + "\"}"); return; }
    const std::string midi = decode_b64(midi_b64);
    auto job  = std::make_shared<RenderJob>();
    job->play = play;
    if (play) {
        const fs::path dir = studio->paths().cache / L"partition";
        std::error_code ec;
        fs::create_directories(dir, ec);
        for (const auto & e : fs::directory_iterator(dir, ec)) if (e.path().filename().wstring().rfind(L"rendu_", 0) == 0) fs::remove(e.path(), ec);
        job->out = dir / (L"rendu_" + std::to_wstring(++render_seq) + L".wav");
    } else {
        job->out = ask_export_path(name.empty() ? std::string("partition") : name, "wav");
        if (job->out.empty()) return;
    }
    render_job = job;
    std::thread([job, midi, bank] {
        job->ok = render_midi_wav(midi, bank, job->out, job->error);
        job->finished = true;
    }).detach();
}

void App::draw_sound_panel() {
    if (getenv("STUDIO_TEST_SONS")) ImGui::SetNextItemOpen(true, ImGuiCond_Once);  // essai : panneau ouvert
    if (!ImGui::CollapsingHeader(tr("Sons de l'Atelier partition (instruments et banques)"))) return;
    muted_wrapped(tr("Instrument de chaque portée et lecture plus réaliste dans l'Atelier partition. Les instruments (échantillons FluidR3 GM, ≈ 2,5 Mo chacun) servent à l'écoute rapide ; une banque SoundFont donne la lecture de qualité et l'export audio (WAV). Tout est téléchargé à votre clic."));
    ImGui::TextColored(P.accent, "%s", tr("Banques SoundFont"));
    const fs::path cur = current_bank();
    for (const SoundBank & b : sound_banks()) {
        std::error_code ec;
        const bool installed = fs::exists(bank_dir() / widen(b.file), ec) && fs::file_size(bank_dir() / widen(b.file), ec) == b.bytes;
        auto       job       = downloader.find(std::string("bank-") + b.id);
        ImGui::PushID(b.id);
        if (installed) {
            const bool chosen = cur == bank_dir() / widen(b.file);
            if (ImGui::RadioButton(tr(b.label), chosen)) { settings.soundfont_bank = b.file; save_settings(settings, settings_path); }
        } else if (job && job->state == 0) {
            ImGui::TextColored(P.accent, "%s", trf("Téléchargement : %s — %s / %s", tr(b.label), fmt_bytes(job->done).c_str(), fmt_bytes(b.bytes).c_str()).c_str());
        } else {
            if (soft_button(tr("Télécharger"), P.green)) download_bank(b);
            ImGui::SameLine();
            ImGui::TextUnformatted(tr(b.label));
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::TextColored(P.accent, "%s", tr("Instruments"));
    if (soft_button(tr("Tout télécharger (≈ 60 Mo)"), P.green)) for (const SoundInst & s : sound_instruments()) if (!instrument_installed(s.id) && !downloader.find(std::string("son-") + s.id)) download_instrument(s.id);
    for (const SoundInst & s : sound_instruments()) {
        ImGui::PushID(s.id);
        const bool ok  = instrument_installed(s.id);
        auto       job = downloader.find(std::string("son-") + s.id);
        if (ok) ImGui::TextColored(P.ok, "● %s", tr(s.label));
        else if (job && job->state == 0) ImGui::TextColored(P.accent, "%s", trf("%s — téléchargement…", tr(s.label)).c_str());
        else {
            if (ImGui::SmallButton(tr("Télécharger"))) download_instrument(s.id);
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", tr(s.label));
        }
        ImGui::PopID();
    }
}

std::string App::lyrics_structure(const std::string & lyrics) {  // lignes « [Section] » dans l'ordre
    std::string out;
    size_t      i = 0;
    while (i <= lyrics.size()) {
        size_t      e = lyrics.find('\n', i);
        std::string t = trim(lyrics.substr(i, e == std::string::npos ? std::string::npos : e - i));
        if (t.size() > 2 && t.front() == '[' && t.back() == ']') out += t + "\n";
        if (e == std::string::npos) break;
        i = e + 1;
    }
    if (!out.empty()) out.pop_back();
    return out;
}

void App::lyrics_box(const char * id, const std::string & text, float h) {  // champ en lecture seule, sélection mémorisée pour le menu
    static std::map<ImGuiID, std::pair<int, int>> sel;
    ImGui::InputTextMultiline(id, const_cast<char *>(text.c_str()), text.size() + 1, ImVec2(-FLT_MIN, h), ImGuiInputTextFlags_ReadOnly);
    const ImGuiID bid = ImGui::GetItemID();
    if (ImGuiInputTextState * st = ImGui::GetInputTextState(bid); st && ImGui::GetActiveID() == bid)
        sel[bid] = { std::min(st->GetSelectionStart(), st->GetSelectionEnd()), std::max(st->GetSelectionStart(), st->GetSelectionEnd()) };
    const std::string menu = std::string("##lvm") + id;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) ImGui::OpenPopup(menu.c_str());
    if (ImGui::BeginPopup(menu.c_str())) {
        int a = std::clamp(sel[bid].first, 0, (int) text.size()), z = std::clamp(sel[bid].second, 0, (int) text.size());
        if (ImGui::MenuItem(tr("Copier la sélection"), "Ctrl+C", false, a < z)) ImGui::SetClipboardText(text.substr(a, z - a).c_str());
        if (ImGui::MenuItem(tr("Copier tout"), nullptr, false, !text.empty())) ImGui::SetClipboardText(text.c_str());
        ImGui::EndPopup();
    }
}

void App::draw_lyrics_view() {
    if (open_lyrics_view) {
        ImGui::OpenPopup(tr("Paroles et structure"));
        open_lyrics_view = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Paroles et structure"), nullptr, ImGuiWindowFlags_NoResize)) return;
    const std::vector<Song> all = studio->snapshot();
    Song                    target;
    for (const Song & s : all)
        if (s.id == lyrics_view_id) target = s;
    const Song        full      = target.long_join > 0 ? with_full_lyrics(target, all) : target;  // morceau long : toutes les parties
    const std::string structure = lyrics_structure(full.lyrics);
    const std::string words     = full.instrumental ? std::string() : trim(full.lyrics);
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(display_title(target.title).c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::TextColored(P.accent, "%s", tr("Structure"));
    if (structure.empty()) muted_wrapped(tr("Aucune structure enregistrée pour ce morceau."));
    else lyrics_box("##lv_structure", structure, std::min(190.0f, 22.0f + 18.0f * (float) std::count(structure.begin(), structure.end(), '\n')) * g_dpi);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (full.instrumental) {
        muted_wrapped(tr("Morceau instrumental : pas de paroles, seulement sa structure."));
    } else {
        ImGui::TextColored(P.accent, "%s", tr("Paroles"));
        if (words.empty()) muted_wrapped(tr("Aucune parole enregistrée pour ce morceau."));
        else lyrics_box("##lv_lyrics", words, 320 * g_dpi);
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    ImGui::BeginDisabled(words.empty());
    if (soft_button(tr("Copier les paroles"), P.teal)) ImGui::SetClipboardText(words.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(structure.empty());
    if (soft_button(tr("Copier la structure"), P.teal)) ImGui::SetClipboardText(structure.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(structure.empty() && words.empty());
    if (soft_button(tr("Tout copier"), P.accent)) {
        std::string all_text = structure;
        if (!words.empty()) all_text += (all_text.empty() ? "" : "\n\n") + words;
        ImGui::SetClipboardText(all_text.c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Petit bouton « Paroles » des cartes : ouvre la fenêtre ci-dessus.
// 1.1.010 : cartes repliées : « Instrumental » au lieu de « Paroles » pour un instrumental (même fenêtre : structure du morceau)
const char * App::lyrics_button_label(const Song & s, bool compact) { return compact && s.instrumental ? tr("Instrumental") : tr("Paroles"); }

float App::lyrics_button_width(const Song & s) const { return ImGui::CalcTextSize(lyrics_button_label(s, true)).x + 2 * ImGui::GetStyle().FramePadding.x; }

std::string App::redo_lyrics_text(const Song & s) {  // même texte que celui des paroles synchronisées
    std::string text = s.lyrics;
    std::ifstream pf(song_folder(s.id) / L"paroles.txt", std::ios::binary);
    if (pf) text.assign((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
    return text;
}

// Sections chantées et leur début ; vide si le morceau n'a pas de paroles synchronisées qui correspondent à ses paroles.
std::vector<App::RedoSection> App::redo_sections(const Song & s) {
    std::vector<RedoSection> out;
    const std::vector<ClipLine> lines = clip_lyrics_lines(redo_lyrics_text(s));
    const std::vector<ClipLine> lrc   = read_lrc(song_folder(s.id) / L"paroles.lrc", s.audio_seconds);
    if (lines.empty() || lrc.size() != lines.size()) return out;
    std::map<std::string, int> seen;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0 && lines[i].section == lines[i - 1].section) continue;
        const int n = ++seen[lower(lines[i].section)];
        out.push_back({ lines[i].section + (n > 1 ? " " + std::to_string(n) : std::string()), lrc[i].start, (int) i });
    }
    return out;
}

// Paroles à partir de la section de la ligne chantée « line » (balise de section comprise, sections vides suivantes comprises).
std::string App::redo_lyrics_from(const std::string & text, int line) {
    std::vector<std::string> rows;
    for (size_t i = 0; i <= text.size();) {
        size_t e = text.find('\n', i);
        rows.push_back(text.substr(i, e == std::string::npos ? std::string::npos : e - i));
        if (e == std::string::npos) break;
        i = e + 1;
    }
    int sung = -1, tag = -1;
    for (int r = 0; r < (int) rows.size(); r++) {
        const std::string t = trim(rows[r]);
        if (t.empty()) continue;
        if (t.front() == '[' && t.back() == ']') { tag = r; continue; }
        if (++sung == line) {
            std::string out;
            for (int k = tag >= 0 ? tag : r; k < (int) rows.size(); k++) out += rows[k] + "\n";
            return trim(out);
        }
    }
    return text;
}

  // paroles synchronisées en cours pour ce morceau
// Point de reprise : 1 s avant la première ligne chantée de la section (la mesure qui l'amène est refaite aussi).
double App::redo_cut(double section_start) { return std::max(2.0, section_start - 1.0); }

void App::draw_redo_modal() {
    const char * title = tr("Refaire à partir d'ici");
    if (open_redo) { ImGui::OpenPopup(title); open_redo = false; redo_pick = redo_pick_test; }
    ImGui::SetNextWindowSize(ImVec2(640 * g_dpi, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) return;
    Song s;
    for (const Song & x : studio->snapshot())
        if (x.id == redo_id) s = x;
    if (s.id.empty()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(display_title(s.title).c_str());
    ImGui::PopFont();
    muted_wrapped(tr("Le début du morceau est gardé tel quel ; le moteur recompose tout ce qui suit le point choisi (même style, mêmes réglages). Le résultat est un nouveau morceau : l'original ne change pas."));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    double from = -1;
    std::string lyrics;
    if (s.instrumental) {
        ImGui::TextColored(P.muted, "%s", tr("Refaire à partir de :"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        redo_seconds = std::clamp(redo_seconds, 2.f, std::max(3.f, (float) s.audio_seconds - 5.f));
        ImGui::SliderFloat("##redo_s", &redo_seconds, 2.f, std::max(3.f, (float) s.audio_seconds - 5.f), "%.0f s");
        ImGui::TextColored(P.muted, "%s", trf("soit %d:%02d sur %s", (int) redo_seconds / 60, (int) redo_seconds % 60, fmt_time(s.audio_seconds).c_str()).c_str());
        from   = redo_seconds;
        lyrics = s.lyrics;  // structure seule : le moteur la suit à partir du point
    } else {
        const bool busy = redo_lrc_job && redo_lrc_job->state == 0;
        if (redo_lrc_job && redo_lrc_job->state != 0) redo_lrc_job.reset();
        const std::vector<RedoSection> secs = busy ? std::vector<RedoSection>() : redo_sections(s);
        if (busy) {
            ImGui::TextColored(P.accent, "%s", tr("Calage des paroles sur le chant (Whisper)…"));
        } else if (secs.empty()) {
            colored_wrapped(P.amber, tr("Pour choisir une section, les paroles doivent être calées sur le chant (fichier paroles.lrc, environ 30 s)."));
            if (soft_button(tr("Caler les paroles"), P.teal)) {
                redo_lrc_job = std::make_shared<Job>();
                start_lrc(redo_lrc_job, { s }, studio->paths().whisper, studio->paths().models_dir / "voix" / whisper_file().name);
            }
        } else {
            ImGui::TextColored(P.muted, "%s", tr("Refaire à partir de la section :"));
            ImGui::BeginChild("##redo_secs", ImVec2(0, std::min(260.f * g_dpi, (secs.size() + 0.5f) * ImGui::GetFrameHeightWithSpacing())), ImGuiChildFlags_Borders);
            for (int i = 1; i < (int) secs.size(); i++) {  // la première section ne se refait pas : ce serait un nouveau morceau
                const std::string label = trf("%s  —  %d:%02d", secs[i].name.c_str(), (int) secs[i].start / 60, (int) secs[i].start % 60);
                if (ImGui::Selectable(label.c_str(), redo_pick == i)) redo_pick = i;
            }
            ImGui::EndChild();
            if (redo_pick > 0 && redo_pick < (int) secs.size()) {
                from   = redo_cut(secs[redo_pick].start);
                lyrics = redo_lyrics_from(redo_lyrics_text(s), secs[redo_pick].line);
                ImGui::TextColored(P.muted, "%s", tr("Paroles chantées à partir de ce point :"));
                std::string shown = lyrics;
                lyrics_box("##redo_lyr", shown, 140 * g_dpi);
            }
        }
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(from <= 0);
    if (accent_button(tr("Refaire"), ImVec2(0, 0), &P.violet)) {
        studio->submit_redo(s, from, s.instrumental ? lyrics : redo_lyrics_text(s));  // paroles complètes (essai du 8/11)
        notify(trf("« %s » : nouvelle version à partir de %d:%02d ajoutée à la file.", display_title(s.title).c_str(), (int) from / 60, (int) from % 60));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void App::ask_redo(const Song & s) {
    redo_id      = s.id;
    open_redo    = true;
    redo_seconds = (float) std::max(2.0, s.audio_seconds / 2);
}

void App::lyrics_button(const Song & s, bool compact) {
    ImGui::PushID("lyrics_btn");
    const bool clicked = ImGui::SmallButton(lyrics_button_label(s, compact));
    ImGui::PopID();
    if (clicked) { lyrics_view_id = s.id; open_lyrics_view = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Affiche les paroles et la structure du morceau, à sélectionner et copier."));
}

void App::draw_score_view() {
    if (open_score_view) {
        ImGui::OpenPopup(tr("Partition"));
        open_score_view = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Partition"), nullptr, ImGuiWindowFlags_NoResize)) return;
    Song target;
    for (const Song & s : studio->snapshot())
        if (s.id == score_view_id) target = s;
    const ScoreInfo & sc = score_info(target);
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(display_title(target.title).c_str());
    ImGui::PopFont();
    if (!sc.present) {
        muted_wrapped(tr("Pas de partition pour ce morceau : il a été créé en mode « Sans partition » ou avant la version 1.0.87."));
    } else {
        ImGui::TextColored(P.accent, "%s", trf("Tonalité : %s (%s)", key_name(sc.key).c_str(), sc.key.c_str()).c_str());
        ImGui::SameLine(0, 20 * g_dpi);
        ImGui::TextColored(P.accent, "%s", trf("Tempo : %d BPM", sc.tempo).c_str());
        ImGui::SameLine(0, 20 * g_dpi);
        ImGui::TextColored(P.accent, "%s", trf("Mesure : %s", sc.meter.c_str()).c_str());
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (ImGui::BeginTable("score_sections", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                              ImVec2(0, std::min(260.0f, 26.0f * (float) sc.sections.size() + 30) * g_dpi))) {
            ImGui::TableSetupColumn(tr("Section"), ImGuiTableColumnFlags_WidthFixed, 150 * g_dpi);
            ImGui::TableSetupColumn(tr("Accords"));
            ImGui::TableSetupScrollFreeze(0, 1);
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
        if (ImGui::CollapsingHeader(tr("Partition complète (texte ABC)"))) {
            std::string text = sc.text;
            ImGui::InputTextMultiline("##abc", text.data(), text.size() + 1, ImVec2(-FLT_MIN, 240 * g_dpi), ImGuiInputTextFlags_ReadOnly);
        }
        muted_wrapped(tr("La partition est le plan que le modèle a écrit avant de composer le son ; le morceau la suit (tonalité, tempo, accords)."));
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (sc.present && accent_button(tr("Voir en notes…"), ImVec2(0, 0), &P.violet)) {  // 1.0.91
        open_atelier(sc.text, display_title(target.title), target.artist + (target.album.empty() ? std::string() : " — " + target.album),
                     display_title(target.title), song_folder(target.id), target.lyrics);
        ImGui::CloseCurrentPopup();
    }
    if (sc.present) ImGui::SameLine();
    if (sc.present && soft_button(tr("Ouvrir le fichier"), P.teal)) {
        std::wstring arg = L"/select,\"" + (song_folder(target.id) / "score.abc").wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
    if (sc.present) ImGui::SameLine();
    if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// 1.0.62 : logos des plateformes sur la ligne de titre : gris, en couleur quand le clip du morceau existe. Largeur 0 si le morceau
// ne peut pas avoir de clip (pas terminé, ou partie 1 d'un morceau long). Fichiers cherchés comme les écrit la fenêtre « Créer des vidéos ».
float App::clip_logos_width(const Song & s) const {
    return s.status == "done" && s.long_rest.empty() ? (4 * 17 + 3 * 5) * g_dpi : 0;  // 1.0.78 : 4e logo, les Shorts
}

void App::draw_clip_logos(const Song & s) {
    if (clip_logos_width(s) <= 0) return;
    struct Net { const char * name; const wchar_t * dir, * suffix; };
    static const Net NETS[3] = { { "YouTube", L"YouTube", L"YouTube" }, { "TikTok", L"TikTok", L"TikTok" }, { "Instagram", L"IG", L"Instagram" } };
    const fs::path   videos = album_folder(s.artist, s.album) / L"videos";
    const std::wstring base = widen(safe_filename(display_title(s.title)));
    const float      h = 17 * g_dpi, step = 22 * g_dpi;
    ImVec2           p0 = ImGui::GetCursorScreenPos();
    ImDrawList *     dl = ImGui::GetWindowDrawList();
    const ImU32      grey = ImGui::GetColorU32(alpha(P.muted, 0.45f)), hole = ImGui::GetColorU32(P.card), white = IM_COL32_WHITE;
    for (int i = 0; i < 3; i++) {
        const fs::path stem = videos / NETS[i].dir / (base + L"_" + NETS[i].suffix);
        bool   clip  = cached_exists(fs::path(stem.wstring() + L"_Clip.mp4"));
        bool   fixed = !clip && cached_exists(fs::path(stem.wstring() + L".mp4"));  // 1.0.63 : vidéo fixe : couleur atténuée
        bool   done  = clip || fixed;
        auto   tone  = [&](ImU32 c) { return clip ? c : (c & 0x00FFFFFFu) | (0x70u << 24); };
        ImVec2 o    = p0 + ImVec2(step * i, 0);
        auto   at   = [&](float x, float y) { return o + ImVec2(x * h, y * h); };
        if (i == 0) {  // écran arrondi et triangle de lecture
            dl->AddRectFilled(at(0, .16f), at(1, .84f), done ? tone(IM_COL32(255, 0, 0, 255)) : grey, h * .2f);
            dl->AddTriangleFilled(at(.38f, .32f), at(.38f, .68f), at(.70f, .50f), done ? tone(white) : hole);
        } else if (i == 1) {  // note de musique, doublée en bleu et rouge quand le clip existe
            auto note = [&](float dx, float dy, ImU32 c) {
                dl->AddCircleFilled(at(.36f + dx, .72f + dy), h * .20f, c);
                dl->AddLine(at(.53f + dx, .72f + dy), at(.53f + dx, .10f + dy), c, h * .15f);
                dl->AddBezierQuadratic(at(.53f + dx, .12f + dy), at(.60f + dx, .34f + dy), at(.86f + dx, .36f + dy), c, h * .13f);
            };
            if (done) {
                note(-.05f, -.03f, tone(IM_COL32(37, 244, 238, 255)));
                note(.05f, .03f, tone(IM_COL32(254, 44, 85, 255)));
            }
            note(0, 0, done ? tone(white) : grey);
        } else {  // appareil photo : carré arrondi, objectif et point
            ImU32 back = done ? tone(IM_COL32(214, 36, 120, 255)) : grey, line = done ? tone(white) : hole;
            dl->AddRectFilled(at(.04f, .04f), at(.96f, .96f), back, h * .26f);
            dl->AddRect(at(.18f, .18f), at(.82f, .82f), line, h * .17f, 0, h * .08f);
            dl->AddCircle(at(.5f, .5f), h * .17f, line, 0, h * .08f);
            dl->AddCircleFilled(at(.68f, .32f), h * .045f, line);
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(o, o + ImVec2(h, h)))
            ImGui::SetTooltip("%s", (clip ? trf("%s : clip créé", NETS[i].name) : fixed ? trf("%s : vidéo fixe, pas de clip", NETS[i].name) : trf("%s : pas encore de clip", NETS[i].name)).c_str());
    }
    {  // 1.0.78 : Short (9:16) : rectangle vertical rouge et triangle blanc, fichier videos\Shorts\<titre>_Short.mp4
        bool   done = cached_exists(videos / L"Shorts" / (base + L"_Short.mp4"));
        ImVec2 o    = p0 + ImVec2(step * 3, 0);
        auto   at   = [&](float x, float y) { return o + ImVec2(x * h, y * h); };
        dl->AddRectFilled(at(.22f, 0), at(.78f, 1), done ? IM_COL32(255, 0, 51, 255) : grey, h * .16f);
        dl->AddTriangleFilled(at(.40f, .34f), at(.40f, .66f), at(.66f, .50f), done ? white : hole);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(o, o + ImVec2(h, h)))
            ImGui::SetTooltip("%s", done ? tr("Short : créé") : tr("Short : pas encore créé"));
    }
    ImGui::Dummy(ImVec2(clip_logos_width(s), h));
}

// Une ligne par morceau terminé ; « ▾ » déplie la carte complète.
// Badge « (disque) Album · n° piste » d'un morceau d'album (1.0.37) : disque dessiné (pas de police d'emoji),
// survol = groupe et album, clic = la bibliothèque n'affiche plus que cet album (champ de recherche).
void App::album_badge(const Song & s) {
    if (s.album.empty()) return;
    ImVec4      c    = album_color(album_key(s.artist, s.album));
    std::string text = "     " + (s.track.empty() ? std::string(tr("Album")) : trf("Album · n° %s", s.track.c_str()));
    ImVec2      p    = ImGui::GetCursorScreenPos();
    float       r    = ImGui::GetTextLineHeight() * 0.34f;
    pill(text.c_str(), c);
    ImVec2       mid(p.x + 7 * g_dpi + r, p.y + 1 * g_dpi + ImGui::GetTextLineHeight() * 0.5f);
    ImDrawList * dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(mid, r, ImGui::GetColorU32(c));
    dl->AddCircleFilled(mid, r * 0.32f, ImGui::GetColorU32(P.card));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        std::string who = s.artist + (!s.artist.empty() ? "  —  " : "") + s.album;
        ImGui::SetTooltip("%s\n%s", who.c_str(), tr("Cliquer pour n'afficher que cet album."));
    }
    if (ImGui::IsItemClicked()) filter_text = s.album;
}

// Liseré de la couleur de l'album sur le bord DROIT de la carte en cours (1.0.38, demande de Herve : plus visible,
// et distinct des couleurs d'état déjà présentes à gauche).
void App::album_stripe(const Song & s) {
    if (s.album.empty()) return;
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp + ImVec2(ws.x - 6 * g_dpi, 0), wp + ws, ImGui::GetColorU32(album_color(album_key(s.artist, s.album))),
                                              ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersRight);
}

void App::draw_song_compact(const Song & s) {
    ImGui::PushID(s.id.c_str());
    ImVec4 color = status_color(s);
    bool fresh = is_new(s);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, fresh ? tint(P.card, color, 0.13f) : P.card);
    ImGui::PushStyleColor(ImGuiCol_Border, fresh ? alpha(color, 0.65f) : P.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 6 * g_dpi));
    ImGui::BeginChild("compact", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    album_stripe(s);  // 1.0.37 ; à droite depuis la 1.0.38
    bool playing = player.id() == s.id && player.playing();
    float thumb_sz = 22 * g_dpi, arrow_w = 30 * g_dpi, gap = 6 * g_dpi;
    std::string summary = song_summary(s);
    float logos_w = clip_logos_width(s);
    float right_w = lyrics_button_width(s) + gap + ImGui::CalcTextSize(summary.c_str()).x + gap + marks_width(settings.mark_style, thumb_sz) + (logos_w > 0 ? logos_w + 10 * g_dpi : 0);
    if (accent_button(playing ? "⏸" : "▶", ImVec2(32 * g_dpi, 0), &P.green)) toggle_play(s);
    ImGui::SameLine(0, 10 * g_dpi);
    ImGui::AlignTextToFramePadding();
    float new_w   = fresh ? pill_width(tr("À écouter")) + 8 * g_dpi : 0;
    float title_w = ImGui::GetContentRegionAvail().x - right_w - 10 * g_dpi - new_w;
    if (fresh) {
        pill(tr("À écouter"), P.pink);
        ImGui::SameLine(0, 8 * g_dpi);
    }
    ImGui::PushFont(g_bold);
    std::string title = ellipsize(s.title, std::max(60.f * g_dpi, title_w));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    {   // un clic sur le titre déplie la carte
        ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + std::max(60.f * g_dpi, title_w), ImGui::GetItemRectMax().y);
        if (title_zone(a, b, s.title + "\n" + s.style + tr("\n\nCliquer pour déplier"))) expanded_cards.insert(s.id);
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - right_w);
    if (logos_w > 0) {
        float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + (ImGui::GetFrameHeight() - 17 * g_dpi) / 2);
        draw_clip_logos(s);
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::SetCursorPosY(y);
        ImGui::AlignTextToFramePadding();
    }
    lyrics_button(s, true);  // 1.0.95 ; 1.1.010 : « Instrumental » pour un instrumental
    ImGui::SameLine(0, gap);
    ImGui::TextColored(P.muted, "%s", summary.c_str());
    ImGui::SameLine(0, gap);
    apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(4 * g_dpi, ws.y), ImGui::GetColorU32(color),
                                              ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

// Clé de la pochette d'un morceau : celle de son album (partagée par ses morceaux), sinon propre au morceau.
std::string App::cover_key(const std::string & id, const std::string & artist, const std::string & album) {
    return album.empty() ? "titre:" + id : album_key(artist, album);
}

// Pochette carrée (image rognée au centre) ou, à défaut, un aplat dérivé du titre avec son initiale.
void App::draw_cover_art(const std::string & key, const std::string & title, float side) {
    ImVec2       p0 = ImGui::GetCursorScreenPos();
    ImDrawList * dl = ImGui::GetWindowDrawList();
    ImTextureID  tex = 0;
    ImVec2       tsize;
    std::string  cover_path = cover_for(key);
    // Sans pochette carrée, on se rabat sur l'image YouTube ou TikTok de l'album (rognée au centre)
    if (cover_path.empty()) cover_path = cover_for(slot_youtube(key));
    if (cover_path.empty()) cover_path = cover_for(slot_tiktok(key));
    float        r = ImGui::GetStyle().ChildRounding * 0.6f;
    if (!cover_path.empty() && thumb_texture(fs::path(widen(cover_path)), tex, tsize, 256)) {
        float k = std::max(side / tsize.x, side / tsize.y);  // remplit le carré, rogné au centre
        float u = (1 - side / (tsize.x * k)) / 2, v = (1 - side / (tsize.y * k)) / 2;
        dl->AddImageRounded(tex, p0, p0 + ImVec2(side, side), ImVec2(u, v), ImVec2(1 - u, 1 - v), IM_COL32_WHITE, r);
    } else {
        unsigned h = 2166136261u;
        for (char c : title) h = (h ^ (unsigned char) c) * 16777619u;
        const ImVec4 * palette[] = { &P.violet, &P.pink, &P.teal, &P.orange, &P.blue, &P.green, &P.amber };
        ImVec4         base      = *palette[h % 7];
        dl->AddRectFilled(p0, p0 + ImVec2(side, side), ImGui::GetColorU32(alpha(base, 0.35f)), r);
        dl->AddRect(p0, p0 + ImVec2(side, side), ImGui::GetColorU32(alpha(base, 0.8f)), r);
        std::string t = clean_title(title), initial = "♪";
        for (char c : t) {
            if (isalnum((unsigned char) c)) {
                initial = std::string(1, (char) toupper((unsigned char) c));
                break;
            }
        }
        ImFont * font = side > 60 * g_dpi ? g_header : g_bold;
        ImGui::PushFont(font);
        ImVec2 ts = ImGui::CalcTextSize(initial.c_str());
        dl->AddText(p0 + ImVec2((side - ts.x) / 2, (side - ts.y) / 2), ImGui::GetColorU32(base), initial.c_str());
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(side, side));
}

// Une ligne par morceau terminé, avec une petite pochette ; « ▾ » déplie la carte complète avec tous ses détails.
void App::draw_song_cover_compact(const Song & s) {
    ImGui::PushID(s.id.c_str());
    ImVec4 color = status_color(s);
    bool   fresh = is_new(s);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, fresh ? tint(P.card, color, 0.13f) : P.card);
    ImGui::PushStyleColor(ImGuiCol_Border, fresh ? alpha(color, 0.65f) : P.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpi, 7 * g_dpi));
    ImGui::BeginChild("covercompact", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    album_stripe(s);  // 1.0.37
    float side = 46 * g_dpi, gap = 8 * g_dpi, thumb_sz = 22 * g_dpi, play_w = 34 * g_dpi;
    float row_y = ImGui::GetCursorPosY();
    draw_cover_art(cover_key(s.id, s.artist, s.album), s.title, side);
    ImGui::SameLine(0, 10 * g_dpi);
    std::string summary = song_summary(s);
    float       logos_w = clip_logos_width(s);
    float       right_w = lyrics_button_width(s) + gap + play_w + gap + ImGui::CalcTextSize(summary.c_str()).x + gap + marks_width(settings.mark_style, thumb_sz) + (logos_w > 0 ? logos_w + gap : 0);
    float       title_w = ImGui::GetContentRegionAvail().x - right_w - 14 * g_dpi;
    ImGui::BeginGroup();
    ImGui::PushFont(g_bold);
    float pill_w = fresh ? pill_width(tr("À écouter")) + 8 * g_dpi : 0;
    if (fresh) {
        pill(tr("À écouter"), P.pink);
        ImGui::SameLine(0, 8 * g_dpi);
    }
    std::string title = ellipsize(s.title, std::max(60.f * g_dpi, title_w - pill_w));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    {   // un clic sur le titre déplie la carte
        ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + std::max(60.f * g_dpi, title_w - pill_w), ImGui::GetItemRectMax().y);
        if (title_zone(a, b, s.title + "\n" + s.style + tr("\n\nCliquer pour déplier"))) expanded_cards.insert(s.id);
    }
    std::string who = s.artist + (!s.artist.empty() && !s.album.empty() ? "  —  " : "") + s.album;
    if (!who.empty()) ImGui::TextColored(P.blue, "%s", ellipsize(who, std::max(60.f * g_dpi, title_w)).c_str());
    ImGui::EndGroup();
    // commandes alignées au centre de la pochette
    float center_y = row_y + (side - ImGui::GetFrameHeight()) / 2;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - right_w);
    if (logos_w > 0) {
        ImGui::SetCursorPosY(center_y + (ImGui::GetFrameHeight() - 17 * g_dpi) / 2);
        draw_clip_logos(s);
        ImGui::SameLine(0, gap);
    }
    ImGui::SetCursorPosY(center_y);
    lyrics_button(s, true);  // 1.0.95 ; 1.1.010 : « Instrumental » pour un instrumental
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(center_y);
    bool playing = player.id() == s.id && player.playing();
    if (accent_button(playing ? "⏸" : "▶", ImVec2(play_w, 0), &P.green)) toggle_play(s);
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(center_y);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", summary.c_str());
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(center_y + (ImGui::GetFrameHeight() - thumb_sz) / 2);
    apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(4 * g_dpi, ws.y), ImGui::GetColorU32(color), ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

// Carte avec pochette à gauche ; les actions sont regroupées dans le menu « Actions ».
void App::draw_song_cover(const Song & s, bool details, bool collapsible) {
    ImGui::PushID(s.id.c_str());
    ImVec4 color = status_color(s);
    bool fresh = is_new(s);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, fresh ? tint(P.card, color, 0.13f) : P.card);
    ImGui::PushStyleColor(ImGuiCol_Border, fresh ? alpha(color, 0.65f) : P.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 12 * g_dpi));
    ImGui::BeginChild("cover", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    album_stripe(s);  // 1.0.37
    float  side = 84 * g_dpi;
    ImDrawList * dl = ImGui::GetWindowDrawList();
    draw_cover_art(cover_key(s.id, s.artist, s.album), s.title, side);
    ImGui::SameLine(0, 14 * g_dpi);
    ImGui::BeginGroup();
    // titre, statut et pouces
    float        line_x = ImGui::GetCursorPosX(), avail = ImGui::GetContentRegionAvail().x;
    const char * status = status_label(s);
    float        status_w = pill_width(status), thumb_sz = 24 * g_dpi, thumb_w = marks_width(settings.mark_style, thumb_sz);
    float        title_y  = ImGui::GetCursorPosY();
    float        logos_w  = clip_logos_width(s);
    float        new_w    = (fresh ? pill_width(tr("À écouter")) + 6 * g_dpi : 0) + (logos_w > 0 ? logos_w + 8 * g_dpi : 0);  // 1.0.62 : place des logos
    ImGui::PushFont(g_bold);
    ImGui::PushTextWrapPos(line_x + avail - status_w - thumb_w - new_w - 20 * g_dpi);
    ImGui::TextWrapped("%s", s.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (collapsible) {  // un clic sur le titre replie la carte
        ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + (avail - status_w - thumb_w - new_w - 20 * g_dpi), ImGui::GetItemRectMax().y);
        if (title_zone(a, b, tr("Cliquer pour replier"))) expanded_cards.erase(s.id);
    }
    float after_y = ImGui::GetCursorPosY();
    if (fresh) {
        ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - new_w - 8 * g_dpi, title_y));
        pill(tr("À écouter"), P.pink);
    }
    ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - logos_w - 16 * g_dpi, title_y));
    draw_clip_logos(s);
    ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - 8 * g_dpi, title_y));
    pill(status, color);
    ImGui::SetCursorPos(ImVec2(line_x + avail - thumb_w, title_y - 3 * g_dpi));
    apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
    ImGui::SetCursorPos(ImVec2(line_x, after_y));
    if (!s.instrumental && !s.lyrics_lang.empty()) {  // langue des paroles (déclarée ou détectée)
        int li = language_index_for_code(s.lyrics_lang);
        if (li > 0) {
            pill(trf("Paroles : %s", tr(LANGUAGES[li].label)).c_str(), P.blue);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", s.lyrics_lang_declared ? tr("Langue des paroles : déclarée par la balise de langue du style.") : tr("Langue des paroles : détectée d'après le texte."));
            }
        }
    }
    if (!s.album.empty()) {  // 1.0.37 : badge de l'album (clic = filtre)
        album_badge(s);
        ImGui::SameLine(0, 8 * g_dpi);
    }
    if (!s.artist.empty() || !s.album.empty()) {
        std::string who = s.artist + (!s.artist.empty() && !s.album.empty() ? "  —  " : "") + s.album;
        ImGui::TextColored(P.blue, "%s", who.c_str());
    }
    ImGui::TextColored(P.muted, "%s", song_summary(s, false).c_str());
    if (ImGui::IsItemHovered()) {
        if (s.negative_style.empty()) ImGui::SetTooltip("%s", s.style.c_str());
        else ImGui::SetTooltip("%s\n%s", s.style.c_str(), trf("À éviter : %s (force %.1f)", s.negative_style.c_str(), s.negative_scale).c_str());
    }
    ImGui::Dummy(ImVec2(0, 3 * g_dpi));
    draw_player(s);
    ImGui::EndGroup();
    // menu d'actions, sous la pochette et le texte
    if (soft_button(tr("Actions ▾"), P.accent)) ImGui::OpenPopup("##actions");
    ImGui::SameLine();
    lyrics_button(s);  // 1.0.95
    ImGui::SameLine();
    ImGui::TextColored(P.muted, tr("export, vidéo, re-rendu, recomposer, prolonger…"));
    draw_song_menu(s);
    if (details) {
        ImGui::Dummy(ImVec2(0, 3 * g_dpi));
        muted_wrapped(s.style);
        draw_song_stats(s, color);
    }
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddRectFilled(wp, wp + ImVec2(5 * g_dpi, ws.y), ImGui::GetColorU32(color), ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

bool App::passes_filters(const Song & s) const {
    if (s.active()) return true;  // la file reste toujours visible
    if (!filter_text.empty()) {
        std::string hay = lower(s.title + " " + s.style + " " + s.parent_title + " " + s.artist + " " + s.album + " " + s.lyrics);  // 1.0.96 : paroles aussi
        if (hay.find(lower(filter_text)) == std::string::npos) return false;
    }
    switch (filter_type) {
        case 1: if (s.instrumental) return false; break;
        case 2: if (!s.instrumental) return false; break;
        case 3: if (s.kind != "cover") return false; break;
        case 4: if (s.kind != "rerender" && s.kind != "extend") return false; break;
        case 5: if (s.status == "done") return false; break;
        case 6: if (!s.validated) return false; break;
        case 7: if (s.status != "done" || s.validated) return false; break;
        case 8: if (!s.flagged) return false; break;
        case 9: if (!is_new(s)) return false; break;
    }
    double              d        = s.audio_seconds > 0 ? s.audio_seconds : s.duration;
    static const double bounds[][2] = { { 0, 1e9 }, { 0, 60 }, { 60, 120 }, { 120, 180 }, { 180, 240 }, { 240, 1e9 } };
    if (d < bounds[filter_duration][0] || d >= bounds[filter_duration][1]) return false;
    if (!filter_style.empty()) {
        bool found = false;
        for (auto & t : split_tags(s.style)) found |= lower(t) == filter_style;
        if (!found) return false;
    }
    return true;
}

  // disposition A : bloc de la file déplié alors qu'elle est vide
void App::draw_queue_bar(const std::vector<Song> & songs) {
    int running = 0, waiting = 0, held = 0;
    for (const Song & s : songs) {
        running += s.status == "running";
        waiting += s.waiting();
        held += s.status == "queued" && s.held;
    }
    // Disposition A (1.1.002) : file vide = une seule ligne ; « Options ▾ » déplie le bloc complet (fin de file, contrôle).
    const bool empty = running + waiting + held == 0;
    if (settings.layout >= 1 && empty && !queue_open) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "● %s", tr("File de génération vide"));
        if (studio->gate_closed()) {
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(P.orange, tr("● jeu en cours : file suspendue"));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Options ▾"))) queue_open = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ce qu'il faut faire quand la file est terminée, contrôle de la tonalité et du tempo."));
        return;
    }
    ImVec4 color = running ? P.accent : waiting ? P.blue : held ? P.amber : P.muted;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.panel, color, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.35f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpi, 8 * g_dpi));
    ImGui::BeginChild("queue", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(g_bold);
    ImGui::TextColored(color, tr("File de génération"));
    ImGui::PopFont();
    auto count = [&](int n, const char * text, const ImVec4 & c) {
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::TextColored(n ? c : P.muted, "● %d %s", n, text);
    };
    count(running, tr("en cours"), P.accent);
    count(waiting, tr("en attente"), P.blue);
    count(held, tr("en pause"), P.amber);
    {   // 1.1.015 : fin estimée de la file (morceau en cours + morceaux en attente, hors pause)
        double left = 0;
        const double now = now_seconds();
        for (const Song & s : songs) {
            if (s.status == "running") left += std::max(0.0, s.eta - (now - s.eta_at));
            else if (s.waiting()) left += studio->eta_for(s);
        }
        if (left > 60 && running + waiting > 0) {
            time_t end = time(nullptr) + (time_t) left;
            tm     lt{};
            localtime_s(&lt, &end);
            char clock[16];
            snprintf(clock, sizeof(clock), "%02d:%02d", lt.tm_hour, lt.tm_min);
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(P.muted, "%s", trf("Fin estimée : %s (vers %s)", fmt_time(left).c_str(), clock).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Estimation d'après la durée des morceaux déjà générés sur ce PC ; elle s'affine au fil des générations."));
        }
    }
    if (studio->gate_closed()) {
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::TextColored(P.orange, tr("● jeu en cours : file suspendue"));
    }
    const char * a = tr("▶ Tout lancer");
    const char * b = tr("⏸ Tout en pause");
    float w = ImGui::CalcTextSize(a).x + ImGui::CalcTextSize(b).x + ImGui::GetStyle().FramePadding.x * 4 +
              ImGui::GetStyle().ItemSpacing.x;
    {   // à droite de la ligne ; sous les compteurs quand la place manque (langues à textes longs)
        float end_x   = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        float want_x  = ImGui::GetContentRegionMax().x - w;
        if (want_x >= end_x + 10 * g_dpi) ImGui::SameLine(want_x);
    }
    ImGui::BeginDisabled(held == 0);
    if (soft_button(a, P.green, false)) studio->resume_all();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tr("Relance tous les morceaux en pause."));
    ImGui::SameLine();
    ImGui::BeginDisabled(running + waiting == 0);
    if (soft_button(b, P.amber, false)) studio->hold_all();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(tr("Met toute la file en pause et libère la carte graphique.\nLa génération en cours reprendra depuis le début."));
    }
    {  // 1.0.97 : session de nuit
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Quand la file est terminée :"));
        ImGui::SameLine();
        const char * acts[] = { tr("ne rien faire"), tr("mettre le PC en veille"), tr("éteindre le PC") };
        int cur = std::clamp(atoi(settings.night_action.c_str()), 0, 2);
        ImGui::SetNextItemWidth(200 * g_dpi);
        if (ImGui::Combo("##night", &cur, acts, 3)) { settings.night_action = std::to_string(cur); save_settings(settings, settings_path); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Un rapport « rapport_nuit.txt » est toujours écrit ; la veille ou l'extinction laisse 60 secondes pour annuler."));
        bool ka = settings.key_check_auto == "1";  // 1.0.98
        if (ImGui::Checkbox(tr("Contrôler la tonalité et le tempo des morceaux à la fin"), &ka)) { settings.key_check_auto = ka ? "1" : "0"; save_settings(settings, settings_path); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("SheetSage2 réécoute chaque morceau de la session (≈ 1 min pour 3 min) : les morceaux qui dérivent de la partition ou du BPM demandé reçoivent un pouce rouge."));
        if (settings.layout >= 1 && empty) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Replier ▴"))) queue_open = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void App::draw_filters(const std::vector<Song> & songs) {
    input_line("##search", tr("Rechercher un titre, un groupe, un album, un style ou des paroles…"), filter_text);
    float               w       = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    static const char * types[] = { N_("Tous les types"), N_("Chansons"), N_("Instrumentaux"), N_("Reprises"), N_("Re-rendus et suites"),
                                    N_("Non terminés"), N_("Validés"), N_("Non validés"), N_("À problème"), N_("Nouveaux (à écouter)") };
    ImGui::SetNextItemWidth(w);
    ImGui::Combo("##ftype", &filter_type, tr_list(types, (int) IM_ARRAYSIZE(types)).data(), IM_ARRAYSIZE(types));
    static const char * durations[] = { N_("Toutes durées"), N_("Moins d'1 min"), N_("1 à 2 min"), N_("2 à 3 min"), N_("3 à 4 min"), N_("4 min et plus") };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w);
    ImGui::Combo("##fduration", &filter_duration, tr_list(durations, (int) IM_ARRAYSIZE(durations)).data(), IM_ARRAYSIZE(durations));
    std::map<std::string, int> counts;
    for (const Song & s : songs) {
        for (auto & t : split_tags(s.style)) counts[lower(t)]++;
    }
    std::vector<std::pair<std::string, int>> tags(counts.begin(), counts.end());
    std::sort(tags.begin(), tags.end(), [](auto & a, auto & b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w);
    std::string preview = filter_style.empty() ? tr("Tous les styles") : filter_style;
    if (ImGui::BeginCombo("##fstyle", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(tr("Tous les styles"), filter_style.empty())) filter_style.clear();
        for (auto & [tag, n] : tags) {
            std::string item = tag + "  (" + std::to_string(n) + ")";
            if (ImGui::Selectable(item.c_str(), filter_style == tag)) filter_style = tag;
        }
        ImGui::EndCombo();
    }
}

void App::draw_library(const std::vector<Song> & songs) {
    std::vector<const Song *> shown;
    for (const Song & s : songs) {
        if (passes_filters(s)) shown.push_back(&s);
    }
    // En-tête : titre, compteur, volume et dossier
    ImGui::PushFont(g_header);
    ImGui::TextUnformatted(tr("Bibliothèque"));
    ImGui::PopFont();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("%zu sur %zu morceaux"), shown.size(), songs.size());
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const float count_x = ImGui::GetCursorPosX();
    char        count[64];
    if (shown.size() == songs.size()) snprintf(count, sizeof(count), songs.size() > 1 ? tr("%zu morceaux") : tr("%zu morceau"), songs.size());
    else snprintf(count, sizeof(count), tr("%zu sur %zu morceaux"), shown.size(), songs.size());
    float folder_w = ImGui::CalcTextSize(tr("Ouvrir le dossier")).x + ImGui::GetStyle().FramePadding.x * 2;
    float albums_w = ImGui::CalcTextSize(tr("Albums…")).x + ImGui::GetStyle().FramePadding.x * 2;
    float vol_w    = 130 * g_dpi;
    float label_w  = ImGui::CalcTextSize(tr("Volume")).x;
    // 1.0.46 : « Lire les nouveaux » = lecture continue des morceaux terminés pas encore écoutés (ordre de la liste)
    int unheard = 0;
    for (const Song * s : shown) unheard += s->status == "done" && !s->heard;
    std::string new_label = new_mode ? std::string(tr("Arrêter les nouveaux")) : trf("Lire les nouveaux (%d)", unheard);
    float       new_w     = ImGui::CalcTextSize(new_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 +
                      (new_mode ? ImGui::CalcTextSize(tr("Suivant")).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x : 0);
    // 1.0.78 : le compteur n'est écrit que s'il tient avant les boutons (sinon il chevauchait « Lire les nouveaux ») ; il reste
    // affiché au survol du titre « Bibliothèque »
    const float buttons_x = ImGui::GetContentRegionMax().x - new_w - folder_w - albums_w - vol_w - label_w - ImGui::GetStyle().ItemSpacing.x * 5;
    if (count_x + ImGui::CalcTextSize(count).x + ImGui::GetStyle().ItemSpacing.x <= buttons_x) {
        ImGui::TextColored(P.muted, "%s", count);
        ImGui::SameLine(buttons_x);
    } else {
        ImGui::SetCursorPosX(std::max(count_x, buttons_x));
    }
    ImGui::BeginDisabled(!new_mode && unheard == 0);
    if (soft_button(new_label.c_str(), new_mode ? P.danger : P.green, false)) {
        if (new_mode) {
            new_mode = false;
            player.close();
        } else {
            start_new_playback(shown);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr("Écoute à la suite tous les morceaux terminés que vous n'avez pas encore écoutés, dans l'ordre de la liste."));
    if (new_mode) {
        ImGui::SameLine();
        if (soft_button(tr("Suivant"), P.green, false)) next_new_song();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, tr("Volume"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(vol_w);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, P.green);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, P.green);
    if (ImGui::SliderInt("##volume", &settings.volume, 0, 100, "%d %%")) player.set_volume(settings.volume);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
    ImGui::SameLine();
    if (ImGui::Button(tr("Albums…"))) open_albums = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Ouvrir le dossier"))) {
        ShellExecuteW(nullptr, L"open", studio->paths().library.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    sort_shown(shown);
    play_queue.clear();
    for (const Song * s : shown) {
        if (s->status == "done") play_queue.push_back(s->id);
    }
    draw_queue_bar(songs);
    draw_jobs();
    draw_filters(songs);
    draw_sort_bar();
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    if (songs.empty()) {
        ImGui::Dummy(ImVec2(0, 40 * g_dpi));
        muted_wrapped(tr("Aucun morceau pour l'instant. Remplissez le formulaire pour lancer le premier."));
    } else if (shown.empty()) {
        ImGui::Dummy(ImVec2(0, 20 * g_dpi));
        muted_wrapped(tr("Aucun morceau ne correspond aux filtres."));
    }
    // Position dans la file : ordre de passage (le plus ancien d'abord).
    std::vector<const Song *> waiting;
    for (const Song & s : songs) {
        if (s.waiting()) waiting.push_back(&s);
    }
    std::sort(waiting.begin(), waiting.end(), [](const Song * a, const Song * b) { return a->created < b->created; });
    // Seule la liste défile : en-tête, file et filtres restent visibles.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("songs", ImVec2(0, 0), ImGuiChildFlags_None);
    // 1.0.39 (demande de Herve) : cartes de la bibliothèque à angles droits (fond, bordure, liserés, pochette) ;
    // le reste de l'interface garde ses arrondis.
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    for (const Song * s : shown) {
        int pos = 0;
        for (size_t i = 0; i < waiting.size(); i++) {
            if (waiting[i] == s) pos = (int) i + 1;
        }
        bool finished = s->status == "done";
        if (finished && settings.card_mode == 1 && !expanded_cards.count(s->id)) draw_song_compact(*s);
        else if (finished && settings.card_mode == 2) draw_song_cover(*s);
        else if (finished && settings.card_mode == 3) {
            if (expanded_cards.count(s->id)) draw_song_cover(*s, true, true);
            else draw_song_cover_compact(*s);
        }
        else draw_song(*s, pos, finished && settings.card_mode == 1);
        ImGui::Dummy(ImVec2(0, 1 * g_dpi));
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
