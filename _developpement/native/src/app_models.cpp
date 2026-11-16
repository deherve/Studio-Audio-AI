// Modèles, LoRA, structures et packs, analyse audio, langue chantée, recommandations, sauvegarde (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

void App::apply_language() {
    std::string code = pending_language;
    pending_language.clear();
    if (!i18n::set_language(code)) return;
    settings.ui_language = code;
    save_settings(settings, settings_path);
    build_fonts(true);
}

  // nom du fichier → fiche
App::CustomLora App::read_custom_lora(const fs::path & dir, const std::string & file, unsigned long long size) {
    CustomLora c;
    c.size = size;
    // En-tête safetensors : 8 octets (taille, petit-boutiste) puis un objet JSON ; on n'en garde que « __metadata__ ».
    if (std::FILE * f = _wfopen((dir / widen(file)).c_str(), L"rb")) {
        unsigned char n8[8] = {};
        if (fread(n8, 1, 8, f) == 8) {
            unsigned long long n = 0;
            for (int i = 7; i >= 0; i--) n = (n << 8) | n8[i];
            if (n > 0 && n < 16ull * 1024 * 1024) {
                std::string head((size_t) n, '\0');
                if (fread(head.data(), 1, head.size(), f) == head.size()) {
                    if (yyjson_doc * doc = yyjson_read(head.data(), head.size(), 0)) {
                        yyjson_val * m = yyjson_obj_get(yyjson_doc_get_root(doc), "__metadata__");
                        c.trigger           = json_str(m, "trigger_word");
                        std::string rank = json_str(m, "source_rank"), steps = json_str(m, "source_steps"),
                                    clip = json_str(m, "source_clip_seconds"), base = json_str(m, "base_model");
                        c.forge = !steps.empty() || !rank.empty();
                        std::vector<std::string> parts;
                        if (!rank.empty()) parts.push_back(trf("rang %s", rank.c_str()));
                        if (!steps.empty()) parts.push_back(trf("%s pas", steps.c_str()));
                        if (!clip.empty()) parts.push_back(trf("extraits de %s s", clip.c_str()));
                        if (!base.empty()) parts.push_back(base);
                        for (size_t i = 0; i < parts.size(); i++) c.details += (i ? " · " : "") + parts[i];
                        yyjson_doc_free(doc);
                    }
                }
            }
        }
        fclose(f);
    }
    // Fichier .json du même nom : il complète ou remplace ce que dit l'en-tête.
    std::string stem = file;
    if (stem.size() > 12 && stem.compare(stem.size() - 12, 12, ".safetensors") == 0) stem.resize(stem.size() - 12);
    std::string text = read_text(dir / widen(stem + ".json"));
    c.json_time      = json_stamp(dir, file);
    if (yyjson_doc * doc = yyjson_read(text.data(), text.size(), 0)) {
        yyjson_val * r = yyjson_doc_get_root(doc);
        auto take = [&](const char * key, std::string & dst) {
            std::string v = json_str(r, key);
            if (!v.empty()) dst = v;
        };
        take("name", c.name);
        take("description", c.description);
        take("author", c.author);
        take("kind", c.kind);
        // Mot déclencheur : celui que la LoRA a appris (« trigger_word » de l'en-tête) l'emporte (1.0.25).
        // Changer le mot dans la fiche sans réentraîner ne change pas ce que le modèle a appris.
        std::string learned = c.trigger;
        take("trigger", c.trigger);
        if (!learned.empty() && c.trigger != learned) {
            c.sheet_trigger = c.trigger;
            c.trigger       = learned;
        }
        take("date", c.date);
        yyjson_val * tags = yyjson_obj_get(r, "tags");
        if (yyjson_is_str(tags)) c.tags = yyjson_get_str(tags);
        size_t       i, n;
        yyjson_val * t;
        yyjson_arr_foreach(tags, i, n, t) if (yyjson_is_str(t)) c.tags += (c.tags.empty() ? "" : ", ") + std::string(yyjson_get_str(t));
        yyjson_val * v = yyjson_obj_get(r, "songs");
        if (yyjson_is_num(v)) c.songs = (int) yyjson_get_num(v);
        v = yyjson_obj_get(r, "clips");
        if (yyjson_is_num(v)) c.clips = (int) yyjson_get_num(v);
        yyjson_doc_free(doc);
    }
    return c;
}

// Date de modification de la fiche « <nom>.json » d'une LoRA (0 si elle n'existe pas).
long long App::json_stamp(const fs::path & dir, const std::string & file) {
    std::string stem = file;
    if (stem.size() > 12 && stem.compare(stem.size() - 12, 12, ".safetensors") == 0) stem.resize(stem.size() - 12);
    std::error_code ec;
    auto            t = fs::last_write_time(dir / widen(stem + ".json"), ec);
    return ec ? 0 : (long long) t.time_since_epoch().count();
}

// Fiche d'une LoRA personnelle (nullptr pour une LoRA du catalogue ou un fichier inconnu).
const App::CustomLora * App::custom_lora(const std::string & file) const {
    auto it = custom_loras.find(file);
    return it == custom_loras.end() ? nullptr : &it->second;
}

// Nom affiché : celui de la fiche d'une LoRA personnelle s'il existe, sinon le nom habituel.
std::string App::custom_label(const std::string & file) const {
    const CustomLora * c = custom_lora(file);
    return c && !c->name.empty() ? c->name : lora_label(file);
}

// Type d'une LoRA personnelle (colonne « Type », pastille du formulaire).
const char * App::custom_kind(const CustomLora * c) {
    std::string k = c ? c->kind : std::string();
    return k == "style" ? tr("Style") : k == "artist" ? tr("Artiste") : k == "slider" ? tr("Curseur") : k == "instrument" ? tr("Instrum.") : tr("Perso");
}

// Description affichée d'une LoRA personnelle.
std::string App::custom_description(const CustomLora & c) {
    if (!c.description.empty()) return c.description;
    return c.forge ? tr("LoRA créée par la Forge.") : tr("LoRA personnelle déposée dans modeles\\lora.");
}

// Mot déclencheur d'une LoRA (catalogue ou fiche personnelle) ; vide s'il n'y en a pas.
std::string App::lora_trigger(const std::string & file) const {
    LoraRef r = lora_ref(file);
    if (r.entry) return r.entry->trigger;
    const CustomLora * c = custom_lora(file);
    return c ? c->trigger : std::string();
}

void App::scan_loras(bool force) {
    double now = ImGui::GetTime();
    if (!force && now - lora_scan_at < 2.0) return;
    lora_scan_at = now;
    lora_files.clear();
    std::error_code ec;
    for (const auto & e : fs::directory_iterator(studio->paths().adapters, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".safetensors") {
            lora_files[narrow(e.path().filename().wstring())] = (unsigned long long) e.file_size(ec);
        }
    }
    // Fiches des LoRA personnelles : lues une fois, relues si le fichier change de taille, si sa fiche .json
    // change (la Forge peut modifier le nom ou le mot déclencheur, 1.0.25) ou si on force.
    const fs::path & dir = studio->paths().adapters;
    for (auto it = custom_loras.begin(); it != custom_loras.end();) it = lora_files.count(it->first) ? std::next(it) : custom_loras.erase(it);
    for (const auto & [file, size] : lora_files) {
        if (lora_ref(file).entry) continue;
        auto it = custom_loras.find(file);
        if (!force && it != custom_loras.end() && it->second.size == size && it->second.json_time == json_stamp(dir, file)) continue;
        std::string old_word = it != custom_loras.end() ? it->second.trigger : std::string();
        custom_loras[file]   = read_custom_lora(dir, file, size);
        // Mot déclencheur changé alors que la LoRA est cochée : l'ancien mot quitte le style, le nouveau y entre.
        const LoraUse * u = nullptr;
        for (const LoraUse & o : form.loras) if (o.file == file) u = &o;
        if (u && u->enabled && old_word != custom_loras[file].trigger) {
            style_trigger(old_word, false);
            style_trigger(custom_loras[file].trigger, true);
        }
    }
    // Premier balayage : les LoRA restées cochées depuis la dernière séance reçoivent leur mot dans le style.
    if (!triggers_synced) {
        triggers_synced = true;
        sync_lora_triggers();
    }
}

App::LoraRef App::lora_ref(const std::string & file) {
    for (const LoraEntry & e : LORA_CATALOG) {
        for (int i = 0; i < e.file_count; i++) {
            if (file == e.files[i].file) return { &e, &e.files[i] };
        }
    }
    return {};
}

std::string App::lora_label(const std::string & file) {
    LoraRef r = lora_ref(file);
    if (!r.entry) {
        std::string s = file;
        for (const char * ext : { ".bf16.safetensors", ".safetensors" }) {
            size_t n = strlen(ext);
            if (s.size() > n && s.compare(s.size() - n, n, ext) == 0) {
                s.resize(s.size() - n);
                break;
            }
        }
        return s;
    }
    std::string n = tr(r.entry->name);
    if (r.file->half[0]) n += std::string(" (") + r.file->half + ")";
    return n;
}

bool App::lora_installed(const LoraEntry & e) const {
    for (int i = 0; i < e.file_count; i++) {
        if (!lora_files.count(e.files[i].file)) return false;
    }
    return true;
}

std::string App::fmt_mb(unsigned long long bytes) {
    char buf[32];
    if (bytes >= 1000ull * 1000 * 1000) snprintf(buf, sizeof(buf), tr("%.2f Go"), bytes / 1e9);
    else snprintf(buf, sizeof(buf), tr("%.0f Mo"), bytes / 1e6);
    for (char & c : buf) if (c == '.') c = ',';
    return buf;
}

// Mot déclencheur d'une LoRA : ajouté au début du style quand elle est cochée, retiré sinon.
void App::style_trigger(const std::string & word, bool present) {
    if (word.empty()) return;
    std::vector<std::string> tags = split_style(form.style);
    auto same = [&](const std::string & t) {
        return t.size() == word.size() && std::equal(t.begin(), t.end(), word.begin(), [](char a, char b) { return tolower((unsigned char) a) == tolower((unsigned char) b); });
    };
    auto it = std::find_if(tags.begin(), tags.end(), same);
    if (present && it == tags.end()) tags.insert(tags.begin(), word);
    else if (!present && it != tags.end()) tags.erase(it);
    else return;
    form.style = join_style(tags);
    form.dirty = true;
}

  // mots des LoRA cochées remis dans le style au premier balayage (1.0.25)

// Met dans le style le mot déclencheur de chaque LoRA cochée et installée (1.0.25) : après un préréglage,
// la reprise d'un morceau, ou juste avant une génération si le mot a été effacé du champ Style.
// Une LoRA sans mot déclencheur n'a rien à ajouter : elle est appliquée à la génération quand même.
void App::sync_lora_triggers() {
    for (const LoraUse & u : form.loras) {
        if (u.enabled && lora_files.count(u.file)) style_trigger(lora_trigger(u.file), true);
    }
}

LoraUse * App::find_lora_use(const std::string & file) {
    for (LoraUse & u : form.loras) if (u.file == file) return &u;
    return nullptr;
}

void App::set_lora(const std::string & file, bool on) {
    LoraRef   r = lora_ref(file);
    LoraUse * u = find_lora_use(file);
    if (!u && on) {
        form.loras.push_back({ file, r.file ? r.file->scale : 1.f, true });
    } else if (u) {
        u->enabled = on;
    }
    std::string word = lora_trigger(file);  // catalogue, ou fiche d'une LoRA personnelle (1.0.24)
    if (!word.empty()) {
        bool any = false;
        for (const LoraUse & o : form.loras) any |= o.enabled && lora_trigger(o.file) == word;
        style_trigger(word, any);
    }
    form.dirty = true;
}

// Durée du fondu « sur la structure » : celle de la dernière section (outro, couplet…), de 5 à 30 s.
double App::structure_fade_seconds(const std::string & lyrics, bool instrumental) {
    std::string tags = structure_tags(lyrics), last = tags.substr(tags.rfind(',') == std::string::npos ? 0 : tags.rfind(',') + 1);
    for (auto & c : last) c = (char) tolower((unsigned char) c);
    double seconds;
    if (instrumental) {
        seconds = last.rfind("intro", 0) == 0 ? 15 : last.rfind("outro", 0) == 0 ? 20 : last.rfind("pre", 0) == 0 ? 15
                : last.rfind("bridge", 0) == 0 ? 25 : 30;
    } else {
        seconds = last.rfind("outro", 0) == 0 || last.rfind("interlude", 0) == 0 ? 15 : 12;
    }
    return std::clamp(seconds, 5.0, 30.0);
}

// Un morceau terminé, neuf et ni validé ni signalé : en attente d'écoute et de vérification.
bool App::is_new(const Song & s) { return s.status == "done" && !s.heard && !s.validated && !s.flagged; }

