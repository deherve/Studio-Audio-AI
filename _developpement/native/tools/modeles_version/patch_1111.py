# 1.1.011 : patch de main.cpp (fins de ligne CRLF gardées). Chaque remplacement doit trouver son texte une seule fois.
import sys
p = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src\main.cpp'
s = open(p, encoding='utf-8', newline='').read()
NL = '\r\n'


def rep(a, b, count=1):
    global s
    a = a.replace('\n', NL)
    b = b.replace('\n', NL)
    n = s.count(a)
    if n != count:
        print('ERREUR', n, 'occurrence(s) au lieu de', count, ':', a[:90].replace(NL, '|'))
        sys.exit(1)
    s = s.replace(a, b)


# --- includes
rep('#include <set>\n', '#include <set>\n#include <deque>\n')
rep('#include "studio.hpp"\n', '#include "studio.hpp"\n#include "journal.hpp"\n')

# --- liste effective des balises (défaut - retirées + ajoutées)
rep('''static ImVec4 category_color(int index) {''', '''// 1.1.011 : balises réellement proposées (Réglages › Styles) : celles de STYLE_GROUPS sans les retirées, plus les ajoutées.
// Reconstruites au début d'une image (rebuild_style_tags) : les pointeurs restent valables pendant toute l'image.
static std::deque<std::string>      g_tag_store;
static std::vector<const char *>    g_eff_items[5];
static bool                         g_eff_on = false;
static const std::vector<const char *> & group_items(const ChipGroup & g) {
    return g_eff_on ? g_eff_items[std::clamp(g.color, 0, 4)] : g.items;
}

static ImVec4 category_color(int index) {''')
rep('for (const char * item : g.items) if (t == low(item)) return true;',
    'for (const char * item : group_items(g)) if (t == low(item)) return true;')

# --- réglages
rep('''    int                 style_mode = 0;         // bloc Style : 0 classique, 1 pastilles, 2 constructeur
''', '''    int                 style_mode = 0;         // bloc Style : 0 classique, 1 pastilles, 2 constructeur
    bool                save_group_style = false;  // 1.1.011 : enregistrer le style sous le nom du groupe à chaque morceau lancé
    std::vector<std::string> tag_added[5], tag_hidden[5];  // 1.1.011 : Réglages › Styles : balises ajoutées / retirées par catégorie
''')
rep('''    v = yyjson_obj_get(r, "style_mode");
    if (yyjson_is_int(v)) s.style_mode = std::clamp((int) yyjson_get_int(v), 0, 2);
''', '''    v = yyjson_obj_get(r, "style_mode");
    if (yyjson_is_int(v)) s.style_mode = std::clamp((int) yyjson_get_int(v), 0, 2);
    v = yyjson_obj_get(r, "save_group_style");
    if (yyjson_is_bool(v)) s.save_group_style = yyjson_get_bool(v);
    for (int gi = 0; gi < 5; gi++) {
        for (int kind = 0; kind < 2; kind++) {
            auto &      list = kind ? s.tag_hidden[gi] : s.tag_added[gi];
            std::string key  = std::string(kind ? "tags_hidden_" : "tags_added_") + std::to_string(gi);
            list.clear();
            size_t       ti, tm;
            yyjson_val * tv;
            yyjson_arr_foreach(yyjson_obj_get(r, key.c_str()), ti, tm, tv) {
                if (yyjson_is_str(tv)) list.push_back(yyjson_get_str(tv));
            }
        }
    }
''')
rep('''    yyjson_mut_obj_add_int(doc, o, "style_mode", s.style_mode);
''', '''    yyjson_mut_obj_add_int(doc, o, "style_mode", s.style_mode);
    yyjson_mut_obj_add_bool(doc, o, "save_group_style", s.save_group_style);
    for (int gi = 0; gi < 5; gi++) {
        for (int kind = 0; kind < 2; kind++) {
            const auto & list = kind ? s.tag_hidden[gi] : s.tag_added[gi];
            if (list.empty()) continue;
            std::string      key = std::string(kind ? "tags_hidden_" : "tags_added_") + std::to_string(gi);
            yyjson_mut_val * ta  = yyjson_mut_obj_add_arr(doc, o, key.c_str());
            for (const std::string & t : list) yyjson_mut_arr_add_strcpy(doc, ta, t.c_str());
        }
    }
''')
rep('''static void load_settings(Settings & s, const fs::path & path) {''', '''// 1.1.011 : recalcule les balises proposées d'après les réglages (par défaut, moins les retirées, plus les ajoutées).
static void rebuild_style_tags(const Settings & st) {
    g_tag_store.clear();
    for (int gi = 0; gi < 5; gi++) {
        g_eff_items[gi].clear();
        auto has = [](const std::vector<std::string> & v, const std::string & t) {
            for (const std::string & x : v) if (lower(x) == lower(t)) return true;
            return false;
        };
        for (const char * item : STYLE_GROUPS[gi].items) {
            if (!has(st.tag_hidden[gi], item)) g_eff_items[gi].push_back(item);
        }
        for (const std::string & t : st.tag_added[gi]) {
            bool dup = false;
            for (const char * e : g_eff_items[gi]) dup = dup || lower(e) == lower(t);
            if (dup) continue;
            g_tag_store.push_back(t);
            g_eff_items[gi].push_back(g_tag_store.back().c_str());
        }
    }
    g_eff_on = true;
}

static void load_settings(Settings & s, const fs::path & path) {''')