void        App::mark_heard(const Song & s) {
    if (!s.heard) studio->set_heard(s.id, true);
}

// Applique le résultat de draw_marks : 1 validé, 2 à problème, 3 retire les marques. Avec le commutateur,
// un clic choisit l'état affiché (pas de bascule) ; avec les boutons, un second clic retire la marque.
void App::apply_mark(const Song & s, int code) {
    if (code != 0) mark_heard(s);  // l'avoir évalué vaut vérification
    if (code == 1) studio->set_validated(s.id, settings.mark_style == 2 ? true : !s.validated);
    else if (code == 2) studio->set_flagged(s.id, settings.mark_style == 2 ? true : !s.flagged);
    else if (code == 3) {
        if (s.validated) studio->set_validated(s.id, false);
        if (s.flagged) studio->set_flagged(s.id, false);
    }
}

// Les LoRA cochées et installées d'un nouveau morceau.
std::vector<std::pair<std::string, double>> App::chosen_loras() const {
    std::vector<std::pair<std::string, double>> out;
    for (const LoraUse & u : form.loras) {
        if (u.enabled && lora_files.count(u.file)) out.push_back({ u.file, u.scale });
    }
    return out;
}

// Groupes d'une liste : "" (éléments personnels) d'abord s'il y en a, puis chaque pack dans l'ordre d'installation.
std::vector<std::string> App::pack_groups(bool styles) const {
    std::vector<std::string> g;
    bool                     own = false;
    auto add = [&](const std::string & pack) {
        if (pack.empty()) own = true;
        else if (std::find(g.begin(), g.end(), pack) == g.end()) g.push_back(pack);
    };
    if (styles) for (const Preset & p : settings.presets) add(p.pack);
    else for (const UserStructure & st : settings.structures) add(st.pack);
    if (own) g.insert(g.begin(), std::string());
    return g;
}

int App::find_preset(const std::string & name, const std::string & pack) const {
    for (size_t i = 0; i < settings.presets.size(); i++) if (settings.presets[i].name == name && settings.presets[i].pack == pack) return (int) i;
    return -1;
}

int App::find_structure(const std::string & name, const std::string & pack) const {
    for (size_t i = 0; i < settings.structures.size(); i++) if (settings.structures[i].name == name && settings.structures[i].pack == pack) return (int) i;
    return -1;
}

fs::path App::ask_pack() {
    wchar_t       file[MAX_PATH] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner       = hwnd;
    std::wstring filter = file_filter(wtr("Packs Studio Audio AI"), L"*.yue2pack");
    ofn.lpstrFilter     = filter.c_str();
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = MAX_PATH;
    std::wstring title  = wtr("Importer des styles et structures");
    ofn.lpstrTitle      = title.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
}

// Lit un pack ; les éléments qui existent déjà (même nom, même groupe) seront tranchés un par un dans une fenêtre.
void App::import_pack(const fs::path & path) {
    auto        job = std::make_unique<ImportJob>();
    std::string error;
    if (!read_pack(path, job->pack, error)) {
        notify(trf("Import impossible (%s) : %s", narrow(path.filename().wstring()).c_str(), error.c_str()));
        return;
    }
    // Pack nommé déjà installé : on ne compare pas élément par élément, on demande pour le pack entier.
    bool installed = false;
    for (const PackInfo & info : settings.packs) installed |= !job->pack.info.name.empty() && info.name == job->pack.info.name;
    job->pack_installed = installed;
    for (const Preset & p : job->pack.styles) job->style_choice.push_back(!installed && find_preset(p.name, p.pack) >= 0 ? -1 : 0);
    for (const UserStructure & st : job->pack.structures) job->structure_choice.push_back(!installed && find_structure(st.name, st.pack) >= 0 ? -1 : 0);
    import_job = std::move(job);
    if (!import_job->pack_installed && !import_has_conflict()) finish_import();
}

// Nom libre pour un pack : « Rock 80s », sinon « Rock 80s (2) », « Rock 80s (3) »…
std::string App::free_pack_name(const std::string & name) const {
    auto taken = [&](const std::string & n) {
        for (const PackInfo & info : settings.packs) if (info.name == n) return true;
        return false;
    };
    if (!taken(name)) return name;
    for (int k = 2;; k++) {
        std::string n = name + " (" + std::to_string(k) + ")";
        if (!taken(n)) return n;
    }
}

bool App::import_has_conflict() const {
    if (!import_job) return false;
    for (int c : import_job->style_choice) if (c < 0) return true;
    for (int c : import_job->structure_choice) if (c < 0) return true;
    return false;
}

// Ajoute les éléments selon les choix, mémorise le pack, signale les LoRA manquantes.
void App::finish_import() {
    ImportJob &           j       = *import_job;
    int                   n_style = 0, n_struct = 0;
    std::set<std::string> missing;
    scan_loras(true);
    for (size_t i = 0; i < j.pack.styles.size(); i++) {
        int c = j.style_choice[i];
        if (c == 3) continue;
        Preset np = j.pack.styles[i];
        int    k  = find_preset(np.name, np.pack);
        if (c == 1 && k >= 0) settings.presets[k] = np;
        else {
            if (k >= 0) np.name = free_name(settings.presets, np.name, np.pack);
            settings.presets.push_back(np);
        }
        n_style++;
        for (const LoraUse & u : np.loras) if (!lora_files.count(u.file)) missing.insert(lora_label(u.file));
    }
    for (size_t i = 0; i < j.pack.structures.size(); i++) {
        int c = j.structure_choice[i];
        if (c == 3) continue;
        UserStructure st = j.pack.structures[i];
        int           k  = find_structure(st.name, st.pack);
        if (c == 1 && k >= 0) settings.structures[k] = st;
        else {
            if (k >= 0) st.name = free_name(settings.structures, st.name, st.pack);
            settings.structures.push_back(st);
        }
        n_struct++;
    }
    if (!j.pack.info.name.empty()) {  // pack nommé : ses informations sont gardées (mise à jour si déjà installé)
        bool found = false;
        for (PackInfo & info : settings.packs) {
            if (info.name == j.pack.info.name) {
                info  = j.pack.info;
                found = true;
            }
        }
        if (!found) settings.packs.push_back(j.pack.info);
    }
    save_settings(settings, settings_path);
    std::string what = trf("%d style(s) et %d structure(s) importé(s)", n_style, n_struct);
    notify(j.pack.info.name.empty() ? what + "." : trf("Pack « %s » : %s.", j.pack.info.name.c_str(), what.c_str()));
    if (!missing.empty()) {
        std::string names;
        for (const std::string & m : missing) names += (names.empty() ? "" : ", ") + m;
        notify(trf("Certains styles utilisent des LoRA non installées : %s (à télécharger dans Réglages › LoRA).", names.c_str()));
    }
    import_job.reset();
}

void App::delete_pack(const std::string & name, bool quiet) {
    std::erase_if(settings.presets, [&](const Preset & p) { return p.pack == name; });
    std::erase_if(settings.structures, [&](const UserStructure & st) { return st.pack == name; });
    std::erase_if(settings.packs, [&](const PackInfo & info) { return info.name == name; });
    save_settings(settings, settings_path);
    if (!quiet) notify(trf("Pack « %s » supprimé.", name.c_str()));
}

void App::begin_export() {
    export_styles.assign(settings.presets.size(), 1);
    export_structures.assign(settings.structures.size(), 1);
    open_export = true;
}

// Écrit les éléments cochés dans un fichier .yue2pack choisi par l'utilisateur ; vrai si le fichier est écrit.
bool App::do_export() {
    PackFile pack;
    pack.info             = export_info;
    pack.info.name        = trim(pack.info.name);
    pack.info.author      = trim(pack.info.author);
    pack.info.version     = trim(pack.info.version);
    pack.info.description = trim(pack.info.description);
    for (size_t i = 0; i < settings.presets.size() && i < export_styles.size(); i++) if (export_styles[i]) pack.styles.push_back(settings.presets[i]);
    for (size_t i = 0; i < settings.structures.size() && i < export_structures.size(); i++) if (export_structures[i]) pack.structures.push_back(settings.structures[i]);
    std::wstring name = widen(safe_filename(pack.info.name.empty() ? std::string(tr("Mes styles")) : pack.info.name) + ".yue2pack");
    std::wstring label = wtr("Pack Studio Audio AI");
    fs::path     path = ask_save(name, label.c_str(), L"yue2pack");
    if (path.empty()) return false;
    if (!write_pack(path, pack)) {
        notify(tr("Écriture du fichier impossible."));
        return false;
    }
    notify(trf("%d style(s) et %d structure(s) exporté(s) dans %s.", (int) pack.styles.size(), (int) pack.structures.size(),
               narrow(path.filename().wstring()).c_str()));
    return true;
}