# --- sites d'appel
rep('style_chips(group.items,', 'style_chips(group_items(group),', 3)
rep('for (const char * item : g.items) if (lower(item) == lower(tag)) return &g;',
    'for (const char * item : group_items(g)) if (lower(item) == lower(tag)) return &g;')
rep('for (const char * item : group.items) {', 'for (const char * item : group_items(group)) {')

# --- case à cocher du formulaire (onglet Nouveau morceau)
rep('''        f.dirty |= input_line("##album", tr("Album"), f.album);
''', '''        f.dirty |= input_line("##album", tr("Album"), f.album);
        draw_group_style_checkbox();
''')
# --- case à cocher de l'onglet IA (album)
rep('''            // Pochettes (1.0.36) : créées après l'écriture des morceaux et leur mise en file
''', '''            draw_group_style_checkbox();
            // Pochettes (1.0.36) : créées après l'écriture des morceaux et leur mise en file
''')

# --- submit : style du groupe + journal
rep('''        if (trial_ids.size() > 1) trial_groups.push_back(trial_ids);  // 1.0.97 : meilleur essai
        const std::string quoted = trim(f.title);
''', '''        if (trial_ids.size() > 1) trial_groups.push_back(trial_ids);  // 1.0.97 : meilleur essai
        const std::string quoted = trim(f.title);
        journal::event("file", "Morceau ajouté à la file : « " + quoted + " » (" + (f.instrumental ? "instrumental" : "chanson") + ", groupe « " +
                                   trim(f.artist) + " », album « " + trim(f.album) + " », " + std::to_string(f.versions) + " version(s)" +
                                   (held ? ", en pause" : "") + ") style : " + style.substr(0, 200));
        if (settings.save_group_style && !trim(f.artist).empty() && !style.empty()) auto_group_style(trim(f.artist), style);
''')

# --- fonctions : style du groupe, Réglages › Styles
rep('''    void draw_ai_style_modal() {''', '''    // 1.1.011 : « Sauvegarder le style de ce groupe » : le style du morceau lancé est enregistré sous le nom du groupe,
    // seulement si aucun de vos styles ne porte déjà ce nom (un style existant n'est jamais remplacé en silence).
    void auto_group_style(const std::string & group, const std::string & style) {
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
    void draw_group_style_checkbox() {
        if (ImGui::Checkbox(tr("Sauvegarder le style de ce groupe"), &settings.save_group_style)) save_settings(settings, settings_path);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("À chaque morceau lancé, le style est enregistré dans vos styles sous le nom du groupe (s'il n'existe pas déjà), pour reproduire le même groupe plus tard."));
    }
    // Albums › « Style perso… » : style personnalisé au nom du groupe, avec les balises communes à au moins la moitié de ses morceaux.
    void open_group_style(const std::vector<Song> & all, const std::string & artist) {
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

    // ---- 1.1.011 : Réglages › Styles : ajouter / retirer des balises de Genres, Ambiance, Voix, Instruments, Son
    bool        tags_dirty = true;  // la liste effective est refaite au début de l'image
    std::string style_tag_new[5];
    static bool tag_in(const std::vector<std::string> & v, const std::string & t) {
        for (const std::string & x : v) if (lower(x) == lower(t)) return true;
        return false;
    }
    static void tag_erase(std::vector<std::string> & v, const std::string & t) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string & x) { return lower(x) == lower(t); }), v.end());
    }
    void style_tags_changed() {
        tags_dirty = true;
        save_settings(settings, settings_path);
    }
    bool style_tags_customized() const {
        for (int gi = 0; gi < 5; gi++) if (!settings.tag_added[gi].empty() || !settings.tag_hidden[gi].empty()) return true;
        return false;
    }
    // Une pastille de la liste : renvoie vrai au clic.
    bool style_tag_chip(const std::string & text, const ImVec4 & color, float avail, float & x, bool first, int id) {
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
    void draw_style_tags_settings() {
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
                    if (style_tag_chip(std::string(custom ? "• " : "") + items[i] + "  ✕", color, avail, x, i == 0, (int) i)) remove = items[i];
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

    void draw_ai_style_modal() {''')

# --- images : balises refaites en début d'image
rep('''    void frame() {
        if (theme_pending) {''', '''    void frame() {
        if (tags_dirty) {  // 1.1.011 : balises de style (Réglages › Styles)
            tags_dirty = false;
            rebuild_style_tags(settings);
        }
        if (theme_pending) {''')

# --- Réglages : catégorie Styles
rep('N_("IA (paroles)") };\n    static constexpr int SET_CAT_COUNT = 7;', 'N_("IA (paroles)"), N_("Styles") };\n    static constexpr int SET_CAT_COUNT = 8;')
rep('''                                 { N_("IA (paroles)"), &P.amber } };''', '''                                 { N_("IA (paroles)"), &P.amber }, { N_("Styles"), &P.green } };''')
rep('''        if (cat == 6) draw_ai_settings();
''', '''        if (cat == 6) draw_ai_settings();
        if (cat == 7) draw_style_tags_settings();
''')

# --- Réglages › Système : ouvrir le journal
rep('''        draw_update_settings();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.blue, tr("Recommandations pour ce PC"));''', '''        draw_update_settings();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.blue, "%s", tr("Journal de débogage"));
        ImGui::PopFont();
        muted_wrapped(tr("Le Studio note ce qui se passe (démarrage, clics, travaux lancés, erreurs et ce qui les a précédées) : à joindre à un message en cas de problème."));
        if (soft_button(tr("Ouvrir le journal"), P.blue)) {
            std::error_code jec;
            if (fs::exists(journal::path(), jec)) ShellExecuteW(nullptr, L"open", journal::path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.blue, tr("Recommandations pour ce PC"));''')

# --- Albums : bouton « Style perso… » et fenêtre dans la fenêtre Albums
rep('''            ImGui::TextColored(P.blue, "%s", a.artist.empty() ? tr("(sans groupe)") : a.artist.c_str());
            ImGui::TextColored(P.muted, tr("%d terminé(s) · %d validé(s)"), a.done, a.validated);''', '''            ImGui::TextColored(P.blue, "%s", a.artist.empty() ? tr("(sans groupe)") : a.artist.c_str());
            if (!a.artist.empty()) {  // 1.1.011 : style personnalisé au nom du groupe
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Style perso…"))) open_group_style(all, a.artist);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tr("Crée un style personnalisé au nom du groupe, avec les balises communes à ses morceaux."));
            }
            ImGui::TextColored(P.muted, tr("%d terminé(s) · %d validé(s)"), a.done, a.validated);''')
rep('''        draw_rename_album_modal();  // 1.0.41''', '''        draw_rename_album_modal();  // 1.0.41
        if (!page) draw_ai_style_modal();  // 1.1.011 : « Style perso… » d'un groupe, dans la fenêtre Albums (en page : dessinée plus bas)''')