// Carte de Réglages › Génération : export, import, packs installés.
void App::draw_packs_settings() {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.green, "%s", tr("Styles, structures et packs"));
    ImGui::PopFont();
    int own_styles = 0, own_structs = 0;
    for (const Preset & p : settings.presets) own_styles += p.pack.empty();
    for (const UserStructure & st : settings.structures) own_structs += st.pack.empty();
    muted_wrapped(trf("Vos styles (préréglages) : %d · vos structures : %d · packs installés : %d.", own_styles, own_structs, (int) settings.packs.size()));
    muted_wrapped(tr("Un fichier .yue2pack regroupe des styles et des structures : pour les sauvegarder, les partager ou installer un pack. Vous pouvez aussi glisser un fichier .yue2pack sur la fenêtre du Studio."));
    ImGui::BeginDisabled(settings.presets.empty() && settings.structures.empty());
    if (accent_button(tr("Exporter…"))) begin_export();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Importer…"))) {
        fs::path path = ask_pack();
        if (!path.empty()) import_queue.push_back(path);
    }
    if (settings.packs.empty()) return;
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    if (ImGui::BeginTable("##packs", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Pack"), 0, 2.f);
        ImGui::TableSetupColumn(tr("Auteur"), 0, 1.4f);
        ImGui::TableSetupColumn(tr("Version"), 0, 0.7f);
        ImGui::TableSetupColumn(tr("Contenu"), 0, 1.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(tr("Supprimer")).x + 20 * g_dpi);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < settings.packs.size(); i++) {
            const PackInfo & info = settings.packs[i];
            int n_style = 0, n_struct = 0;
            for (const Preset & p : settings.presets) n_style += p.pack == info.name;
            for (const UserStructure & st : settings.structures) n_struct += st.pack == info.name;
            ImGui::PushID((int) i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.name.c_str());
            if (!info.description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.description.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(P.muted, "%s", info.author.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(P.muted, "%s", info.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(P.muted, "%s", trf("%d style(s), %d structure(s)", n_style, n_struct).c_str());
            ImGui::TableNextColumn();
            if (soft_button(tr("Supprimer"), P.danger)) pending_pack_delete = info.name;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// Fenêtres liées aux packs : export, éléments déjà présents à l'import, suppression d'un pack.
// Dessinées au-dessus des Réglages s'ils sont ouverts, sinon au niveau principal (fichier déposé sur la fenêtre).
void App::draw_pack_modals() {
    // ---- Export
    if (open_export) {
        ImGui::OpenPopup(tr("Exporter des styles et structures"));
        open_export = false;
    }
    ImGui::SetNextWindowSize(ImVec2(620 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Exporter des styles et structures"), nullptr, ImGuiWindowFlags_NoResize)) {
        muted_wrapped(tr("Cochez les éléments à mettre dans le fichier."));
        ImGui::BeginChild("##export_list", ImVec2(0, 240 * g_dpi), ImGuiChildFlags_Borders);
        auto list = [&](auto & items, std::vector<char> & checked, const char * title, bool structures) {
            if (items.empty()) return;
            ImGui::SeparatorText(title);
            for (size_t i = 0; i < items.size() && i < checked.size(); i++) {
                const auto & it    = items[i];
                std::string  label = it.pack.empty() ? it.name : it.pack + " › " + it.name;
                bool         b     = checked[i] != 0;
                ImGui::PushID((int) i);
                if (ImGui::Checkbox(label.c_str(), &b)) checked[i] = b;
                ImGui::PopID();
                if (structures && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", arrow_text(settings.structures[i].sections).c_str());
            }
        };
        ImGui::PushID("styles");
        list(settings.presets, export_styles, tr("Styles"), false);
        ImGui::PopID();
        ImGui::PushID("structures");
        list(settings.structures, export_structures, tr("Structures"), true);
        ImGui::PopID();
        ImGui::EndChild();
        if (ImGui::SmallButton(tr("Tout cocher"))) {
            std::fill(export_styles.begin(), export_styles.end(), 1);
            std::fill(export_structures.begin(), export_structures.end(), 1);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Tout décocher"))) {
            std::fill(export_styles.begin(), export_styles.end(), 0);
            std::fill(export_structures.begin(), export_structures.end(), 0);
        }
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        ImGui::TextColored(P.muted, "%s", tr("Pack (facultatif : laissez le nom vide pour une simple sauvegarde)"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        tx::InputTextWithHint("##pack_name", tr("Nom du pack (ex. : Rock 80s)"), &export_info.name);
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(half);
        tx::InputTextWithHint("##pack_author", tr("Auteur"), &export_info.author);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        tx::InputTextWithHint("##pack_version", tr("Version (ex. : 1.0)"), &export_info.version);
        ImGui::SetNextItemWidth(-FLT_MIN);
        tx::InputTextWithHint("##pack_desc", tr("Description"), &export_info.description);
        int n = 0;
        for (char c : export_styles) n += c != 0;
        for (char c : export_structures) n += c != 0;
        if (modal_buttons(tr("Exporter…"), n > 0) && do_export()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Import : pack du même nom déjà installé
    const char * installed_title = tr("Pack déjà installé");
    if (import_job && import_job->pack_installed && !ImGui::IsPopupOpen(installed_title)) ImGui::OpenPopup(installed_title);
    ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
    if (ImGui::BeginPopupModal(installed_title, nullptr, ImGuiWindowFlags_NoResize)) {
        if (!import_job || !import_job->pack_installed) {
            ImGui::CloseCurrentPopup();
        } else {
            const std::string & name    = import_job->pack.info.name;
            std::string         renamed = free_pack_name(name);
            ImGui::TextWrapped("%s", trf("Le pack « %s » est déjà installé. Remplacer : l'ancien est retiré puis le nouveau est importé. Garder les deux : le nouveau est ajouté sous le nom « %s ».",
                                         name.c_str(), renamed.c_str()).c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (accent_button(tr("Remplacer le pack"))) {
                delete_pack(name, true);
                import_job->pack_installed = false;
                finish_import();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Garder les deux"))) {
                // Le pack importé prend un nom libre : ses éléments sont rangés à part et il a sa propre ligne
                import_job->pack.info.name = renamed;
                for (Preset & p : import_job->pack.styles) p.pack = renamed;
                for (UserStructure & st : import_job->pack.structures) st.pack = renamed;
                import_job->pack_installed = false;
                finish_import();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (soft_button(tr("Annuler l'import"), P.danger, false)) {
                import_job.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // ---- Import : élément déjà présent
    const char * conflict_title = tr("Élément déjà présent");
    if (import_has_conflict() && !import_job->pack_installed && !ImGui::IsPopupOpen(conflict_title)) ImGui::OpenPopup(conflict_title);
    ImGui::SetNextWindowSize(ImVec2(500 * g_dpi, 0));
    if (ImGui::BeginPopupModal(conflict_title, nullptr, ImGuiWindowFlags_NoResize)) {
        // Premier élément à trancher : style, puis structure
        int * choice = nullptr;
        std::string text;
        if (import_job) {
            for (size_t i = 0; i < import_job->style_choice.size() && !choice; i++) {
                if (import_job->style_choice[i] >= 0) continue;
                choice                = &import_job->style_choice[i];
                const Preset & p      = import_job->pack.styles[i];
                text = p.pack.empty() ? trf("Le style « %s » existe déjà dans vos styles.", p.name.c_str())
                                      : trf("Le style « %s » existe déjà dans le pack « %s ».", p.name.c_str(), p.pack.c_str());
            }
            for (size_t i = 0; i < import_job->structure_choice.size() && !choice; i++) {
                if (import_job->structure_choice[i] >= 0) continue;
                choice                  = &import_job->structure_choice[i];
                const UserStructure & st = import_job->pack.structures[i];
                text = st.pack.empty() ? trf("La structure « %s » existe déjà dans vos structures.", st.name.c_str())
                                       : trf("La structure « %s » existe déjà dans le pack « %s ».", st.name.c_str(), st.pack.c_str());
            }
        }
        if (!choice) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::Checkbox(tr("Faire de même pour les éléments suivants"), &import_job->same_for_all);
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            int picked = -1;
            if (accent_button(tr("Remplacer"))) picked = 1;
            ImGui::SameLine();
            if (ImGui::Button(tr("Garder les deux"))) picked = 2;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("L'élément importé est ajouté avec un numéro : « Nom (2) »."));
            ImGui::SameLine();
            if (ImGui::Button(tr("Ignorer"))) picked = 3;
            ImGui::SameLine();
            if (soft_button(tr("Annuler l'import"), P.danger, false)) {
                import_job.reset();
                ImGui::CloseCurrentPopup();
            } else if (picked > 0) {
                if (import_job->same_for_all) {
                    for (int & c : import_job->style_choice) if (c < 0) c = picked;
                    for (int & c : import_job->structure_choice) if (c < 0) c = picked;
                } else {
                    *choice = picked;
                }
                if (!import_has_conflict()) {
                    finish_import();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    // ---- Suppression d'un pack
    const char * delete_title = tr("Supprimer le pack ?");
    if (!pending_pack_delete.empty() && !ImGui::IsPopupOpen(delete_title)) ImGui::OpenPopup(delete_title);
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(delete_title, nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", trf("Les styles et structures du pack « %s » seront retirés du Studio. Vous pourrez le réimporter depuis son fichier .yue2pack.",
                                     pending_pack_delete.c_str()).c_str());
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_pack_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            delete_pack(pending_pack_delete);
            pending_pack_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Fenêtres « Enregistrer la structure » et « Supprimer la structure ? » (ouvertes depuis le bloc Paroles).
void App::draw_structure_modals() {
    if (open_structure) {
        ImGui::OpenPopup(tr("Enregistrer la structure"));
        open_structure = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Enregistrer la structure"), nullptr, ImGuiWindowFlags_NoResize)) {
        muted_wrapped(arrow_text(structure_to_save));
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ImGui::InputTextWithHint("##structure_name", tr("Nom de la structure (ex. : Pop courte avec pont)"), &structure_name,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        text_context_menu(structure_name);
        std::string name     = trim(structure_name);
        int         existing = find_structure(name, "");
        if (existing >= 0) colored_wrapped(P.warn, tr("Une de vos structures porte déjà ce nom : elle sera remplacée."));
        muted_wrapped(tr("Elle apparaîtra dans la liste des structures, rubrique « Mes structures », pour une chanson comme pour un instrumental."));
        bool valid = !name.empty();
        if (modal_buttons(tr("Enregistrer"), valid) || (enter && valid)) {
            UserStructure st{ name, structure_to_save, "" };
            if (existing >= 0) settings.structures[existing] = st;
            else settings.structures.push_back(st);
            save_settings(settings, settings_path);
            notify(trf("Structure « %s » enregistrée.", name.c_str()));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const char * delete_title = tr("Supprimer la structure ?");
    if (pending_structure_delete >= (int) settings.structures.size()) pending_structure_delete = -1;
    if (pending_structure_delete >= 0 && !ImGui::IsPopupOpen(delete_title)) ImGui::OpenPopup(delete_title);
    ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
    if (ImGui::BeginPopupModal(delete_title, nullptr, ImGuiWindowFlags_NoResize)) {
        if (pending_structure_delete < 0) {
            ImGui::CloseCurrentPopup();
        } else {
            const UserStructure & st = settings.structures[pending_structure_delete];
            ImGui::TextWrapped("%s", trf("La structure « %s » sera supprimée de la liste.", st.name.c_str()).c_str());
            muted_wrapped(arrow_text(st.sections));
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_structure_delete = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                std::string name = st.name;
                settings.structures.erase(settings.structures.begin() + pending_structure_delete);
                pending_structure_delete = -1;
                save_settings(settings, settings_path);
                notify(trf("Structure « %s » supprimée.", name.c_str()));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

// Packs à importer (bouton « Importer… » ou fichiers déposés sur la fenêtre), un à la fois.
void App::update_imports() {
    for (const std::wstring & w : g_dropped) {
        fs::path path(w);
        if (lower(narrow(path.extension().wstring())) == ".yue2pack") import_queue.push_back(path);
        else notify(tr("Seuls les packs .yue2pack peuvent être déposés sur la fenêtre."));
    }
    g_dropped.clear();
    if (!import_job && !import_queue.empty()) {
        fs::path next = import_queue.front();
        import_queue.erase(import_queue.begin());
        import_pack(next);
    }
}

// Résumé d'un préréglage (infobulle) : style, LoRA avec leur force, structures.
std::string App::preset_summary(const Preset & p) const {
    std::string s = p.style;
    if (!p.loras.empty()) {
        s += tr("\nLoRA :");
        for (const LoraUse & u : p.loras) {
            char sc[16];
            snprintf(sc, sizeof(sc), "%.2g", u.scale);
            for (char & c : sc) if (c == '.') c = ',';
            s += " " + lora_label(u.file) + " ×" + sc + (&u == &p.loras.back() ? "" : ",");
        }
    }
    auto arrow = [](std::string t) {
        std::string out;
        for (char c : t) out += c == ',' ? std::string(" > ") : std::string(1, c);
        return out;
    };
    if (!p.structure_song.empty()) s += tr("\nStructure (chanson) : ") + arrow(p.structure_song);
    if (!p.structure_inst.empty()) s += tr("\nStructure (instrumental) : ") + arrow(p.structure_inst);
    if (p.has_negative && !p.negative_style.empty()) {
        char sc[16];
        snprintf(sc, sizeof(sc), "%.1f", p.negative_scale);
        s += "\n" + trf("À éviter : %s (force %s)", p.negative_style.c_str(), sc);
    }
    return s;
}

// Applique un préréglage : style, LoRA (si le préréglage en contient) et structure des paroles du mode actuel.
void App::apply_preset(const Preset & p) {
    Form & f = form;
    f.style  = p.style;
    f.dirty  = true;
    if (p.has_negative) {  // préréglage récent : il mémorise le style à éviter (vide = le champ est vidé)
        f.negative_style = p.negative_style;
        f.negative_scale = p.negative_scale;
    }
    if (!p.loras.empty()) {
        scan_loras(true);
        f.loras.clear();
        std::string missing;
        for (const LoraUse & u : p.loras) {
            if (lora_files.count(u.file)) f.loras.push_back(u);
            else missing += (missing.empty() ? "" : ", ") + lora_label(u.file);
        }
        if (!missing.empty()) notify(trf("LoRA du préréglage non installée(s) : %s (à télécharger dans Réglages).", missing.c_str()));
    }
    sync_lora_triggers();  // le style du préréglage remplace l'ancien : on y remet les mots des LoRA cochées
    const std::string & st = f.instrumental ? p.structure_inst : p.structure_song;
    if (!st.empty()) {
        std::string   text   = skeleton_text(st, f.instrumental);
        std::string & target = f.instrumental ? f.lyrics_inst : f.lyrics_song;
        if (f.instrumental || !has_lyrics_text(target)) {
            target = text;
        } else {  // des paroles seraient perdues : confirmation
            f.pending_structure = true;
            f.pending_text      = text;
            f.pending_name      = p.name + tr(" (préréglage)");
        }
    }
    ImGui::ClearActiveID();
}

bool App::downloads_running() const {
    for (const auto & j : tracked_downloads) if (j->state == 0) return true;
    return false;
}

// ---- Mises à jour des modèles et des LoRA : taille locale différente de celle du dépôt.
unsigned long long App::model_local_size(const std::string & file) const {
    std::error_code ec;
    auto            n = fs::file_size(studio->paths().gguf_dir / widen(file), ec);
    return ec ? 0 : (unsigned long long) n;
}

bool App::model_update_available(const std::string & file) {
    unsigned long long r = remote->get(file), l = model_local_size(file);
    return r > 0 && l > 0 && r != l;
}

bool App::lora_update_available(const LoraEntry & e) {
    if (!lora_installed(e)) return false;
    for (int i = 0; i < e.file_count; i++) {
        unsigned long long r = remote->get(e.files[i].file);
        auto               it = lora_files.find(e.files[i].file);
        if (r > 0 && it != lora_files.end() && it->second != r) return true;
    }
    return false;
}

// Lance, en tâche de fond, la lecture des tailles à jour des modèles et LoRA installés.
void App::start_update_check() {
    scan_loras(true);
    std::vector<std::pair<std::string, std::string>> items;
    for (const char * f : MODEL_FILES_KNOWN) {
        if (model_local_size(f) > 0) items.push_back({ f, std::string(MODEL_BASE_URL) + f });
    }
    for (const LoraEntry & e : LORA_CATALOG) {
        if (!lora_installed(e)) continue;
        for (int i = 0; i < e.file_count; i++) items.push_back({ e.files[i].file, latest_url(e.files[i].url) });
    }
    refresh_analysis();
    if (analysis_present) {
        for (const AnalysisFile & f : analysis_files()) items.push_back({ f.name, f.url });
    }
    refresh_whisper();
    if (whisper_present) items.push_back({ whisper_file().name, whisper_file().url });
    if (!items.empty()) std::thread(check_remote_sizes, remote, items).detach();
}

void App::start_model_update(const std::string & file) {
    std::vector<DlFile> files = { { std::string(MODEL_BASE_URL) + file, file, remote->get(file) } };
    auto job = downloader.start("model:" + file, model_label(file), studio->paths().gguf_dir, files);
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(trf("Mise à jour de « %s » lancée.", model_label(file).c_str()));
}

void App::start_lora_update(const LoraEntry & e) {
    std::vector<DlFile> files;
    for (int i = 0; i < e.file_count; i++) files.push_back({ latest_url(e.files[i].url), e.files[i].file, remote->get(e.files[i].file) });
    auto job = downloader.start(e.id, tr(e.name), studio->paths().adapters, files);
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(trf("Mise à jour de « %s » lancée.", tr(e.name)));
}

// Annonce la fin des téléchargements et relit le dossier des LoRA.
void App::update_downloads() {
    if (remote->state == 2 && !updates_announced) {
        updates_announced = true;
        int n = 0;
        for (const char * f : MODEL_FILES_KNOWN) n += model_update_available(f);
        for (const LoraEntry & e : LORA_CATALOG) n += lora_update_available(e);
        n += analysis_update_count() > 0;
        n += whisper_update_available();
        if (n > 0) notify(trf("%d mise(s) à jour de modèles ou de LoRA disponible(s) : voir Réglages.", n));
    }
    for (const auto & j : tracked_downloads) {
        if (j->state == 0) continue;
        if (std::find(announced_downloads.begin(), announced_downloads.end(), j.get()) != announced_downloads.end()) continue;
        announced_downloads.push_back(j.get());
        if (j->key == "whisper") {
            refresh_whisper();
            if (j->state == 1) notify(tr("Modèle de transcription des paroles prêt."));
            else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
            continue;
        }
        if (j->key == "analysis") {
            refresh_analysis();
            if (j->state == 1) notify(tr("Modèle d'analyse audio prêt."));
            else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
            continue;
        }
        if (finish_ai_download(*j)) continue;  // modèles IA et llama.cpp (1.0.26)
        if (j->key.rfind("model:", 0) == 0) {
            models = studio->paths().list_models();
            if (j->state == 1) notify(trf("Modèle « %s » mis à jour : il sera utilisé pour les prochains morceaux.", j->label.c_str()));
            else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
            continue;
        }
        if (j->state == 1) notify(trf("LoRA « %s » téléchargée : elle est disponible dans le formulaire.", j->label.c_str()));
        else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
        scan_loras(true);
    }
}

void App::start_lora_download(const LoraEntry & e) {
    std::vector<DlFile> files;
    for (int i = 0; i < e.file_count; i++) files.push_back({ e.files[i].url, e.files[i].file, e.files[i].bytes });
    fs::create_directories(studio->paths().adapters);
    auto job = downloader.start(e.id, tr(e.name), studio->paths().adapters, files);
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(trf("Téléchargement de la LoRA « %s » lancé.", tr(e.name)));
}

// ---- Analyse audio : modèles d'étiquetage (téléchargement, mises à jour, suppression)
fs::path App::analysis_dir() const { return studio->paths().models_dir / "analyse"; }

  // 1.0.42 : modeles\analyse
void     App::refresh_analysis() { analysis_present = analysis_installed(analysis_dir()); }

unsigned long long App::analysis_local_size(const AnalysisFile & f) const {
    std::error_code ec;
    auto            n = fs::file_size(analysis_dir() / widen(f.name), ec);
    return ec ? 0 : (unsigned long long) n;
}

unsigned long long App::analysis_total_bytes() const {
    unsigned long long n = 0;
    for (const AnalysisFile & f : analysis_files()) n += f.bytes;
    return n;
}

int App::analysis_update_count() {
    int n = 0;
    if (!analysis_present) return 0;
    for (const AnalysisFile & f : analysis_files()) {
        unsigned long long r = remote->get(f.name), l = analysis_local_size(f);
        n += r > 0 && l > 0 && r != l;
    }
    return n;
}

void App::start_analysis_download(bool update) {
    std::vector<DlFile> files;
    for (const AnalysisFile & f : analysis_files()) {
        unsigned long long r = update ? remote->get(f.name) : 0;
        files.push_back({ f.url, f.name, r > 0 ? r : f.bytes });
    }
    std::error_code ec;
    fs::create_directories(analysis_dir(), ec);
    auto job = downloader.start("analysis", tr("Analyse audio"), analysis_dir(), files);
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(update ? tr("Mise à jour du modèle d'analyse audio lancée.") : tr("Téléchargement du modèle d'analyse audio lancé."));
}

void App::check_analysis_updates() {
    std::vector<std::pair<std::string, std::string>> items;
    for (const AnalysisFile & f : analysis_files()) items.push_back({ f.name, f.url });
    std::thread(check_remote_sizes, remote, items).detach();
}

void App::delete_analysis_files() {
    std::error_code ec;
    int             n = 0;
    for (const AnalysisFile & f : analysis_files()) n += remove_inside(analysis_dir() / widen(f.name), ec) ? 1 : 0;
    if (analysis) analysis->cancel = true;
    analysis.reset();
    refresh_analysis();
    notify(trf("Modèle d'analyse audio supprimé (%d fichiers).", n));
}

// ---- Transcription des paroles : modèle Whisper (fichier unique dans models/whisper)
fs::path App::whisper_dir() const { return studio->paths().models_dir / "voix"; }

  // 1.0.42 : modeles\voix (Whisper)
unsigned long long App::whisper_local_size() const {
    std::error_code ec;
    auto            n = fs::file_size(whisper_dir() / widen(whisper_file().name), ec);
    return ec ? 0 : (unsigned long long) n;
}

void App::refresh_whisper() { whisper_present = whisper_local_size() > 0 && fs::exists(studio->paths().whisper); }

bool App::whisper_update_available() {
    unsigned long long r = remote->get(whisper_file().name), l = whisper_local_size();
    return r > 0 && l > 0 && r != l;
}

void App::start_whisper_download(bool update) {
    const AnalysisFile & wf = whisper_file();
    unsigned long long   r  = update ? remote->get(wf.name) : 0;
    std::vector<DlFile>  files = { { wf.url, wf.name, r > 0 ? r : wf.bytes } };
    std::error_code      ec;
    fs::create_directories(whisper_dir(), ec);
    auto job = downloader.start("whisper", tr("Transcription des paroles"), whisper_dir(), files);
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(update ? tr("Mise à jour du modèle de transcription des paroles lancée.") : tr("Téléchargement du modèle de transcription des paroles lancé."));
}

void App::delete_whisper_file() {
    std::error_code ec;
    remove_inside(whisper_dir() / widen(whisper_file().name), ec);
    refresh_whisper();
    if (remove_refused) return;
    notify(tr("Modèle de transcription des paroles supprimé."));
}

  // done_flag : 1 terminé, 2 interrompu

// Raison pour laquelle la carte graphique n'est pas libre (texte vide : libre). Un morceau en pause dans la file ne gêne pas.
std::string App::busy_reason() {
    if (game_active) return tr("un jeu est en cours");
    if (studio->gate_closed()) return tr("la file de génération est suspendue");
    if (analysis && analysis->state == 0) return tr("une analyse audio est en cours");
    if (downloads_running()) return tr("un téléchargement est en cours");
    for (const Song & s : studio->snapshot()) if (s.status == "running" || s.waiting()) return tr("une génération est en cours ou en attente");
    return std::string();
}

int App::language_check_pending() {
    int n = 0;
    for (const Song & s : studio->snapshot()) {
        n += s.status == "done" && s.lyrics_lang_check == 0;  // chantés (même sans texte écrit) et instrumentaux (des paroles s'y glissent parfois)
    }
    return n;
}

// Appelée à chaque image : annonce la fin et interrompt si la carte graphique est demandée par une génération.
void App::poll_language_check() {
    if (int flag = lang_done_flag.exchange(0)) {
        int n = lang_checked, fixed = lang_fixed;
        lang_checked = lang_fixed = 0;
        if (flag == 2) notify(tr("Vérification de la langue interrompue : une génération a démarré."));
        else notify(trf("Langue des paroles vérifiée à l'écoute : %d morceau(x), %d corrigé(s).", n, fixed));
    }
    if (lang_running && studio->local_active()) lang_stop = true;
}

void App::start_language_check() {
    if (lang_running || !whisper_present) return;
    std::vector<Song> todo;
    for (const Song & s : studio->snapshot()) {
        if (s.status != "done" || s.lyrics_lang_check > 0) continue;
        todo.push_back(s);
    }
    if (todo.empty()) {
        notify(tr("Tous les morceaux chantés ont déjà été vérifiés."));
        return;
    }
    lang_total   = (int) todo.size();
    lang_checked = lang_fixed = 0;
    lang_stop    = false;
    lang_running = true;
    Studio * st  = studio;
    Paths    pa  = studio->paths();
    std::thread([this, st, pa, todo]() {
        fs::path work = app_temp_dir() / L"langue";  // 1.0.30 : dossier temporaire du programme
        for (const Song & s : todo) {
            if (lang_stop) break;
            fs::path audio;
            for (const wchar_t * n : { L"audio.mp3", L"audio.flac", L"audio.wav" }) {
                if (fs::exists(song_folder(s.id) / n)) {
                    audio = song_folder(s.id) / n;
                    break;
                }
            }
            if (audio.empty()) {
                st->set_language_check(s.id, "", 1);
                continue;
            }
            if (s.instrumental) {  // marqué instrumental : des paroles ont-elles tout de même été chantées ?
                // 1) la musique « parle-t-elle » une langue connue de façon cohérente ? (sur de la musique seule, Whisper
                //    renvoie des langues au hasard avec une faible confiance) 2) combien de mots distincts y entend-on ?
                float       lavg = 0;
                int         lvotes = 0;
                fs::path    model = pa.models_dir / "voix" / whisper_file().name;
                std::string code = detect_audio_language(audio, pa.whisper, model, work, s.audio_seconds, lavg, lvotes);
                std::string lang;
                int         words = 0;
                if (language_index_for_code(code) > 0 && lvotes >= 2 && lavg >= 0.5f) {
                    detect_audio_vocals(audio, pa.whisper, model, work, s.audio_seconds, lang, words);
                }
                if (lang_stop && code.empty()) break;
                bool vocals = words >= 10 && lang == code;
                st->set_heard_vocals(s.id, vocals ? lang : "");
                if (vocals) lang_fixed++;
                lang_checked++;
                continue;
            }
            float       avg   = 0;
            int         votes = 0;
            std::string heard = detect_audio_language(audio, pa.whisper, pa.models_dir / "voix" / whisper_file().name, work, s.audio_seconds, avg, votes);
            if (lang_stop && heard.empty()) break;  // interrompu : on recommencera plus tard
            int  li   = language_index_for_code(heard);
            bool sure = li > 0 && votes >= 2 && avg >= (s.lyrics_lang.empty() ? 0.45f : 0.6f);
            if (li > 0 && heard == s.lyrics_lang && avg >= 0.35f) {
                st->set_language_check(s.id, "", 2);  // confirmée
            } else if (sure) {
                st->set_language_check(s.id, heard, 2);
                lang_fixed++;
            } else {
                st->set_language_check(s.id, "", 1);  // non concluant : la langue déclarée ou déduite du texte reste
            }
            lang_checked++;
        }
        std::error_code ec;
        fs::remove_all(work, ec);
        lang_done_flag = lang_stop ? 2 : 1;
        lang_running   = false;
    }).detach();
}

// ---- Analyse d'un fichier audio choisi pour une reprise
void App::start_analysis() {
    Form & f = form;
    if (!analysis_present || f.cover.empty()) return;
    if (analysis) analysis->cancel = true;
    auto job  = std::make_shared<AnalysisJob>();
    job->file = f.cover;
    job->with_score = studio->can_transcribe() && !studio->local_active();  // la partition utilise la carte graphique
    job->with_lyrics = whisper_present && !studio->local_active();
    job->search_web  = settings.lyrics_web;
    analysis  = job;
    Paths    pa   = studio->paths();
    fs::path work = app_temp_dir() / L"analyse";  // 1.0.30 : dossier temporaire du programme
    std::thread([job, pa, work]() {
        AudioAnalysis out;
        auto          progress = [&](const std::string & what, float f) {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->stage    = what;
            job->progress = f;
        };
        bool ok = analyze_audio(job->file, pa.models_dir / "analyse", work, job->with_score ? pa.transcribe : fs::path(), pa.transcriber,
                                job->with_lyrics ? pa.whisper : fs::path(), pa.models_dir / "voix" / whisper_file().name, progress,
                                job->cancel, out);
        // Paroles sur Internet (morceau chanté seulement) : corrigent la transcription de Whisper.
        // Délais courts : hors ligne, l'analyse se termine quand même en quelques secondes.
        if (ok && job->search_web && out.voice >= 0.3f && !job->cancel) {
            progress("web", 0.97f);
            job->identity = guess_song_identity(job->file, out.duration);
            std::string heard;
            for (const LyricLine & l : out.lyric_lines) heard += l.text + "\n";
            job->web = fetch_web_lyrics(job->identity, heard, job->cancel);
            if (job->web.status == WebLyricsStatus::Found) {
                std::vector<std::string> secs = suggested_structure(out, false);
                bool intro = !secs.empty() && secs.front() == "Intro";
                bool outro = !secs.empty() && secs.back() == "Outro";
                job->web_text = web_lyrics_to_yue2(job->web.plain, intro, outro);
            }
        }
        job->result = out;
        if (ok) job->sugg = build_suggestions(out);
        job->state = ok ? 1 : (job->cancel ? 3 : 2);
    }).detach();
}

// Paroles proposées par l'analyse : celles d'Internet si elles ont été trouvées et vérifiées, sinon Whisper.
std::string App::analysis_lyrics(const AnalysisJob & job) {
    return !job.web_text.empty() ? job.web_text : suggested_lyrics(job.result);
}

bool App::analysis_has_lyrics(const AnalysisJob & job) {
    return !job.web_text.empty() || !job.result.lyric_lines.empty();
}

// Ajoute au style les balises cochées, règle le tempo et propose la structure ; generate : lance ensuite la génération.
void App::apply_analysis(bool generate) {
    Form & f = form;
    if (!analysis || analysis->state != 1) return;
    const AudioAnalysis & a = analysis->result;
    const bool detected = analysis->use_lyrics && analysis_has_lyrics(*analysis);
    if (generate && !f.instrumental && (a.voice < 0.35f || !(has_lyrics_text(f.lyrics_song) || detected))) {
        set_mode(f, true);
        notify(a.voice < 0.35f ? tr("Fichier instrumental : génération en mode instrumental.")
                               : tr("Pas de paroles à chanter : génération en mode instrumental."));
    }
    std::vector<std::string> tags = split_tags(f.style);
    int                      added = 0;
    for (const Suggestion & sg : analysis->sugg) {
        if (!sg.selected || (f.instrumental && sg.group == "voice")) continue;
        if (is_bpm_tag(sg.tag)) {
            tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
        } else if (std::any_of(tags.begin(), tags.end(), [&](const std::string & t) { return lower(t) == lower(sg.tag); })) {
            continue;
        }
        tags.push_back(sg.tag);
        added++;
    }
    f.style = join_style(tags);
    if (f.instrumental) f.style = set_language(f.style, "");
    if (detected && !f.instrumental) {
        std::string   text   = analysis_lyrics(*analysis);
        std::string & target = f.lyrics_song;
        if (!has_lyrics_text(target)) {
            target = text;
        } else if (!generate) {  // des paroles seraient perdues : confirmation
            f.pending_structure = true;
            f.pending_text      = text;
            f.pending_name      = tr("paroles du fichier analysé");
        }
        int li = language_index_for_code(a.lyrics_lang);
        if (li > 0) {
            f.language = li;
            f.style    = set_language(f.style, LANGUAGES[li].tag);
        }
    } else if (analysis->use_structure) {
        std::vector<std::string> secs = suggested_structure(a, f.instrumental);
        if (secs.size() >= 3) {
            std::string joined;
            for (size_t i = 0; i < secs.size(); i++) joined += (i ? "," : "") + secs[i];
            std::string   text   = skeleton_text(joined, f.instrumental);
            std::string & target = f.instrumental ? f.lyrics_inst : f.lyrics_song;
            if (f.instrumental || !has_lyrics_text(target)) {
                target = text;
            } else if (!generate) {  // des paroles seraient perdues : confirmation
                f.pending_structure = true;
                f.pending_text      = text;
                f.pending_name      = tr("structure du fichier analysé");
            }
        }
    }
    f.cursor = -1;
    f.dirty  = true;
    ImGui::ClearActiveID();
    if (!generate) {
        notify(trf("%d balise(s) ajoutée(s) au style.", added));
        return;
    }
    if (trim(f.title).empty()) f.title = narrow(f.cover.stem().wstring());
    if (trim(f.style).empty()) {
        notify(tr("Aucune balise de style à appliquer : génération annulée."));
        return;
    }
    request_submit(false);
}

// Suggestions d'une ou plusieurs catégories, en pastilles cochables.
void App::draw_suggestion_group(const char * title, std::initializer_list<const char *> groups, const ImVec4 & color) {
    std::vector<Suggestion *> items;
    for (Suggestion & sg : analysis->sugg) {
        for (const char * g : groups) if (sg.group == g) items.push_back(&sg);
    }
    if (items.empty()) return;
    ImGuiStyle & st = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", title);
    float avail = ImGui::GetContentRegionAvail().x;
    float x     = ImGui::CalcTextSize(title).x + st.ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    for (size_t i = 0; i < items.size(); i++) {
        Suggestion & sg   = *items[i];
        std::string  text = (sg.selected ? "✓ " : "") + sg.tag;
        float        w    = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
        if (x + st.ItemSpacing.x * 0.6f + w <= avail) {
            ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
            x += st.ItemSpacing.x * 0.6f + w;
        } else {
            x = w;
        }
        ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, sg.selected ? 0.55f : 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, sg.selected ? 0.65f : 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_Text, sg.selected ? P.accent_ink : color);
        ImGui::PushID((int) i);
        if (ImGui::SmallButton(text.c_str())) sg.selected = !sg.selected;
        ImGui::PopID();
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sg.why.c_str());
    }
    ImGui::PopStyleVar(2);
}

// Résultat de la recherche des paroles sur Internet, sous les balises de l'analyse.
void App::draw_web_lyrics_status(const AnalysisJob & job) {
    const WebLyrics &    w  = job.web;
    const SongIdentity & id = job.identity;
    std::string          who = id.artist + " – " + id.title;
    switch (w.status) {
        case WebLyricsStatus::NotTried: return;
        case WebLyricsStatus::Found:
            colored_wrapped(P.ok, trf("Paroles trouvées sur Internet : %s", w.source.c_str()).c_str());
            if (ImGui::IsItemHovered()) {
                std::string tip = tr("Source : lrclib.net (base de paroles ouverte). Structure reconstruite : strophes répétées = refrain.");
                if (w.match >= 0) tip += "\n" + trf("Mots entendus par Whisper retrouvés : %d %%", (int) std::lround(w.match * 100));
                ImGui::SetTooltip("%s", tip.c_str());
            }
            break;
        case WebLyricsStatus::NoIdentity: muted_wrapped(tr("Paroles sur Internet : groupe ou titre introuvables (renseignez les étiquettes du fichier ou nommez-le « Groupe - Titre »).")); break;
        case WebLyricsStatus::Offline: muted_wrapped(tr("Paroles sur Internet : pas de connexion, transcription Whisper conservée.")); break;
        case WebLyricsStatus::NotFound: muted_wrapped(trf("Paroles sur Internet : « %s » introuvable, transcription Whisper conservée.", who.c_str())); break;
        case WebLyricsStatus::Mismatch:
            muted_wrapped(trf("Paroles sur Internet : trouvées pour « %s » mais trop différentes de ce que Whisper a entendu (%d %% de mots communs) ; transcription Whisper conservée.",
                              who.c_str(), (int) std::lround(w.match * 100)));
            break;
    }
}

void App::draw_analysis_panel() {
    Form & f = form;
    if (!analysis_present) {
        muted_wrapped(tr("Analyse du style du fichier : modèle non installé (Réglages › Analyse audio)."));
        if (ImGui::SmallButton(tr("Ouvrir les réglages"))) open_settings = true;
        return;
    }
    if (!analysis || analysis->file != f.cover) {
        if (settings.analysis_auto && analysis_auto_file != f.cover) {
            analysis_auto_file = f.cover;
            start_analysis();
        } else {
            if (ImGui::Button(tr("Analyser le style de ce fichier"))) start_analysis();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Détecte genre, ambiance, instruments, voix, tempo, tonalité et structure, puis propose des balises de style."));
        }
        return;
    }
    AnalysisJob & job = *analysis;
    if (job.state == 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Analyse du fichier…"));
        ImGui::SameLine();
        progress_bar(job.progress, ImVec2(160 * g_dpi, 0));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Annuler"))) job.cancel = true;
        if (!job.with_score) colored_wrapped(P.muted, tr("Sans partition (tempo, tonalité, structure) : génération en cours ou moteur absent."));
        return;
    }
    if (job.state != 1) {
        if (job.state == 2) colored_wrapped(P.warn, trf("Analyse impossible : %s", job.result.error.c_str()).c_str());
        else ImGui::TextColored(P.muted, "%s", tr("Analyse annulée."));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Relancer l'analyse"))) start_analysis();
        return;
    }
    const AudioAnalysis & a = job.result;
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.accent, "%s", tr("Analyse du fichier"));
    ImGui::PopFont();
    ImGui::SameLine(0, 10 * g_dpi);
    if (ImGui::SmallButton(tr("Relancer l'analyse"))) start_analysis();
    {
        std::string info = a.voice >= 0.5f ? std::string(tr("voix chantée")) : std::string(tr("plutôt instrumental"));
        if (a.bpm > 0) info += trf("  ·  %d BPM", (int) std::lround(a.bpm));
        if (!a.key.empty()) info += "  ·  " + a.key;
        if (a.duration > 0) info += "  ·  " + fmt_time(a.duration);
        muted_wrapped(info);
    }
    if (!a.score_error.empty()) muted_wrapped(trf("Partition indisponible (%s).", a.score_error.c_str()));
    if (a.voice < 0.35f && !f.instrumental) muted_wrapped(tr("Ce fichier semble instrumental : passez en mode « Instrumental » pour une reprise fidèle."));
    draw_suggestion_group(tr("Genres"), { "genre" }, P.violet);
    draw_suggestion_group(tr("Ambiance"), { "mood" }, P.teal);
    draw_suggestion_group(tr("Voix"), { "voice" }, P.pink);
    draw_suggestion_group(tr("Instruments"), { "instrument" }, P.orange);
    draw_suggestion_group(tr("Tempo et tonalité"), { "tempo", "key" }, P.amber);
    if (job.sugg.empty()) muted_wrapped(tr("Aucune balise fiable détectée."));
    if (!a.lyrics_error.empty()) muted_wrapped(trf("Paroles non transcrites (%s).", a.lyrics_error.c_str()));
    draw_web_lyrics_status(job);
    if (analysis_has_lyrics(job)) {
        ImGui::Checkbox(tr("Reprendre les paroles détectées"), &job.use_lyrics);
        ImGui::SameLine(0, 8 * g_dpi);
        if (!job.web_text.empty()) ImGui::TextColored(P.ok, "%s", tr("(paroles trouvées sur Internet)"));
        else ImGui::TextColored(P.muted, "%s", trf("(%d lignes)", (int) a.lyric_lines.size()).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Relisez et corrigez les paroles dans le bloc Paroles avant de générer."));
        if (job.use_lyrics) {
            std::string preview = analysis_lyrics(job);
            ImGui::BeginChild("##lyr_preview", ImVec2(-FLT_MIN, 110 * g_dpi), ImGuiChildFlags_Borders);
            ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
            ImGui::TextWrapped("%s", preview.c_str());
            ImGui::PopStyleColor();
            ImGui::EndChild();
        }
    }
    if (!analysis_has_lyrics(job) ? !a.sections.empty() : (!a.sections.empty() && !job.use_lyrics)) {
        ImGui::Checkbox(tr("Reprendre la structure détectée dans les paroles"), &job.use_structure);
        std::string st;
        for (const std::string & t : suggested_structure(a, f.instrumental)) st += (st.empty() ? "" : " · ") + t;
        if (ImGui::IsItemHovered() && !st.empty()) ImGui::SetTooltip("%s", st.c_str());
    }
    ImGui::BeginDisabled(job.sugg.empty());
    if (accent_button(tr("Appliquer au style et aux paroles"), ImVec2(0, 0), &P.violet)) apply_analysis(false);
    ImGui::SameLine();
    if (soft_button(tr("Appliquer et générer"), P.green, false)) apply_analysis(true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(tr("Mode automatique : applique les balises cochées et la structure, puis lance la génération. Sans paroles à chanter, la reprise est générée en instrumental."));
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
}

std::string App::fmt_gb(unsigned long long bytes) {
    char buf[32];
    // Unités binaires comme le Gestionnaire des tâches de Windows (12 Go = 12 Gio)
    if (bytes >= 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), tr("%.1f Go"), bytes / 1073741824.0);
    else snprintf(buf, sizeof(buf), tr("%.0f Mo"), bytes / 1048576.0);
    for (char & c : buf) if (c == '.') c = ',';
    return buf;
}

float App::gauge_height() const { return (settings.gauge_style == 0 ? 58 * g_dpi + 2 * ImGui::GetTextLineHeight() : 48 * g_dpi); }

void App::draw_dial(int index, ImVec2 p, float w, float h, const char * label, const std::string & value, float fraction, int limit, const std::string & tip) {
    const bool sport = settings.gauge_style == 0;
    ImGui::SetCursorScreenPos(p);
    ImGui::PushID(index);
    ImGui::InvisibleButton("##compteur", ImVec2(w, h));
    ImGui::PopID();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s : %s\n%s", label, value.c_str(), tip.c_str());
    float & f = gauge_anim[index];
    f += (std::clamp(fraction, 0.f, 1.f) - f) * std::min(1.f, ImGui::GetIO().DeltaTime * 6.f);

    ImDrawList * dl  = ImGui::GetWindowDrawList();
    const float  th  = ImGui::GetTextLineHeight();
    const float  r   = sport ? std::min(46 * g_dpi, w * .46f) : std::min(28 * g_dpi, w * .46f);
    const float  cx  = sport ? p.x + w * .5f : p.x + r + 4 * g_dpi;
    const float  cy  = sport ? p.y + 4 * g_dpi + r + 4 * g_dpi : p.y + h - 8 * g_dpi;
    const float  pi  = 3.14159265f;
    auto at = [&](float t, float radius) { const float a = pi + pi * t; return ImVec2(cx + radius * std::cos(a), cy + radius * std::sin(a)); };
    const int    steps = 48;
    const float  thick = (sport ? 7 : 5) * g_dpi;
    for (int i = 0; i < steps; i++) {
        const float t0 = i / (float) steps, t1 = (i + 1) / (float) steps;
        dl->AddLine(at(t0, r), at(t1 + .004f, r), ImGui::GetColorU32(t0 <= f && f > 0 ? gauge_color(t0) : P.chip), thick);
    }
    if (sport) {
        for (int i = 0; i <= 20; i++) {
            const bool major = i % 5 == 0;
            dl->AddLine(at(i / 20.f, r - thick), at(i / 20.f, r - thick - (major ? 7 : 4) * g_dpi), ImGui::GetColorU32(major ? P.text : P.muted), major ? 1.5f * g_dpi : 1.f);
        }
    }
    if (limit < 100) dl->AddLine(at(limit / 100.f, r - thick * .9f), at(limit / 100.f, r + thick), ImGui::GetColorU32(P.amber), 3 * g_dpi);
    const ImVec2 tip_pt = at(f, r - thick - (sport ? 3 : 1) * g_dpi);
    const float  a = pi + pi * f, nx = -std::sin(a) * 2.6f * g_dpi, ny = std::cos(a) * 2.6f * g_dpi;
    dl->AddTriangleFilled(tip_pt, ImVec2(cx + nx, cy + ny), ImVec2(cx - nx, cy - ny), IM_COL32(255, 72, 60, 255));
    dl->AddCircleFilled(ImVec2(cx, cy), 5 * g_dpi, ImGui::GetColorU32(P.chip));
    dl->AddCircle(ImVec2(cx, cy), 5 * g_dpi, ImGui::GetColorU32(P.muted));

    ImGui::PushFont(g_bold);
    const ImVec2 vs = ImGui::CalcTextSize(value.c_str());
    ImGui::PopFont();
    const ImVec2 ls = ImGui::CalcTextSize(label);
    ImVec2       vp, lp;
    if (sport) {
        vp = ImVec2(cx - vs.x * .5f, cy + 7 * g_dpi);
        lp = ImVec2(cx - ls.x * .5f, vp.y + th);
    } else {
        vp = ImVec2(cx + r + 10 * g_dpi, p.y + h * .5f - th);
        lp = ImVec2(vp.x, vp.y + th);
    }
    // fenêtre étroite : le texte qui ne tient pas reste dans la bulle
    if (sport ? vs.x <= w : vp.x + vs.x <= p.x + w) {
        ImGui::PushFont(g_bold);
        dl->AddText(vp, ImGui::GetColorU32(P.text), value.c_str());
        ImGui::PopFont();
    }
    if (sport ? ls.x <= w : lp.x + ls.x <= p.x + w) dl->AddText(lp, ImGui::GetColorU32(P.muted), label);
}

  // (instant, utilisation en %)
double App::gpu_average(double now, double value) {
    if (value >= 0 && (gpu_history.empty() || now - gpu_history.back().first >= 1.9)) gpu_history.push_back({ now, value });
    while (!gpu_history.empty() && now - gpu_history.front().first > 16.0) gpu_history.erase(gpu_history.begin());
    if (gpu_history.empty()) return -1;
    double sum = 0;
    for (auto & h : gpu_history) sum += h.second;
    return sum / gpu_history.size();
}

void App::draw_gauges(ImVec2 p, float width, float height) {
    GpuMem g    = gpu_monitor.get();
    float  cell = std::floor(width / 4);
    if (cell < 44 * g_dpi) return;  // fenêtre trop étroite : pas de place sans recouvrir les boutons
    std::string used_text = tr("indisponible"), spill_text = tr("indisponible"), used_tip = tr("Mémoire vidéo dédiée occupée sur la carte, tous programmes confondus.");
    float       used_f = 0, spill_f = 0;
    if (g.ok) {
        used_text = fmt_gb(g.used);
        used_f    = g.total ? (float) ((double) g.used / (double) g.total) : 0.f;
        if (g.total) used_tip = trf("%.0f %%", 100.0 * g.used / g.total) + " · " + fmt_gb(g.total) + "\n" + used_tip;
        const double scale = 2.0 * 1073741824.0;  // compteur plein à 2 Go de débordement
        spill_text = fmt_gb(g.shared);
        spill_f    = (float) std::min(1.0, (double) g.shared / scale);
    }
    draw_dial(0, p, cell, height, tr("Mémoire GPU"), used_text, used_f, 100, used_tip);
    draw_dial(1, ImVec2(p.x + cell, p.y), cell, height, tr("Débordement"), spill_text, spill_f, 100,
              tr("Mémoire système utilisée par la carte quand sa mémoire est pleine : bien plus lente,\nelle ralentit fortement la génération. Compteur plein = 2 Go."));
    // limit : limite réglée (GPU seulement, 100 = aucune), repère orange sur le compteur.
    double gpu_avg = gpu_average(ImGui::GetTime(), g.gpu);
    auto load = [&](int index, const char * label, double pct, const char * tip, int limit) {
        std::string text = pct < 0 ? std::string(tr("indisponible")) : trf("%.0f %%", pct);
        std::string info = tip;
        float       f    = pct < 0 ? 0.f : (float) (pct / 100.0);
        if (limit < 100 && pct >= 0 && gpu_avg >= 0) {
            // Avec une limite : le compteur montre la moyenne sur 16 s, comparée à la limite.
            text = trf("%.0f %%", gpu_avg);
            f    = (float) (gpu_avg / 100.0);
            info += "\n" + trf("Instantané : %.0f %%", pct) + "\n" + trf("Limite réglée : %d (repère orange).", limit) + "\n" +
                    tr("Avec une limite, le compteur montre la moyenne des 16 dernières secondes : sur un long morceau, la carte alterne des pas de plusieurs secondes à pleine puissance et des pauses, la limite s'applique en moyenne.");
        }
        draw_dial(index, ImVec2(p.x + cell * index, p.y), cell, height, label, text, f, limit, info);
    };
    load(2, tr("Utilisation CPU"), g.cpu, tr("Utilisation du processeur, tous programmes confondus (comme le Gestionnaire des tâches)."), 100);
    load(3, tr("Utilisation GPU"), g.gpu,
         tr("Utilisation de la carte graphique : moteur le plus chargé (3D, calcul, vidéo…), tous programmes confondus.\nPendant une génération, elle doit rester proche du maximum."), settings.gpu_limit);
}

std::string App::model_label(const std::string & file) {
    if (file.empty()) return "Q8_0";
    std::string s = file;
    if (s.rfind("YuE2-3B-", 0) == 0) s = s.substr(8);
    if (s.size() > 5 && s.compare(s.size() - 5, 5, ".gguf") == 0) s.resize(s.size() - 5);
    return s;
}

void App::init_models() {
    models        = studio->paths().list_models();
    current_model = settings.model;
    if (std::find(models.begin(), models.end(), current_model) == models.end()) {
        // Modèle par défaut conseillé : Q6_K (qualité jugée équivalente, plus léger), à défaut Q8_0
        current_model = models.empty() ? "" : models.front();
        for (const char * preferred : { "YuE2-3B-Q6_K.gguf", "YuE2-3B-Q8_0.gguf" }) {
            if (std::find(models.begin(), models.end(), preferred) != models.end()) {
                current_model = preferred;
                break;
            }
        }
    }
    studio->set_active_model(current_model);
}

const char * App::solver_label(const std::string & s) { return solver_spec(s).label; }

// Clé canonique d'un réglage mémorisé (anciennes valeurs « ab2 » / « midpoint » comprises ; vide = AB2 32, le défaut).
std::string App::solver_key(const std::string & s) { return s.empty() ? std::string("ab2_32") : std::string(solver_spec(s).key); }

// Conseils pour ce PC ; rec_model / rec_solver : réglages conseillés (fichier GGUF, clé de solveur).
std::vector<App::Advice> App::build_advice(std::string & rec_model, std::string & rec_solver) const {
    std::vector<Advice> out;
    const GpuInfo &     g      = gpu_profile().gpu;
    const double        vram   = (double) g.vram / 1073741824.0;
    const double        ram    = (double) sysinfo.ram_total / 1073741824.0;
    // Modèle : la génération occupe 4 à 7 Go en plus du modèle (2,6 à 3,8 Go selon la quantification).
    if (vram >= 15) rec_model = "YuE2-3B-Q8_0.gguf";
    else if (vram >= 11) rec_model = "YuE2-3B-Q6_K.gguf";
    else rec_model = "YuE2-3B-Q5_K_M.gguf";
    rec_solver = vram >= 11 && g.vendor != GpuVendor::Intel ? "ab2_32" : "ab3_20";
    if (!g.found) {
        out.push_back({ 2, tr("Carte graphique non reconnue : recommandations impossibles.") });
        return out;
    }
    out.push_back({ 0, trf("Modèle conseillé : %s (carte de %.0f Go).", model_label(rec_model).c_str(), vram) });
    if (vram >= 15) out.push_back({ 0, tr("Durée : jusqu'à 6 minutes sans débordement.") });
    else if (vram >= 11) out.push_back({ 0, tr("Durée : jusqu'à 4 à 5 minutes sans débordement, si aucun autre programme n'occupe la carte.") });
    else if (vram >= 8) out.push_back({ 1, tr("Durée : 3 minutes au plus ; au-delà, la mémoire de la carte déborde et la génération ralentit fortement.") });
    else out.push_back({ 2, tr("Carte trop juste (moins de 8 Go) : 2 minutes au plus, débordement probable.") });
    out.push_back({ 0, trf("Solveur conseillé : %s.", solver_label(rec_solver)) });
    if (g.vendor == GpuVendor::Amd) out.push_back({ 0, tr("Carte AMD : mode stable (FP32) conservé, indispensable sur les RX 6000.") });
    else if (g.vendor == GpuVendor::Nvidia && !gpu_profile().cuda_installed) out.push_back({ 1, tr("Carte Nvidia : installez le moteur CUDA (dossier « outils\\moteur\\cuda »), souvent plus rapide que Vulkan.") });
    else if (g.vendor == GpuVendor::Nvidia) out.push_back({ 0, tr("Carte Nvidia : moteur CUDA installé.") });
    else out.push_back({ 1, tr("Carte Intel ou inconnue : non testée, gardez le mode stable.") });
    if (ram > 0 && ram < 15) out.push_back({ 2, trf("Mémoire vive : %.0f Go. 16 Go conseillés : en cas de débordement, la carte se sert de la mémoire vive.", ram) });
    else if (ram >= 31) out.push_back({ 0, trf("Mémoire vive : %.0f Go, confortable.", ram) });
    else if (ram > 0) out.push_back({ 0, trf("Mémoire vive : %.0f Go, suffisante.", ram) });
    if (sysinfo.models_disk == DiskKind::Hdd) {
        out.push_back({ 2, trf("Les modèles sont sur un disque dur (%s) : chaque génération recharge 3 à 4 Go, soit environ 30 s de plus. Placez le dossier des modèles sur un SSD.", sysinfo.models_drive.c_str()) });
    } else if (sysinfo.models_disk == DiskKind::Ssd) {
        out.push_back({ 0, trf("Modèles sur un SSD (%s) : chargement rapide.", sysinfo.models_drive.c_str()) });
    }
    if (sysinfo.threads > 0 && sysinfo.threads < 8) out.push_back({ 1, tr("Processeur de moins de 8 cœurs logiques : l'analyse audio et les exports seront plus lents.") });
    return out;
}

// Réglages › Système › Recommandations pour ce PC.
void App::draw_advice() {
    const GpuInfo & g   = gpu_profile().gpu;
    const char *    dsk = sysinfo.models_disk == DiskKind::Ssd ? "SSD" : sysinfo.models_disk == DiskKind::Hdd ? tr("disque dur") : tr("type inconnu");
    muted_wrapped(trf("%s · %.0f Go de mémoire vive · %s (%d cœurs logiques) · disque des modèles : %s", g.name.c_str(),
                      (double) sysinfo.ram_total / 1073741824.0, sysinfo.cpu_name.c_str(), sysinfo.threads, dsk));
    std::string rec_model, rec_solver;
    for (const Advice & a : build_advice(rec_model, rec_solver)) {
        ImVec4 c = a.level == 0 ? P.ok : a.level == 1 ? P.amber : P.warn;
        ImGui::TextColored(c, "%s", "●");  // pastille verte (bon), ambre (conseil) ou orange (attention)
        ImGui::SameLine(0, 6 * g_dpi);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(a.text.c_str());
        ImGui::PopTextWrapPos();
    }
    const bool installed = std::find(models.begin(), models.end(), rec_model) != models.end();
    const bool same      = solver_key(settings.solver) == rec_solver && (!installed || settings.model == rec_model);
    ImGui::BeginDisabled(same);
    if (accent_button(tr("Appliquer les réglages conseillés"), ImVec2(0, 0), &P.green)) {
        if (installed) {
            settings.model = rec_model;
            current_model  = rec_model;
        }
        settings.solver = rec_solver;
        current_solver  = rec_solver;
        studio->set_active_solver(rec_solver);
        save_settings(settings, settings_path);
        notify(tr("Réglages conseillés appliqués (modèle et solveur par défaut)."));
    }
    ImGui::EndDisabled();
    if (same) {
        ImGui::SameLine();
        ImGui::TextColored(P.ok, "%s", tr("● réglages déjà conformes"));
    } else if (!installed) {
        ImGui::SameLine();
        colored_wrapped(P.muted, trf("(modèle %s non installé : seul le solveur sera changé)", model_label(rec_model).c_str()).c_str());
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
}

  // en Go
// Vrai si la génération peut démarrer sans déborder. Sinon remplit vram_hogs / vram_need / vram_free.
bool App::vram_check_ok() {
    if (studio->local_active()) return true;  // une génération tourne déjà : le morceau attendra son tour
    GpuMem g = gpu_monitor.get();
    if (!g.ok || g.total == 0) return true;  // compteurs indisponibles : pas de vérification
    const Form &    f = form;
    std::error_code ec;
    double model_gb = current_model.empty() ? 3.8 : (double) fs::file_size(studio->paths().gguf_dir / widen(current_model), ec) / 1073741824.0;
    if (ec) model_gb = 3.8;
    // Durée prévue : celle de la reprise, l'estimation d'après les paroles (durée automatique) ou le plafond.
    double secs = f.duration;
    if (!f.cover.empty() && f.cover_seconds > 0) secs = std::min(secs, f.cover_seconds);
    else if (f.auto_duration) secs = std::min(secs, estimate_seconds(f.instrumental ? f.lyrics_inst : f.lyrics_song, f.instrumental));
    // Mesures sur RX 6700 XT : 7 Go (Q6_K, 2 min) à 10 Go (Q8_0, 6 min) ; environ 4 Go de travail + 0,4 Go par minute.
    vram_need = model_gb + 4.0 + 0.4 * std::clamp(secs / 60.0, 1.0, 6.0);
    // Balises négatives : le moteur compose deux versions en parallèle, son cache de travail double
    // (mesuré : 2,7 Go de plus avec Q6_K, quelle que soit la durée).
    if (!trim(f.negative_style).empty()) vram_need += 2.7;
    vram_free = (double) (g.total > g.used ? g.total - g.used : 0) / 1073741824.0;
    if (vram_free >= vram_need) return true;
    const GpuInfo & gi = gpu_profile().gpu;
    vram_hogs = gpu_memory_by_process(gi.found ? gi.luid : g_adapter_luid, 6);
    DWORD self = GetCurrentProcessId();
    vram_hogs.erase(std::remove_if(vram_hogs.begin(), vram_hogs.end(), [&](const GpuProcess & p) { return p.pid == self; }), vram_hogs.end());
    return false;
}

// Bouton « Générer » : vérifie d'abord la mémoire vidéo libre ; « Ajouter en pause » ne démarre rien.
void App::request_submit(bool held) {
    // L'IA locale occupe la carte graphique ou le processeur : pas de génération en même temps (1.0.26).
    if (ai_local_busy()) {
        notify(tr("L'IA locale est en train d'écrire : attendez la fin (ou « Arrêter ») avant de générer."));
        return;
    }
    if (held || vram_check_ok()) submit(held);
    else open_vram = true;
}

void App::draw_vram_popup() {
    if (open_vram) {
        ImGui::OpenPopup(tr("Mémoire vidéo insuffisante"));
        open_vram = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560 * g_dpi, 0));
    if (!ImGui::BeginPopupModal(tr("Mémoire vidéo insuffisante"), nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("%s", trf("Cette génération demande environ %.1f Go de mémoire vidéo ; il en reste %.1f Go de libre. Le reste débordera vers la mémoire système et la génération sera beaucoup plus lente (2 à 8 fois).",
                                 vram_need, vram_free).c_str());
    if (!vram_hogs.empty()) {
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        ImGui::TextColored(P.muted, "%s", tr("Programmes qui occupent la carte :"));
        for (const GpuProcess & p : vram_hogs) {
            ImGui::BulletText("%s — %s", p.name.c_str(), fmt_gb(p.dedicated).c_str());
        }
        muted_wrapped(tr("Fermez ceux dont vous n'avez pas besoin (navigateur, jeux, logiciels vidéo), puis relancez la génération."));
    }
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    if (accent_button(tr("Générer quand même"), ImVec2(0, 0), &P.warn)) {
        submit(false);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Ajouter en pause"))) {
        submit(true);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Gestionnaire des tâches"))) ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Réglages › Système › Carte graphique : carte détectée, moteur utilisé, compatibilité et choix du moteur.
// Limite d'utilisation de la carte : de 30 à 100 % (100 = aucune limite), appliquée aussi pendant une génération.
void App::draw_gpu_limit() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Limite d'utilisation du GPU"));
    ImGui::SameLine(0, 10 * g_dpi);
    ImGui::SetNextItemWidth(std::min(280 * g_dpi, ImGui::GetContentRegionAvail().x));
    std::string shown = settings.gpu_limit >= 100 ? std::string(tr("aucune limite")) : std::to_string(settings.gpu_limit) + " %%";  // %% : format d'ImGui
    if (ImGui::SliderInt("##gpu_limit", &settings.gpu_limit, 30, 100, shown.c_str(), ImGuiSliderFlags_AlwaysClamp)) {
        set_gpu_limit(settings.gpu_limit);  // pris en compte dès le pas suivant, même pendant une génération
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Le moteur est mis en pause par intervalles très courts pour laisser souffler la carte (température, bruit, autres programmes).\nExemple : limite à 90 → génération environ 1,1 fois plus longue. Effet approximatif : la jauge « Utilisation GPU » fait foi."));
    }
}

void App::draw_gpu_profile() {
    GpuProfile &    gp = gpu_profile();
    const GpuInfo & g  = gp.gpu;
    if (g.found) {
        ImGui::TextColored(P.muted, tr("Carte : %s (%s, %.0f Go)"), g.name.c_str(), gpu_vendor_name(g.vendor),
                           (double) g.vram / 1073741824.0);
    } else {
        ImGui::TextColored(P.warn, "%s", tr("Carte graphique non reconnue."));
    }
    ImGui::TextColored(P.muted, tr("Moteur utilisé : %s"), gp.cuda_active ? "CUDA (Nvidia)" : "Vulkan");

    // Compatibilité : mode stable (noyaux FP32) ou rapide (FP16). Pris en compte dès la génération suivante.
    const char * compat_labels[] = { N_("Automatique"), N_("Stable (plus lent, sans grésillement)"), N_("Rapide (FP16)") };
    const char * compat_notes[]  = {
        N_("Stable sur les cartes AMD et Intel, rapide sur les cartes Nvidia."),
        N_("Impose les calculs en précision complète (FP32) : indispensable sur les cartes AMD RDNA2 (RX 6000), à essayer si un morceau grésille ou sature."),
        N_("Calculs en demi-précision : plus rapide, mais peut produire des grésillements sur certaines cartes AMD."),
    };
    // Les deux listes commencent au même endroit : après le plus long des deux libellés.
    const float label_w = std::max(ImGui::CalcTextSize(tr("Compatibilité de la carte")).x, ImGui::CalcTextSize(tr("Moteur de calcul")).x) + 10 * g_dpi;
    const float combo_x = ImGui::GetCursorPosX() + label_w;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Compatibilité de la carte"));
    ImGui::SameLine(combo_x);
    ImGui::SetNextItemWidth(320 * g_dpi);
    int c = gp.compat.load();
    if (ImGui::BeginCombo("##gpu_compat", tr(compat_labels[c]))) {
        for (int i = 0; i < 3; i++) {
            if (ImGui::Selectable(tr(compat_labels[i]), c == i)) {
                gp.compat           = i;
                settings.gpu_compat = compat_to_string(i);
                save_settings(settings, settings_path);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(compat_notes[i]));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 10 * g_dpi);
    ImGui::TextColored(P.muted, "%s", gp.stable() ? tr("→ mode stable") : tr("→ mode rapide"));
    if (gp.cuda_active) muted_wrapped(tr("Avec le moteur CUDA, ce réglage n'a pas d'effet (il concerne le moteur Vulkan)."));

    // Moteur de calcul : Vulkan (toutes cartes) ou CUDA (Nvidia, si le dossier « moteur\cuda » ou l'ancien « moteur_cuda » est installé).
    const char * backend_labels[] = { N_("Automatique"), "Vulkan", "CUDA" };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Moteur de calcul"));
    ImGui::SameLine(combo_x);
    ImGui::SetNextItemWidth(320 * g_dpi);
    int b = gp.backend.load();
    if (ImGui::BeginCombo("##gpu_backend", tr(backend_labels[b]))) {
        for (int i = 0; i < 3; i++) {
            const bool cuda_missing = i == BACKEND_CUDA && !gp.cuda_installed;
            ImGui::BeginDisabled(cuda_missing);
            if (ImGui::Selectable(tr(backend_labels[i]), b == i)) {
                gp.backend           = i;
                settings.gpu_backend = backend_to_string(i);
                save_settings(settings, settings_path);
            }
            ImGui::EndDisabled();
            if (cuda_missing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", tr("Moteur CUDA non installé : dossier « outils\\moteur\\cuda » absent."));
            }
        }
        ImGui::EndCombo();
    }
    if (gp.want_cuda() != gp.cuda_active) {
        ImGui::TextColored(P.warn, "%s", tr("Redémarrez l'application pour changer de moteur."));
    } else if (g.vendor == GpuVendor::Nvidia && !gp.cuda_installed) {
        muted_wrapped(tr("Carte Nvidia : le moteur Vulkan fonctionne. Le moteur CUDA, souvent plus rapide, s'installe en décompressant son archive dans le dossier de l'application (dossier « outils\\moteur\\cuda »)."));
    }
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
}

void App::init_solver() {
    current_solver = solver_key(settings.solver);
    studio->set_active_solver(current_solver);
}

// as_default : choisit aussi le solveur par défaut au démarrage (Réglages).
void App::draw_solver_combo(const char * id, float width, bool as_default) {
    const std::string shown = as_default ? solver_key(settings.solver) : current_solver;
    ImGui::SetNextItemWidth(width);
    const bool open = ImGui::BeginCombo(id, solver_label(shown));
    if (open) {
        for (const SolverSpec & sp : solver_specs()) {
            char line[96];
            snprintf(line, sizeof(line), tr("%s   ·   %d évaluations"), sp.label, sp.evals);
            if (ImGui::Selectable(line, shown == sp.key)) {
                current_solver = sp.key;
                if (as_default) {
                    settings.solver = sp.key;
                    save_settings(settings, settings_path);
                }
                studio->set_active_solver(sp.key);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(sp.note));
        }
        ImGui::EndCombo();
    }
}

// Liste déroulante des modèles ; as_default : choisit aussi le modèle par défaut (Réglages).
void App::draw_model_combo(const char * id, float width, bool as_default) {
    const std::string & shown = as_default ? (settings.model.empty() ? current_model : settings.model) : current_model;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, model_label(shown).c_str())) {
        for (const std::string & m : models) {
            std::error_code ec;
            double          gb = (double) fs::file_size(studio->paths().gguf_dir / widen(m), ec) / 1e9;
            char            line[96];
            snprintf(line, sizeof(line), tr("%s   (%.2f Go)"), model_label(m).c_str(), gb);
            if (ImGui::Selectable(line, m == shown)) {
                current_model = m;
                if (as_default) {
                    settings.model = m;
                    save_settings(settings, settings_path);
                }
                studio->set_active_model(m);
            }
        }
        ImGui::EndCombo();
    }
}

// Nombre d'images retenu pour un morceau selon le choix de l'utilisateur ; 0 = automatique (l'IA décide, 6 à 12)
int App::clip_images_for(const Song & s) {
    const std::string & m    = settings.clip_image_mode;
    const int           base = video_rec.count(s.id) ? video_rec[s.id] : 0;
    if (m == "fixed") return std::clamp(atoi(settings.clip_image_count.c_str()), 0, 60);
    if (m == "more") return std::min(60, (int) (base * 1.5 + 0.5));
    if (m == "less") return std::max(3, (int) (base * 0.5 + 0.5));
    return base;
}

  // version plus récente du Studio
std::string App::update_address() const { return settings.update_url.empty() ? std::string(UPDATE_MANIFEST_URL) : settings.update_url; }

// Recherche de mise à jour en tâche de fond (au démarrage, ou bouton « Vérifier maintenant »).
void App::start_app_update_check() {
    if (app_update->state == 1) return;
    app_update = std::make_shared<UpdateInfo>();
    std::thread(check_for_update, app_update, update_address()).detach();
}

// Pastille de l'en-tête : version, et « Mise à jour x.y.z » quand une version plus récente est publiée.
void App::draw_version_badge() {
    ImGui::SameLine(0, 8 * g_dpi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", STUDIO_VERSION);
    if (app_update->state == 3) {
        ImGui::SameLine(0, 8 * g_dpi);
        if (soft_button(trf("Mettre à jour (%s)", app_update->latest.c_str()).c_str(), P.amber) && !app_update->url.empty()) {
            ShellExecuteW(nullptr, L"open", widen(app_update->url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::IsItemHovered()) {
            std::string tip = trf("Version installée : %s · nouvelle version : %s\nOuvre la page de la dernière version sur GitHub (fichiers à télécharger).", STUDIO_VERSION, app_update->latest.c_str());
            if (!app_update->notes.empty()) tip += "\n\n" + app_update->notes;
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
}

// Réglages › Système : version installée et état de la recherche de mise à jour.
// 1.0.84 : licences des modèles (vérifiées sur Hugging Face le 5/11/2026). La commercialisation des morceaux en dépend.
void App::draw_model_licences() {
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.amber, "%s", tr("Licences des modèles"));
    ImGui::PopFont();
    colored_wrapped(P.text, tr("La commercialisation des morceaux, pochettes et vidéos créés avec Studio Audio AI est soumise aux licences des modèles utilisés. Référez-vous à leurs licences :"));
    struct Row { const char * use; const char * models; const char * licence; bool nc; };
    const Row rows[] = {
        { N_("Musique"), "YuE2-3B, YuE2-Vae", "CC BY-NC 4.0", true },
        { N_("Reprises et partitions"), "MERT-v2-FullSong, SheetSage2", "CC BY-NC 4.0", true },
        { N_("Analyse audio"), "Essentia (MTG)", "CC BY-NC-SA 4.0", true },
        { N_("LoRA"), "—", N_("licence propre à chaque LoRA (voir sa page)"), false },
        { N_("Transcription des paroles"), "Whisper", "MIT", false },
        { N_("Pochettes et clips"), "Z-Image Turbo, MOSS-Image", "Apache 2.0", false },
        { N_("Pochettes et clips"), "FLUX.3 klein", N_("FLUX Kommunity : commercial seulement sous 5 M$ de revenus"), true },
        { N_("Pochettes et clips"), "SDXL-Lightning", "CreativeML OpenRAIL++-M", false },
        { N_("IA de texte"), "Mistral Nemo, Mistral Small, Qwen 3, Qwen 2.5 (7B, 14B)", "Apache 2.0", false },
        { N_("IA de texte"), "Qwen 2.5 3B", "Qwen Research", true },
        { N_("IA de texte"), "Llama 3.1", "Llama 3.1 Community License", false },
        { N_("IA de texte"), "Gemma 2, Gemma 3", "Gemma Terms of Use", false },
    };
    if (ImGui::BeginTable("model_licences", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Usage"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Modèles"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(tr("Licence"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableHeadersRow();
        for (const Row & r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tr(r.use));
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", r.models);
            ImGui::TableNextColumn();
            if (r.nc) ImGui::TextColored(P.amber, "%s · %s", r.licence, tr("non commercial"));
            else ImGui::TextWrapped("%s", tr(r.licence));
        }
        ImGui::EndTable();
    }
    muted_wrapped(tr("Les textes complets sont sur la page de chaque modèle (Hugging Face) ; ceux des programmes livrés sont dans le dossier « LICENSES »."));
    if (ImGui::SmallButton(tr("Ouvrir le dossier LICENSES"))) ShellExecuteW(nullptr, L"open", (app_dir() / L"LICENSES").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::draw_update_settings() {
    ImGui::TextColored(P.muted, tr("Version installée : %s"), STUDIO_VERSION);
    ImGui::SameLine(0, 12 * g_dpi);
    switch (app_update->state.load()) {
        case 0: colored_wrapped(P.muted, tr("Recherche de mise à jour : pas encore de site de publication.")); break;
        case 1: ImGui::TextColored(P.muted, "%s", tr("Recherche d'une mise à jour…")); break;
        case 2: ImGui::TextColored(P.ok, "%s", tr("● à jour")); break;
        case 3: colored_wrapped(P.amber, trf("Mise à jour disponible : %s", app_update->latest.c_str()).c_str()); break;
        default: ImGui::TextColored(P.muted, "%s", tr("Recherche de mise à jour impossible (pas de connexion ?).")); break;
    }
    if (!update_address().empty()) {
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::BeginDisabled(app_update->state == 1);
        if (ImGui::SmallButton(tr("Vérifier maintenant"))) start_app_update_check();
        ImGui::EndDisabled();
    }
    // 1.0.81 : sauvegarde automatique du jour et vérification au démarrage
    ImGui::TextColored(P.muted, "%s", last_backup.empty() ? tr("Sauvegarde automatique : aucune pour l'instant.")
                                                          : trf("Sauvegarde automatique : %s (%d gardées)", last_backup.c_str(), backup_count).c_str());
    ImGui::SameLine(0, 12 * g_dpi);
    if (ImGui::SmallButton(tr("Ouvrir les sauvegardes"))) ShellExecuteW(nullptr, L"open", backups_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    draw_update_panel();  // 1.0.98
    draw_disk_panel();
    draw_sound_panel();   // 1.0.97
    if (startup_issues.empty()) ImGui::TextColored(P.ok, "%s", tr("● Vérification au démarrage : aucun fichier manquant."));
    for (const std::string & s : startup_issues) colored_wrapped(P.amber, "• " + s);
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
}

fs::path App::backups_dir() const { return app_dir() / L"sauvegardes"; }

// Fichiers de modeles\ d'au moins 1 Mo (les notes .txt et petites fiches ne comptent pas), chemin relatif → taille
std::map<std::string, unsigned long long> App::model_files() const {
    std::map<std::string, unsigned long long> out;
    std::error_code ec;
    const fs::path root = studio->paths().models_dir;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        unsigned long long size = (unsigned long long) it->file_size(ec);
        if (size >= (1ull << 20)) out[narrow(it->path().lexically_relative(root).generic_wstring())] = size;
    }
    return out;
}

void App::startup_checks() {
    std::error_code ec;
    const fs::path        root = backups_dir();
    std::vector<fs::path> olds;
    for (const auto & e : fs::directory_iterator(root, ec))
        if (e.is_directory(ec)) olds.push_back(e.path());
    std::sort(olds.begin(), olds.end());
    // 1. Modèles et LoRA présents à la dernière sauvegarde, absents maintenant
    const auto models   = model_files();
    bool       vanished = false;  // un fichier a disparu : la fenêtre s'ouvre à chaque lancement, pas une fois par jour
    if (!olds.empty()) {
        std::ifstream in(olds.back() / L"modeles.txt");
        const std::string when = narrow(olds.back().filename().wstring());
        for (std::string line; std::getline(in, line);) {
            std::string name = line.substr(0, line.find('\t'));
            if (!name.empty() && !models.count(name)) {
                std::string shown = name;
                std::replace(shown.begin(), shown.end(), '/', '\\');
                startup_issues.push_back(trf("Disparu depuis la sauvegarde du %s : modeles\\%s", when.c_str(), shown.c_str()));
                vanished = true;
            }
        }
    }
    // 2. LoRA cochées dans vos styles mais absentes du dossier des LoRA
    scan_loras(true);
    std::set<std::string> seen;
    for (const Preset & p : settings.presets)
        for (const LoraUse & u : p.loras)
            if (u.enabled && !lora_files.count(u.file) && seen.insert(u.file).second)
                startup_issues.push_back(trf("Le style « %s » utilise une LoRA absente : %s", p.name.c_str(), u.file.c_str()));
    // 3. Sauvegarde du jour (une seule par jour)
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t day[16];
    swprintf(day, 16, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    const fs::path dst = root / day;
    const bool     first_today = !fs::exists(dst, ec);
    if (first_today) {
        fs::create_directories(dst / L"config", ec);
        for (const auto & e : fs::directory_iterator(studio->paths().config, ec))
            if (e.is_regular_file(ec)) fs::copy_file(e.path(), dst / L"config" / e.path().filename(), fs::copy_options::overwrite_existing, ec);
        {
            std::ofstream out(dst / L"modeles.txt", std::ios::binary);
            for (const auto & [name, size] : models) out << name << '\t' << size << '\n';
        }
        fs::create_directories(dst / L"lora", ec);
        for (const auto & e : fs::directory_iterator(studio->paths().adapters, ec))
            if (e.is_regular_file(ec) && e.path().extension() == L".json") fs::copy_file(e.path(), dst / L"lora" / e.path().filename(), fs::copy_options::overwrite_existing, ec);
        const fs::path lib = studio->paths().library;
        for (fs::recursive_directory_iterator it(lib, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->path().filename() != L"meta.json" || !it->is_regular_file(ec)) continue;
            const fs::path to = dst / L"Audios" / it->path().lexically_relative(lib);
            fs::create_directories(to.parent_path(), ec);
            fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
        }
        olds.push_back(dst);
        // les 15 dernières sont gardées (dossiers de ce programme seulement, nommés AAAA-MM-JJ)
        while (olds.size() > 15) {
            if (olds.front().filename().wstring().size() == 10) fs::remove_all(olds.front(), ec);
            olds.erase(olds.begin());
        }
    }
    last_backup         = narrow(fs::path(day).wstring());
    backup_count        = (int) olds.size();
    startup_issues_open = !startup_issues.empty() && (first_today || vanished);  // LoRA d'un style absente : une fois par jour
}

void App::draw_startup_issues() {
    if (startup_issues_open) {
        ImGui::OpenPopup(tr("Vérification au démarrage"));
        startup_issues_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(720 * g_dpi, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(tr("Vérification au démarrage"), nullptr, ImGuiWindowFlags_NoResize)) return;
    muted_wrapped(tr("Des fichiers attendus sont introuvables. Vos réglages, vos styles, la liste des LoRA et la fiche de chaque morceau sont sauvegardés chaque jour dans le dossier « sauvegardes » du programme."));
    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    for (const std::string & s : startup_issues) colored_wrapped(P.amber, "• " + s);
    ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    if (soft_button(tr("Ouvrir le dossier des modèles"), P.violet)) ShellExecuteW(nullptr, L"open", studio->paths().models_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (soft_button(tr("Ouvrir les sauvegardes"), P.teal)) ShellExecuteW(nullptr, L"open", backups_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Coupe le style en lignes à la largeur du champ, uniquement après les virgules.
std::string App::wrap_style(const std::string & style, float width) {
    std::string out = style;
    float       x   = 0;
    size_t      start = 0;
    while (start < out.size()) {
        size_t comma = out.find(", ", start);
        size_t end   = comma == std::string::npos ? out.size() : comma + 1;
        float  w     = ImGui::CalcTextSize(out.c_str() + start, out.c_str() + end).x;
        if (x > 0 && x + w > width && start > 0 && out[start - 1] == ' ') {
            out[start - 1] = '\n';
            x              = 0;
        }
        x += w + ImGui::CalcTextSize(" ").x;
        if (comma == std::string::npos) break;
        start = comma + 2;
    }
    return out;
}

void App::notify(const std::string & text) {
    journal::event("message", text);
    toast       = text;
    toast_until = ImGui::GetTime() + 4.0;
}