# --- menu de gauche : point lumineux
rep('''        auto entry = [&](const char * label, bool on) {
            ImGui::PushStyleColor(ImGuiCol_Header, P.accent_soft);
            const bool clicked = ImGui::Selectable(label, on, 0, item);
            ImGui::PopStyleColor();''', '''        // 1.1.011 : point lumineux à droite de l'entrée quand un travail correspondant est en cours
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
                dl->AddCircleFilled(c, 8 * g_dpi, ImGui::GetColorU32(alpha(P.ok, 0.20f * pulse)));
                dl->AddCircleFilled(c, 5 * g_dpi, ImGui::GetColorU32(alpha(P.ok, 0.45f)));
                dl->AddCircleFilled(c, 3.2f * g_dpi, ImGui::GetColorU32(P.ok));
            }''')
rep('''if (entry(tr("Composer avec l'IA"), b_page == 0 && form_shown == 1)) go_form(1);
        if (entry(tr("Partition"), b_page == 0 && form_shown == 2)) go_form(2);''', '''if (entry(tr("Composer avec l'IA"), b_page == 0 && form_shown == 1, ai_busy)) go_form(1);
        if (entry(tr("Partition"), b_page == 0 && form_shown == 2, score_busy)) go_form(2);''')
rep('''if (entry(lib.c_str(), b_page == 1 && lib_tab == 0)) b_page = 1''', '''if (entry(lib.c_str(), b_page == 1 && lib_tab == 0, running + waiting > 0 || jobs_busy)) b_page = 1''')
rep('''if (entry(tr("Albums"), b_page == 1 && lib_tab == 1)) b_page = 1''', '''if (entry(tr("Albums"), b_page == 1 && lib_tab == 1, covers_busy || jobs_busy)) b_page = 1''')
rep('''if (entry(tr("Réglages"), b_page == 2)) open_settings = true;''', '''if (entry(tr("Réglages"), b_page == 2, dl_busy)) open_settings = true;''')
rep('''if (entry(tr("Aide"), b_page == 3)) open_help();''', '''if (entry(tr("Aide"), b_page == 3, help_busy)) open_help();''')

# --- travaux terminés / en échec : journal
rep('''                j->announced = true;
                notify(j->name + (j->state == 1 ? tr(" : terminé.") : tr(" : échec.")));''', '''                j->announced = true;
                {
                    std::lock_guard<std::mutex> lock(j->mutex);
                    if (j->state == 1) journal::event("travail", j->name + " : terminé.");
                    else journal::error("travail", j->name + " : échec" + (j->message.empty() ? "" : " (" + j->message + ")"));
                }
                notify(j->name + (j->state == 1 ? tr(" : terminé.") : tr(" : échec.")));''')

# --- notifications dans le journal
rep('''    void notify(const std::string & text) {
        toast       = text;''', '''    void notify(const std::string & text) {
        journal::event("message", text);
        toast       = text;''')

# --- démarrage, images, arrêt
rep('''    if (!ensure_models(instance, fs::path(exe).parent_path(), paths)) return 0;
''', '''    if (!ensure_models(instance, fs::path(exe).parent_path(), paths)) return 0;
    journal::init(paths.root / "journal", STUDIO_VERSION);  // 1.1.011
''')
rep('''    ImGui::CreateContext();
    ImGuiIO & io    = ImGui::GetIO();''', '''    ImGui::CreateContext();
    journal::imgui_install();
    ImGuiIO & io    = ImGui::GetIO();''')
rep('''        app.frame();
        ImGui::Render();''', '''        app.frame();
        journal::imgui_frame_end();
        ImGui::Render();''')
rep('''    ImGui_ImplDX11_Shutdown();''', '''    journal::event("appli", "Arrêt du Studio.");
    ImGui_ImplDX11_Shutdown();''')
rep('''    load_settings(app.settings, app.settings_path);
    app.load_last_album();''', '''    load_settings(app.settings, app.settings_path);
    journal::event("appli", "Réglages chargés : disposition " + std::to_string(app.settings.layout) + ", thème " + std::to_string(app.settings.theme) +
                                ", langue « " + app.settings.ui_language + " »");
    app.load_last_album();''')

open(p, 'w', encoding='utf-8', newline='').write(s)
print('OK')
