// Réglages › IA, bibliothèque, cartes (fonctions de App ; déclarées dans app.hpp).
#include "app.hpp"

// ---- Réglages › IA
void App::start_ai_model_download(const AiModelFile & m) {
    std::error_code ec;
    fs::create_directories(ai_models_dir(), ec);
    auto job = downloader.start(std::string("ia:") + m.id, m.name, ai_models_dir(), { { ai_model_url(m), m.file, m.bytes } });
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(trf("Téléchargement du modèle IA « %s » lancé.", m.name));
}

void App::start_llama_download() {
    const LlamaPackage & p = llama_vulkan_package();
    std::error_code      ec;
    fs::create_directories(llama_dir(), ec);
    auto job = downloader.start("llama_vulkan", "llama.cpp (Vulkan)", llama_dir(), { { p.url, p.file, p.bytes } });
    if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
    announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
    notify(tr("Téléchargement de llama.cpp pour la carte graphique lancé."));
}

// Fin d'un téléchargement de l'IA (appelé par update_downloads). Renvoie vrai si la clé lui appartenait.
bool App::finish_ai_download(DownloadJob & j) {
    if (j.key == "llama_vulkan") {
        if (j.state == 1) {
            *llama_extract_state = 1;
            auto     state = llama_extract_state;
            auto     err   = llama_extract_error;
            fs::path zip = llama_dir() / llama_vulkan_package().file, dir = llama_dir();
            std::thread([state, err, zip, dir] {
                std::string e;
                bool        ok = llama_extract(zip, dir, e);
                *err           = e;
                *state         = ok ? 2 : 3;
            }).detach();
        } else if (j.state == 2) {
            notify(trf("Téléchargement de « %s » impossible : %s", j.label.c_str(), j.error.c_str()));
        }
        return true;
    }
    if (j.key.rfind("ia:", 0) == 0) {
        if (j.state == 1) {
            notify(trf("Modèle IA « %s » prêt : choisissez-le dans Réglages › IA.", j.label.c_str()));
            // Premier modèle installé : il devient le modèle utilisé.
            if (settings.ai_local_model.empty() || !fs::exists(ai_models_dir() / widen(settings.ai_local_model))) {
                for (const AiModelFile & m : ai_model_catalog()) {
                    if (std::string("ia:") + m.id == j.key) settings.ai_local_model = m.file;
                }
                save_settings(settings, settings_path);
            }
        } else if (j.state == 2) {
            notify(trf("Téléchargement de « %s » impossible : %s", j.label.c_str(), j.error.c_str()));
        }
        return true;
    }
    return false;
}

void App::poll_llama_extract() {
    int s = *llama_extract_state;
    if (s < 2) return;
    *llama_extract_state = 0;
    refresh_local_llm(true);
    if (s == 2) notify(tr("llama.cpp pour la carte graphique est prêt : l'IA locale utilisera la carte graphique au repos."));
    else notify(trf("Installation de llama.cpp impossible : %s", tr(llama_extract_error->c_str())));
}

// Choix du moteur : bouton radio d'une ligne.
void App::ai_engine_radio(const char * label, const std::string & id) {
    if (ImGui::RadioButton(label, settings.ai_engine == id)) {
        settings.ai_engine = id;
        save_settings(settings, settings_path);
    }
}

// Bouton « Télécharger llama.cpp pour la carte graphique » (avec sa barre de progression), dans Réglages › IA et,
// depuis 1.0.28, dans l'onglet « Composer avec l'IA » quand il manque. Le téléchargement ne part qu'au clic.
void App::draw_llama_download() {
    auto dl = downloader.find("llama_vulkan");
    if (dl && dl->state == 0) {
        unsigned long long d = dl->done, t = std::max<unsigned long long>(1, dl->total);
        progress_bar((float) ((double) d / (double) t), ImVec2(160 * g_dpi, 0));
        ImGui::SameLine();
        if (ImGui::SmallButton("✕##llama")) dl->cancel = true;
    } else if (*llama_extract_state == 1) {
        ImGui::TextColored(P.muted, "%s", tr("Décompression de llama.cpp…"));
    } else {
        if (ImGui::Button(trf("Télécharger llama.cpp pour la carte graphique (%s) ↓", fmt_mb(llama_vulkan_package().bytes).c_str()).c_str())) start_llama_download();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", trf("Version %s de llama.cpp (projet libre, licence MIT) avec Vulkan : l'IA locale écrit 5 à 10 fois plus vite sur la carte graphique et peut utiliser de plus gros modèles. Installée dans le dossier ia\\llama.", llama_vulkan_package().tag).c_str());
        }
    }
}

void App::draw_ai_settings() {
    draw_ai_settings_inner();
}

void App::draw_ai_settings_inner() {
    refresh_local_llm();
    poll_llama_extract();
    const bool is_online = settings.ai_engine != "local" && settings.ai_engine != "server";

    begin_card(P.amber, "c_ai_engine");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.amber, "%s", tr("Moteur d'IA pour « Composer avec l'IA »"));
    ImGui::PopFont();
    muted_wrapped(tr("L'IA locale tourne sur ce PC, gratuitement et sans Internet, mais seulement au repos (aucune génération de morceau en cours). Un serveur local (Ollama, LM Studio…) fonctionne de la même façon. Un service en ligne écrit mieux et plus vite, avec votre clé d'API (payant à l'usage, quelques centimes au plus par morceau)."));
    ai_engine_radio(tr("IA locale intégrée (llama.cpp)"), "local");
    ImGui::SameLine();
    ai_engine_radio(tr("Serveur local"), "server");
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Service en ligne"), is_online)) {
        if (!is_online) {
            settings.ai_engine = settings.ai_last_service.empty() ? "openai" : settings.ai_last_service;
            save_settings(settings, settings_path);
        }
    }
    end_card();

    // Mots à éviter (1.0.33) : pour des morceaux qui ne se ressemblent pas
    begin_card(P.warn, "c_ai_avoid");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.warn, "%s", tr("Mots à éviter"));
    ImGui::PopFont();
    muted_wrapped(tr("Mots que l'IA ne doit employer ni dans les titres ni dans les paroles, séparés par des virgules. Si un morceau en contient quand même, le Studio redemande une fois."));
    {
        // Retour à la ligne automatique (1.0.34) : ImGui 1.91 ne coupe pas les lignes d'un champ multiligne ; le texte
        // est donc remis en forme à chaque frappe (wrap_word_list), une ligne ne dépassant jamais la largeur du champ.
        static std::string edit;
        static bool        editing = false;
        static float       wrap_w  = 0;
        wrap_w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ScrollbarSize - 4 * g_dpi;
        if (!editing) {
            std::string joined;
            for (const std::string & w : settings.ai_avoid) joined += (joined.empty() ? "" : ", ") + w;
            edit = wrap_word_list(joined, wrap_w, nullptr);
        }
        auto rewrap = [](ImGuiInputTextCallbackData * d) -> int {
            std::string text(d->Buf, d->BufTextLen);
            int         cursor = d->CursorPos;
            std::string out    = wrap_word_list(text, *(float *) d->UserData, &cursor);
            if (out != text) {
                d->DeleteChars(0, d->BufTextLen);
                d->InsertChars(0, out.c_str());
                d->CursorPos      = std::clamp(cursor, 0, d->BufTextLen);
                d->SelectionStart = d->SelectionEnd = d->CursorPos;
            }
            return 0;
        };
        float lines = std::clamp((float) std::count(edit.begin(), edit.end(), '\n') + 1.6f, 3.6f, 9.0f);
        tx::InputTextMultiline("##ai_avoid", &edit, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * lines),
                                  ImGuiInputTextFlags_CallbackEdit, rewrap, &wrap_w);
        editing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings.ai_avoid.clear();
            std::set<std::string> seen;
            size_t start = 0;
            while (start <= edit.size()) {
                size_t      end = edit.find_first_of(",;\n", start);
                std::string w   = trim(edit.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (!w.empty() && seen.insert(fold(w)).second) settings.ai_avoid.push_back(w);
                if (end == std::string::npos) break;
                start = end + 1;
            }
            save_settings(settings, settings_path);
        }
    }
    if (ImGui::SmallButton(tr("Supprimer les doublons"))) {
        int removed = 0;
        settings.ai_avoid = ai_dedupe_words(settings.ai_avoid, removed);
        if (removed > 0) save_settings(settings, settings_path);
        notify(removed > 0 ? trf("%d doublon(s) retiré(s) de la liste des mots à éviter.", removed) : std::string(tr("Aucun doublon dans la liste des mots à éviter.")));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Retire les mots en double, sans tenir compte des accents, des majuscules ni du pluriel (« Rêve » et « rêves » comptent pour un) ; le premier est gardé."));
    ImGui::SameLine();
    if (ImGui::SmallButton((std::string(tr("Rétablir la liste par défaut")) + "##avoid").c_str())) {
        settings.ai_avoid = ai_default_avoid_words();
        save_settings(settings, settings_path);
    }
    if (ImGui::Checkbox(tr("Paroles plus naturelles : consignes anti-style « IA » (humanizer)"), &settings.ai_natural)) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Ajoute aux demandes de paroles des règles d'écriture naturelle dans la langue choisie : images concrètes, pas de « ce n'est pas X, c'est Y », pas de formules toutes faites ni de calques de l'anglais (règles supplémentaires pour le français). Inspiré des skills humanizer et humanizer-fr (MIT)."));
    if (ImGui::Checkbox(tr("Relire les paroles : l'IA corrige les lignes qui sonnent « IA »"), &settings.ai_polish)) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Après avoir écrit les paroles, l'IA les relit et ne réécrit que les lignes toutes faites ou traduites de l'anglais. Les sections, le nombre de lignes et les refrains restent identiques. Compter environ une minute de plus par chanson avec l'IA locale (quelques secondes en ligne). Décochée par défaut : avec une petite IA locale, l'effet reste modeste."));
    if (ImGui::Checkbox(tr("Éviter aussi les mots qui reviennent dans les derniers morceaux écrits par l'IA"), &settings.ai_auto_avoid)) save_settings(settings, settings_path);
    if (settings.ai_auto_avoid) {
        std::vector<std::string> autow = ai_auto_words();
        std::string              list;
        for (const std::string & w : autow) list += (list.empty() ? "" : ", ") + w;
        ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
        ImGui::TextWrapped("%s", autow.empty() ? trf("Aucun pour l'instant (%d morceau(x) retenu(s)).", (int) settings.ai_recent.size()).c_str()
                                               : trf("Mots évités en ce moment : %s", list.c_str()).c_str());
        ImGui::PopStyleColor();
        if (!settings.ai_recent.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Oublier les derniers morceaux"))) {
                settings.ai_recent.clear();
                save_settings(settings, settings_path);
            }
        }
    }
    end_card();

    // Albums (1.0.35) : nombre maximal d'instrumentaux d'un album « Mélangé »
    begin_card(P.green, "c_ai_album");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.green, "%s", tr("Albums"));
    ImGui::PopFont();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Instrumentaux max par album"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120 * g_dpi);
    if (ImGui::InputInt("##ai_max_inst", &settings.ai_max_instrumentals)) {
        settings.ai_max_instrumentals = std::clamp(settings.ai_max_instrumentals, 0, 50);
        save_settings(settings, settings_path);
    }
    muted_wrapped(tr("Type « Mélangé » de l'onglet Composer avec l'IA : l'album n'aura jamais plus d'instrumentaux que ce nombre (2 par défaut ; 0 = aucun)."));
    end_card();

    // Pochettes (1.0.36) : service d'images, modèle, modèle local à télécharger
    begin_card(P.pink, "c_ai_images");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.pink, "%s", tr("Pochettes (images d'album)"));
    ImGui::PopFont();
    muted_wrapped(tr("Images créées pour les albums : en local sur la carte graphique (gratuit, au repos seulement), ou par un service en ligne avec la clé d'API saisie plus haut pour ce service. La description de chaque image se modifie dans la fenêtre « Albums… »."));
    {
        const ImageService * cur = find_image_service(settings.ai_image_engine);
        ImGui::SetNextItemWidth(320 * g_dpi);
        if (ImGui::BeginCombo("##img_service", cur ? tr(cur->name) : "?")) {
            for (const ImageService & s : image_services()) {
                if (ImGui::Selectable(tr(s.name), cur == &s)) {
                    settings.ai_image_engine = s.id;
                    save_settings(settings, settings_path);
                }
            }
            ImGui::EndCombo();
        }
        if (cur && std::string(cur->id) != "local") {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(160 * g_dpi, ImGui::GetContentRegionAvail().x));
            std::string & model = settings.ai_image_models[cur->id];
            if (tx::InputTextWithHint("##img_model", cur->model, &model)) save_settings(settings, settings_path);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Modèle d'images du service (vide = celui proposé)."));
        }
        std::string why, label;
        bool        ready = image_engine_ready(why, label);
        colored_wrapped(ready ? P.ok : P.amber, ready ? tr("● Prêt") : why);
        if (cur && std::string(cur->id) == "local") {
            // 1.0.50 : trois modèles au choix, chacun téléchargé au clic seulement dans son propre dossier
            muted_wrapped(tr("Modèle utilisé (mesures sur une Radeon RX 6700 XT 12 Go, image carrée 768 × 768) :"));
            for (const LocalImageModel & m : local_image_models()) {
                ImGui::PushID(m.id);
                bool        ready = image_model_ready(image_models_dir(), m);
                std::string key   = std::string("images-") + m.id;
                auto        job   = downloader.find(key);
                if (ImGui::RadioButton(m.name, settings.ai_image_local == m.id)) {
                    settings.ai_image_local = m.id;
                    save_settings(settings, settings_path);
                }
                ImGui::SameLine();
                ImGui::TextColored(P.muted, "%s", trf("≈ %d s par image · %.1f Go de mémoire vidéo · %s à télécharger", m.seconds, m.vram_gb,
                                                     fmt_mb(local_image_bytes(m)).c_str()).c_str());
                ImGui::Indent(24 * g_dpi);
                if (std::string(m.id) == "klein") muted_wrapped(tr("Très beau rendu, texte bien écrit dans l'image, mais environ trois fois plus lent que l'ancien FLUX.2 klein. Licence FLUX Kommunity (voir le tableau des licences dans Réglages › Système)."));
                else if (std::string(m.id) == "zimage") muted_wrapped(tr("Rendu proche de la photo ; il faut environ 9 Go de mémoire vidéo."));
                else if (std::string(m.id) == "iris") muted_wrapped(tr("Le plus rapide des modèles qui écrivent bien le texte dans l'image (titre, nom du groupe) : environ 14 s par image. Licence Apache 2.0."));
                else if (std::string(m.id) == "qwenturbo") muted_wrapped(tr("Très bon rendu et texte bien écrit dans l'image (accents parfois approximatifs), environ 1 minute par image, 8,5 Go de mémoire vidéo. Licence Qwen : usage commercial à vérifier (tableau des licences dans Réglages › Système)."));
                else if (std::string(m.id) == "qwen21") muted_wrapped(tr("Version complète de Qwen-Image 2.1 : rendu sobre et soigné, mais environ 3 fois plus lent que la version Turbo (2 min 40 par image). Même licence Qwen que la version Turbo."));
                else if (std::string(m.id) == "sdxl") muted_wrapped(tr("Très rapide (≈ 17 s en 1344 × 768), beau rendu, mais suit moins fidèlement la description. Licence OpenRAIL++."));
                else {
                    muted_wrapped(tr("Rendu le plus soigné, surtout pour le texte (titre, nom du groupe), mais environ 3 minutes par image."));
                    if (image_offload(m))
                        colored_wrapped(P.amber, tr("Sur cette carte (12 Go ou moins), ses poids restent en mémoire vive : environ 3 min par image, 5 Go de mémoire vidéo. Les grandes images (1024) sont trop lentes : les pochettes restent en 768."));
                }
                if (ready) ImGui::TextColored(P.ok, "%s", tr("● Installé"));
                else if (job && job->state == 0) {
                    float frac = job->total ? (float) job->done / (float) job->total : 0.f;
                    progress_bar(frac, ImVec2(-FLT_MIN, 0), trf("Téléchargement… %s / %s", fmt_mb(job->done).c_str(), fmt_mb(job->total).c_str()).c_str());
                } else {
                    if (soft_button(trf("Télécharger %s (%s)", m.name, fmt_mb(local_image_bytes(m)).c_str()).c_str(), P.pink)) {
                        std::vector<DlFile> files;
                        for (const ImageModelFile & mf : m.files) files.push_back({ image_model_url(mf), mf.file, mf.bytes });
                        std::error_code ec;
                        fs::create_directories(local_image_dir(image_models_dir(), m), ec);
                        auto j = downloader.start(key, m.name, local_image_dir(image_models_dir(), m), files);
                        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), j) == tracked_downloads.end()) tracked_downloads.push_back(j);
                        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), j.get()), announced_downloads.end());
                        notify(tr("Téléchargement du modèle d'images lancé."));
                    }
                    if (job && job->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", job->error.c_str()));
                }
                ImGui::Unindent(24 * g_dpi);
                ImGui::PopID();
            }
            path_line(image_models_dir());
            muted_wrapped(tr("Modèles sous licence Apache 2.0 (Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS ; Iris-3B : Sperid Labs). FLUX.3 klein (Black Forest Labs) : FLUX Kommunity License ; Qwen-Image 2.1 : licence Qwen, voir le tableau des licences."));
        }
    }
    end_card();

    // IA locale intégrée
    begin_card(P.violet, "c_ai_local");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.violet, "%s", tr("IA locale intégrée"));
    ImGui::PopFont();
    {
        const GpuInfo & g  = gpu_profile().gpu;
        const double    GB = 1024.0 * 1024 * 1024;
        colored_wrapped(P.muted, trf("Ce PC : %s (%.0f Go de mémoire vidéo), %.0f Go de mémoire vive.", g.found ? g.name.c_str() : "?", g.vram / GB,
                                              sysinfo.ram_total / GB).c_str());
    }
    if (!local_llm.ready()) {
        ImGui::TextColored(P.warn, "%s", tr("llama.cpp est introuvable."));
    } else {
        ImGui::TextColored(P.ok, "%s", local_llm.vulkan ? tr("● llama.cpp prêt : carte graphique (Vulkan) ou processeur.") : tr("● llama.cpp prêt : processeur seulement."));
    }
    if (!local_llm.vulkan) draw_llama_download();
    else muted_wrapped(tr("L'IA locale travaille sur la carte graphique, au repos seulement : le modèle y est chargé le temps d'écrire, puis libéré. Un modèle plus gros que la mémoire vidéo est partagé avec le processeur (plus lent)."));
    muted_wrapped(tr("Modèles téléchargeables depuis Hugging Face. La colonne « Sur ce PC » indique où chaque modèle peut tourner d'après votre carte graphique et votre mémoire. Cochez « Utiliser » pour choisir le modèle."));
    ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    std::vector<std::string> files = ai_local_files();
    int                      extra = 0;
    for (const std::string & f : files) {
        bool known = false;
        for (const AiModelFile & m : ai_model_catalog()) known |= f == m.file;
        extra += !known;
    }
    float rows = (float) std::min<size_t>(ai_model_catalog().size() + extra, 8) + 1.4f;
    if (ImGui::BeginTable("##ai_models", 6, tf, ImVec2(0, rows * ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("Utiliser"), ImGuiTableColumnFlags_WidthFixed, 60 * g_dpi);
        ImGui::TableSetupColumn(tr("Modèle"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 70 * g_dpi);
        ImGui::TableSetupColumn(tr("Sur ce PC"), ImGuiTableColumnFlags_WidthFixed, 150 * g_dpi);
        ImGui::TableSetupColumn(tr("Téléchargement"), ImGuiTableColumnFlags_WidthFixed, 150 * g_dpi);
        ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
        ImGui::TableHeadersRow();
        auto fit_cell = [&](unsigned long long bytes) {
            AiFit f = ai_fit(bytes);
            if (f == FIT_GPU) ImGui::TextColored(P.ok, "%s", tr("Carte graphique · rapide"));
            else if (f == FIT_MIXED) ImGui::TextColored(P.amber, "%s", tr("Carte + processeur · moyen"));
            else if (f == FIT_CPU) ImGui::TextColored(P.amber, "%s", tr("Processeur · lent"));
            else ImGui::TextColored(P.danger, "%s", tr("Trop lourd"));
        };
        for (const AiModelFile & m : ai_model_catalog()) {
            ImGui::PushID(m.id);
            ImGui::TableNextRow();
            bool installed = ai_model_installed(m);
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginDisabled(!installed);
            if (ImGui::RadioButton("##use", settings.ai_local_model == m.file)) {
                settings.ai_local_model = m.file;
                settings.ai_engine      = "local";
                save_settings(settings, settings_path);
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(1);
            std::string stars;
            for (int i = 0; i < 5; i++) stars += i < m.quality ? "★" : "☆";
            ImGui::TextUnformatted(m.name);
            ImGui::SameLine();
            ImGui::TextColored(P.amber, "%s", stars.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Qualité d'écriture attendue (paroles, langues autres que l'anglais)."));
            ImGui::SameLine();
            if (ImGui::SmallButton("↗")) ShellExecuteW(nullptr, L"open", widen(std::string("https://huggingface.co/") + m.repo).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ouvrir la page du modèle sur Hugging Face (licence, détails)"));
            ImGui::TextColored(P.muted, "%s", tr(m.note));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(fmt_mb(m.bytes).c_str());
            ImGui::TableSetColumnIndex(3);
            fit_cell(m.bytes);
            ImGui::TableSetColumnIndex(4);
            auto job = downloader.find(std::string("ia:") + m.id);
            if (job && job->state == 0) {
                unsigned long long d = job->done, t = std::max<unsigned long long>(1, job->total);
                progress_bar((float) ((double) d / (double) t), ImVec2(90 * g_dpi, 0));
                ImGui::SameLine();
                if (ImGui::SmallButton("✕")) job->cancel = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Annuler le téléchargement"));
            } else if (installed) {
                ImGui::TextColored(P.ok, "%s", tr("● installé"));
            } else {
                bool failed = job && job->state == 2;
                if (ImGui::SmallButton(failed ? tr("Réessayer ↓") : tr("Télécharger ↓"))) start_ai_model_download(m);
                if (failed && ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Échec : %s"), job->error.c_str());
            }
            ImGui::TableSetColumnIndex(5);
            if (installed && soft_button(tr("Supprimer"), P.danger)) pending_ai_delete = m.file;
            ImGui::PopID();
        }
        // Modèles GGUF déposés à la main dans models\ia
        for (const std::string & f : files) {
            bool known = false;
            for (const AiModelFile & m : ai_model_catalog()) known |= f == m.file;
            if (known) continue;
            ImGui::PushID(f.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::RadioButton("##use", settings.ai_local_model == f)) {
                settings.ai_local_model = f;
                settings.ai_engine      = "local";
                save_settings(settings, settings_path);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(f.c_str());
            ImGui::TextColored(P.muted, "%s", tr("Modèle déposé à la main dans modeles\\ia."));
            std::error_code    ec;
            unsigned long long size = fs::file_size(ai_models_dir() / widen(f), ec);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(fmt_mb(size).c_str());
            ImGui::TableSetColumnIndex(3);
            fit_cell(size);
            ImGui::TableSetColumnIndex(4);
            ImGui::TextColored(P.ok, "%s", tr("● installé"));
            ImGui::TableSetColumnIndex(5);
            if (soft_button(tr("Supprimer"), P.danger)) pending_ai_delete = f;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton(tr("Ouvrir le dossier des modèles IA"))) {
        std::error_code ec;
        fs::create_directories(ai_models_dir(), ec);
        ShellExecuteW(nullptr, L"open", ai_models_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    end_card();

    // Serveur local
    begin_card(P.teal, "c_ai_server");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.teal, "%s", tr("Serveur local"));
    ImGui::PopFont();
    muted_wrapped(tr("Ollama, LM Studio, llama-server, KoboldCpp, Jan… : s'il tourne sur ce PC, « Détecter » le trouve sur son port habituel."));
    bool scanning = ai_scan && ai_scan->state == 0;
    ImGui::BeginDisabled(scanning);
    if (ImGui::Button(scanning ? tr("Recherche…") : tr("Détecter"))) {
        auto scan = std::make_shared<AiServerScan>();
        ai_scan   = scan;
        std::thread([scan] {
            scan->servers = ai_detect_servers();
            scan->state   = 1;
        }).detach();
    }
    ImGui::EndDisabled();
    if (ai_scan && ai_scan->state == 1) {
        if (ai_scan->servers.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", tr("Aucun serveur local trouvé."));
        }
        for (const AiServer & s : ai_scan->servers) {
            ImGui::PushID(s.url.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.ok, "● %s", s.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s · %s", s.url.c_str(), trf("%d modèle(s)", (int) s.models.size()).c_str());
            ImGui::SameLine();
            bool connected = settings.ai_engine == "server" && settings.ai_server_url == s.url;
            if (connected) ImGui::TextColored(P.ok, "%s", tr("connecté"));
            else if (soft_button(tr("Se connecter"), P.teal)) {
                settings.ai_engine      = "server";
                settings.ai_server_url  = s.url;
                settings.ai_server_name = s.name;
                if (std::find(s.models.begin(), s.models.end(), settings.ai_server_model) == s.models.end())
                    settings.ai_server_model = s.models.empty() ? "" : s.models.front();
                save_settings(settings, settings_path);
            }
            ImGui::PopID();
        }
    }
    if (!settings.ai_server_url.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Adresse"));
        ImGui::SameLine(110 * g_dpi);
        ImGui::SetNextItemWidth(320 * g_dpi);
        if (tx::InputText("##ai_srv_url", &settings.ai_server_url)) save_settings(settings, settings_path);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Modèle"));
        ImGui::SameLine(110 * g_dpi);
        draw_ai_model_picker("server", settings.ai_server_model);
    }
    end_card();

    // Services en ligne
    begin_card(P.blue, "c_ai_online");
    ImGui::PushFont(g_bold);
    ImGui::TextColored(P.blue, "%s", tr("Services en ligne (clé d'API)"));
    ImGui::PopFont();
    muted_wrapped(tr("Créez une clé d'API sur le site du service (bouton ↗), collez-la ici puis « Enregistrer ». Elle est chiffrée par Windows pour votre seul compte, jamais écrite en clair. La demande (langue, influence, thème) est envoyée au service choisi."));
    std::string cur = is_online ? settings.ai_engine : (settings.ai_last_service.empty() ? "openai" : settings.ai_last_service);
    const AiService * svc = find_ai_service(cur);
    if (!svc) svc = &ai_services().front();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Service"));
    ImGui::SameLine(110 * g_dpi);
    ImGui::SetNextItemWidth(320 * g_dpi);
    if (ImGui::BeginCombo("##ai_service", tr(svc->name), ImGuiComboFlags_HeightLarge)) {
        for (const AiService & s : ai_services()) {
            std::string item = std::string(tr(s.name)) + (settings.ai_keys[s.id].empty() ? "" : "  ●");
            if (ImGui::Selectable(item.c_str(), &s == svc)) {
                settings.ai_engine       = s.id;
                settings.ai_last_service = s.id;
                ai_key_edit.clear();
                save_settings(settings, settings_path);
            }
        }
        ImGui::EndCombo();
    }
    if (svc->key_page[0]) {
        ImGui::SameLine();
        if (ImGui::SmallButton("↗##key")) ShellExecuteW(nullptr, L"open", widen(svc->key_page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ouvrir la page du service où créer une clé d'API"));
    }
    if (std::string(svc->id) == "custom") {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Adresse"));
        ImGui::SameLine(110 * g_dpi);
        ImGui::SetNextItemWidth(320 * g_dpi);
        if (tx::InputTextWithHint("##ai_custom_url", "https://…/v1", &settings.ai_custom_url)) save_settings(settings, settings_path);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Clé d'API"));
    ImGui::SameLine(110 * g_dpi);
    bool has_key = !settings.ai_keys[svc->id].empty();
    ImGui::SetNextItemWidth(320 * g_dpi);
    tx::InputTextWithHint("##ai_key", has_key ? tr("clé enregistrée (chiffrée) : collez-en une autre pour la remplacer") : tr("collez la clé ici"), &ai_key_edit,
                             ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::BeginDisabled(trim(ai_key_edit).empty());
    if (soft_button(tr("Enregistrer"), P.blue)) {
        std::string stored = ai_protect_key(trim(ai_key_edit));
        if (stored.empty()) notify(tr("Chiffrement de la clé impossible."));
        else {
            settings.ai_keys[svc->id] = stored;
            settings.ai_engine        = svc->id;
            settings.ai_last_service  = svc->id;
            save_settings(settings, settings_path);
            notify(trf("Clé de %s enregistrée (chiffrée).", tr(svc->name)));
        }
        SecureZeroMemory(ai_key_edit.data(), ai_key_edit.size());
        ai_key_edit.clear();
    }
    ImGui::EndDisabled();
    if (has_key) {
        ImGui::SameLine();
        if (soft_button(tr("Effacer la clé"), P.danger)) {
            settings.ai_keys.erase(svc->id);
            save_settings(settings, settings_path);
        }
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.muted, "%s", tr("Modèle"));
    ImGui::SameLine(110 * g_dpi);
    std::string model = ai_service_model(svc->id);
    if (draw_ai_model_picker(svc->id, model)) {
        settings.ai_models[svc->id] = model;
        save_settings(settings, settings_path);
    }
    // Essai : une toute petite demande pour vérifier la clé et le modèle.
    bool testing = ai_test && ai_test->state == 0;
    ImGui::BeginDisabled(testing || (!has_key && std::string(svc->id) != "custom"));
    if (ImGui::Button(testing ? tr("Essai…") : tr("Essayer"))) {
        auto       test = std::make_shared<AiTest>();
        ai_test         = test;
        AiEndpoint ep   = ai_endpoint(svc->id);
        std::thread([test, ep] {
            test->result = ai_chat(ep, "You are a test.", "Reply with the single word OK.", 60000);
            test->state  = 1;
        }).detach();
    }
    ImGui::EndDisabled();
    if (ai_test && ai_test->state == 1) {
        ImGui::SameLine();
        if (ai_test->result.ok) ImGui::TextColored(P.ok, "%s", tr("● Le service répond : tout est prêt."));
        else colored_wrapped(P.danger, trf(tr(ai_test->result.error.c_str()), ai_test->result.error_arg.c_str()));
    }
    end_card();

    // Confirmation de suppression d'un modèle IA local
    if (!pending_ai_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer le modèle IA ?"))) ImGui::OpenPopup(tr("Supprimer le modèle IA ?"));
    ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
    if (ImGui::BeginPopupModal(tr("Supprimer le modèle IA ?"), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped(tr("Le fichier « %s » sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau."), pending_ai_delete.c_str());
        path_line(ai_models_dir() / widen(pending_ai_delete));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) {
            pending_ai_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool busy = ai_local_busy();
        ImGui::BeginDisabled(busy);
        if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
            std::error_code ec;
            remove_inside(ai_models_dir() / widen(pending_ai_delete), ec);
            if (!ec && settings.ai_local_model == pending_ai_delete) {
                settings.ai_local_model.clear();
                save_settings(settings, settings_path);
            }
            if (!remove_refused)
                notify(ec ? trf("Suppression impossible : %s", ec.message().c_str()) : trf("Modèle IA « %s » supprimé.", pending_ai_delete.c_str()));
            pending_ai_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

// Champ du modèle d'un service ou d'un serveur : saisie libre + liste obtenue auprès du service.
// Renvoie vrai si le modèle a changé.
bool App::draw_ai_model_picker(const std::string & target, std::string & model) {
    bool changed = false;
    ImGui::PushID(target.c_str());
    ImGui::SetNextItemWidth(320 * g_dpi);
    if (tx::InputText("##model", &model)) changed = true;
    ImGui::SameLine();
    bool loading = ai_model_list && ai_model_list->state == 0 && ai_model_list->target == target;
    ImGui::BeginDisabled(loading);
    if (ImGui::SmallButton(loading ? tr("Liste…") : tr("Lister les modèles"))) {
        auto list    = std::make_shared<AiModelList>();
        list->target = target;
        ai_model_list = list;
        AiEndpoint ep = ai_endpoint(target);
        std::thread([list, ep] {
            list->models = ai_list_models(ep, list->error);
            list->state  = 1;
        }).detach();
        ImGui::OpenPopup("models_pop");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("models_pop")) {
        if (!ai_model_list || ai_model_list->state == 0) ImGui::TextColored(P.muted, "%s", tr("Interrogation du service…"));
        else if (!ai_model_list->error.empty()) colored_wrapped(P.danger, ai_model_list->error.c_str());
        else if (ai_model_list->models.empty()) ImGui::TextColored(P.muted, "%s", tr("Aucun modèle annoncé par le service."));
        else {
            ImGui::BeginChild("##models_scroll", ImVec2(380 * g_dpi, std::min(320.f, 24.f * ai_model_list->models.size() + 8) * g_dpi));
            for (const std::string & m : ai_model_list->models) {
                if (ImGui::Selectable(m.c_str(), m == model)) {
                    model   = m;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
    if (target == "server" && changed) {
        settings.ai_server_model = model;
        save_settings(settings, settings_path);
    }
    ImGui::PopID();
    return changed;
}

// ------------------------------------------------------------------ Bibliothèque
void App::draw_player(const Song & s) {
    // Lecture depuis le WAV (sortie brute du moteur, sans perte) : ce qu'on entend ici est
    // exactement ce que le modèle a produit, sans l'écrêtage ou le grésillement qu'un
    // ré-encodage MP3 peut introduire sur les passages riches en aigus (distorsion, cymbales).
    fs::path master  = playable_file(s.id);
    bool     loaded  = player.id() == s.id;
    bool     playing = loaded && player.playing();
    if (accent_button(playing ? tr("⏸ Pause") : tr("▶ Lecture"), ImVec2(96 * g_dpi, 0), &P.green)) {
        if (!loaded && !player.open(s.id, master.wstring())) {
            notify(tr("Lecture impossible : ") + narrow(master.wstring()));
        } else if (playing) {
            player.pause();
        } else {
            player.set_volume(settings.volume);
            player.play();
            mark_heard(s);
        }
    }
    ImGui::SameLine();
    int         length  = loaded ? player.length() : (int) (s.audio_seconds * 1000);
    int         pos     = drag_id == s.id ? drag_ms : (loaded ? player.position() : 0);
    std::string clock   = fmt_clock(pos) + " / " + fmt_clock(length);
    float       clock_w = ImGui::CalcTextSize("00:00 / 00:00").x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clock_w - ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, P.green);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, P.green);
    ImGui::SliderInt("##pos", &pos, 0, std::max(length, 1), "", ImGuiSliderFlags_NoInput);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemActive()) {
        drag_id = s.id;
        drag_ms = pos;
    }
    if (ImGui::IsItemDeactivated() && drag_id == s.id) {
        if (!loaded) player.open(s.id, master.wstring());
        player.seek(drag_ms);
        drag_id.clear();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(clock.c_str());
}

ImVec4 App::status_color(const Song & s) {
    if (s.status == "running") return P.accent;
    if (s.status == "queued") return s.held ? P.amber : P.blue;
    if (s.status == "done") return is_new(s) ? P.pink : P.ok;  // rose : en attente d'écoute et de vérification
    if (s.status == "interrupted") return P.orange;
    return P.danger;
}

const char * App::status_label(const Song & s) {
    if (s.status == "running") return tr("En cours");
    if (s.status == "queued") return s.held ? tr("En pause") : tr("En attente");
    if (s.status == "done") return tr("Terminée");
    if (s.status == "cancelled") return tr("Annulée");
    if (s.status == "interrupted") return tr("Interrompue");
    return tr("Erreur");
}

// Statistiques d'un morceau (durée, temps, graine, modèle, solveur, LoRA) ; message d'erreur éventuel.
void App::draw_song_stats(const Song & s, const ImVec4 & color) {
    double took = (s.finished > 0 ? s.finished : now_seconds()) - s.started;
    bool   first_stat = true;
    float  stat_limit = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    auto   stat = [&](const char * name, const std::string & value) {
        float w = ImGui::CalcTextSize(name).x + 5 * g_dpi + ImGui::CalcTextSize(value.c_str()).x;
        // les statistiques passent à la ligne quand la largeur manque
        if (!first_stat && ImGui::GetItemRectMax().x + 18 * g_dpi + w <= stat_limit) ImGui::SameLine(0, 18 * g_dpi);
        first_stat = false;
        ImGui::TextColored(P.muted, "%s", name);
        ImGui::SameLine(0, 5 * g_dpi);
        ImGui::TextUnformatted(value.c_str());
    };
    // Valeur que l'on peut sélectionner : clic = tout sélectionner, Ctrl+C ou clic droit = copier (graine à recoller dans le formulaire).
    auto stat_copyable = [&](const char * name, const std::string & value, const std::string & id) {
        float w = ImGui::CalcTextSize(name).x + 5 * g_dpi + ImGui::CalcTextSize(value.c_str()).x + 8 * g_dpi;
        if (!first_stat && ImGui::GetItemRectMax().x + 18 * g_dpi + w <= stat_limit) ImGui::SameLine(0, 18 * g_dpi);
        first_stat = false;
        ImGui::TextColored(P.muted, "%s", name);
        ImGui::SameLine(0, 5 * g_dpi);
        std::string buf = value;
        ImGui::PushID(id.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, alpha(P.accent, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, alpha(P.accent, 0.18f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * g_dpi, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(value.c_str()).x + 6 * g_dpi);
        ImGui::InputText("##copyable", buf.data(), buf.size() + 1, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Cliquer pour sélectionner ; Ctrl+C ou clic droit pour copier la graine (à coller dans le champ « Graine »)."));
        if (ImGui::BeginPopupContextItem("##copy_menu")) {
            if (ImGui::MenuItem(tr("Copier la graine"), "Ctrl+C")) ImGui::SetClipboardText(value.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    if (s.status == "done") {
        stat(tr("Durée"), fmt_time(s.audio_seconds));
        if (s.started > 0 && s.finished > 0) stat(tr("Temps de génération"), fmt_time(took));
        if (s.has_used_seeds) stat_copyable(tr("Graine"), std::to_string(s.used_lm_seed), "seed_" + s.id);
        stat(tr("Modèle"), model_label(s.model));
        if (!s.solver.empty()) stat(tr("Solveur"), solver_label(s.solver));
        {  // 1.0.87 : partition écrite par le modèle
            const ScoreInfo & sc = score_info(s);
            if (!sc.key.empty() || sc.tempo > 0)
                stat(tr("Partition"), key_name(sc.key) + (sc.tempo > 0 ? " · " + std::to_string(sc.tempo) + " BPM" : std::string()));
        }
        if (!s.adapters.empty()) {
            std::string names;
            for (const auto & [file, scale] : s.adapters) {
                char sc[16];
                snprintf(sc, sizeof(sc), "%.2g", scale);
                for (char & c : sc) if (c == '.') c = ',';
                names += (names.empty() ? "" : ", ") + lora_label(file) + " ×" + sc;
            }
            stat(tr("LoRA"), names);
        }
    } else if (!s.active() && s.started > 0 && s.finished > 0) {
        stat(tr("Arrêtée après"), fmt_time(took));
    }
    if (!s.message.empty() && !s.active() && s.status != "done") {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", s.message.c_str());
        ImGui::PopStyleColor();
    }
}

void App::draw_song(const Song & s, int queue_pos, bool collapsible) {
    ImGui::PushID(s.id.c_str());
    ImVec4 color = status_color(s);
    bool fresh = is_new(s);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.card, color, s.active() ? 0.05f : (fresh ? 0.13f : 0.0f)));
    ImGui::PushStyleColor(ImGuiCol_Border, (s.active() || fresh) ? alpha(color, fresh ? 0.65f : 0.45f) : P.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * g_dpi, 12 * g_dpi));
    ImGui::BeginChild("card", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    album_stripe(s);  // 1.0.37

    // Titre, étiquettes et statut
    float        line_x = ImGui::GetCursorPosX(), avail = ImGui::GetContentRegionAvail().x;
    const char * status = status_label(s);
    float        status_w = pill_width(status);
    float        thumb_sz = 26 * g_dpi;
    float        thumb_w  = s.status == "done" ? marks_width(settings.mark_style, thumb_sz) : 0;  // marques, une fois terminé
    float        gap      = thumb_w > 0 ? 8 * g_dpi : 0;
    float        title_y  = ImGui::GetCursorPosY();
    float        logos_w  = clip_logos_width(s);
    float        logos_g  = logos_w > 0 ? logos_w + 10 * g_dpi : 0;
    ImGui::PushFont(g_bold);
    ImGui::PushTextWrapPos(line_x + avail - status_w - thumb_w - gap - logos_g - 16 * g_dpi);
    ImGui::TextWrapped("%s", s.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (collapsible) {  // un clic sur le titre replie la carte
        ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + (avail - status_w - thumb_w - gap - logos_g - 16 * g_dpi), ImGui::GetItemRectMax().y);
        if (title_zone(a, b, tr("Cliquer pour replier"))) expanded_cards.erase(s.id);
    }
    float after_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - gap - logos_g, title_y));
    draw_clip_logos(s);
    ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - gap, title_y));
    pill(status, color);
    if (thumb_w > 0) {
        ImGui::SetCursorPos(ImVec2(line_x + avail - thumb_w, title_y - 3 * g_dpi));
        apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
    }
    ImGui::SetCursorPos(ImVec2(line_x, after_y));

    bool tags = false;
    auto tag  = [&](const char * text, const ImVec4 & c) {
        if (tags) ImGui::SameLine(0, 5 * g_dpi);
        pill(text, c);
        tags = true;
    };
    if (fresh) tag(tr("Nouveau — à écouter et vérifier"), P.pink);
    if (s.instrumental) tag(tr("Instrumental"), P.teal);
    if (s.instrumental && !s.heard_vocals_lang.empty()) {  // Whisper entend des paroles dans ce morceau marqué instrumental
        int         li   = language_index_for_code(s.heard_vocals_lang);
        std::string name = li > 0 ? std::string(tr(LANGUAGES[li].label)) : s.heard_vocals_lang;
        tag(trf("Voix détectée : %s", name.c_str()).c_str(), P.amber);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ce morceau est marqué instrumental, mais Whisper y entend des paroles à l'écoute : vérifiez-le."));
    }
    if (!s.instrumental && !s.lyrics_lang.empty()) {  // langue des paroles (déclarée dans le style ou détectée)
        int li = language_index_for_code(s.lyrics_lang);
        if (li > 0) tag(trf("Paroles : %s", tr(LANGUAGES[li].label)).c_str(), P.blue);
        if (li > 0 && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", s.lyrics_lang_check == 2 ? tr("Langue des paroles : vérifiée à l'écoute du morceau (Whisper).")
                                    : s.lyrics_lang_declared ? tr("Langue des paroles : déclarée par la balise de langue du style.") : tr("Langue des paroles : détectée d'après le texte."));
        }
    }
    if (s.kind == "rerender") tag(tr("Re-rendu"), P.violet);
    if (s.kind == "extend") tag(s.redo_from > 0 ? tr("Partie refaite") : tr("Suite"), P.violet);
    if (s.kind == "cover") tag(tr("Reprise"), P.pink);
    if (s.truncated) tag(tr("Fondu de fin"), P.amber);
    if (!s.album.empty()) {  // 1.0.37 : badge de l'album (clic = filtre)
        if (tags) ImGui::SameLine(0, 5 * g_dpi);
        album_badge(s);
        tags = true;
    }
    if (!s.artist.empty() || !s.album.empty()) {
        if (tags) ImGui::SameLine(0, 10 * g_dpi);
        std::string who = s.artist + (!s.artist.empty() && !s.album.empty() ? "  —  " : "") + s.album;
        ImGui::TextColored(P.blue, "%s", who.c_str());
        tags = true;
    }
    if (s.kind != "new" && !s.parent_title.empty()) {
        muted_wrapped(s.kind == "rerender" ? trf("Re-rendu de « %s »", s.parent_title.c_str())
                      : s.kind == "extend" && s.redo_from > 0 ? trf("Refait à partir de %d:%02d de « %s »", (int) s.redo_from / 60, (int) s.redo_from % 60, s.parent_title.c_str())
                      : s.kind == "extend" ? trf("Suite de « %s »", s.parent_title.c_str())
                                           : trf("Reprise de « %s »", s.parent_title.c_str()));
    }
    muted_wrapped(s.style);

    double took = (s.finished > 0 ? s.finished : now_seconds()) - s.started;
    draw_song_stats(s, color);

    // Actions selon l'état
    if (s.status == "running") {
        std::string overlay = s.stage;
        if (s.started > 0) {
            overlay += "  —  " + fmt_time(took);
            double left = s.eta - (now_seconds() - s.eta_at);
            if (s.eta > 0) overlay += left > 1 ? trf("  ·  ≈ %s restantes", fmt_time(left).c_str()) : std::string(tr("  ·  presque fini"));
        }
        if (!s.local) overlay += tr("  (version web)");
        progress_bar((float) s.progress, ImVec2(-FLT_MIN, 0), overlay.c_str());
        if (s.local) {
            if (soft_button(tr("⏸ Mettre en pause"), P.amber)) studio->set_held(s.id, true);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(tr("Arrête la génération et libère la carte graphique.\nElle reprendra depuis le début une fois relancée."));
            }
            ImGui::SameLine();
        }
        if (danger_button(tr("Annuler"))) studio->cancel(s.id);
    } else if (s.status == "queued") {
        if (s.held) {
            if (soft_button(tr("▶ Lancer"), P.green)) studio->set_held(s.id, false);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Remet le morceau dans la file : il partira à son tour."));
        } else {
            if (queue_pos > 0) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(P.muted, tr("Position %d dans la file"), queue_pos);
                ImGui::SameLine();
            }
            if (soft_button(tr("⏸ Pause"), P.amber)) studio->set_held(s.id, true);
        }
        ImGui::SameLine();
        if (danger_button(tr("Annuler"))) studio->cancel(s.id);
    } else if (s.status == "done" && settings.layout >= 1) {
        draw_song_actions_c(s);  // 1.1.002 : disposition A, carte rangée
    } else if (s.status == "done") {
        draw_player(s);
        Flow         flow;
        const char * formats[] = { "flac", "wav", "mp3" };
        const char * labels[]  = { tr("↓ FLAC"), tr("↓ WAV"), tr("↓ MP3") };
        for (int i = 0; i < 3; i++) {
            flow.next(labels[i]);
            if (soft_button(labels[i], P.green)) {
                std::wstring target = save_dialog(hwnd, s, formats[i]);
                if (!target.empty()) {
                    fs::path src = song_folder(s.id) / (std::string("audio.") + formats[i]);
                    notify(CopyFileW(src.c_str(), target.c_str(), FALSE) ? tr("Exporté : ") + narrow(target)
                                                                         : tr("Échec de l'export vers ") + narrow(target));
                }
            }
        }
        flow.next(tr("Dossier"));
        if (ImGui::SmallButton(tr("Dossier"))) {
            std::wstring arg = L"/select,\"" + (song_folder(s.id) / "audio.flac").wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        flow.next(tr("Vidéo"));
        if (soft_button(tr("Vidéo"), P.teal)) {
            std::string key = s.album.empty() ? std::string() : album_key(s.artist, s.album);
            open_video_dialog({ s }, s.title, key);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Crée une vidéo MP4 pour YouTube (16:9) et/ou TikTok (9:16) : une image fixe + le morceau."));
        }
        // 1.0.55 : clip animé d'un seul morceau (essai, ou pour revoir un clip en particulier)
        flow.next(tr("Clip"));
        if (soft_button(tr("Clip"), P.teal)) {
            std::string key = s.album.empty() ? std::string() : album_key(s.artist, s.album);
            open_video_dialog({ s }, s.title, key, true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Clip animé de ce morceau seul : scénario d'après les paroles, images par scène, paroles en sous-titres."));
        }
        flow.next(tr("Modifier"));
        bool edit_clicked = soft_button(tr("Modifier"), P.blue);
        if (edit_clicked) {
            edit_id     = s.id;
            edit_title  = s.title;
            edit_artist = s.artist;
            edit_album  = s.album;
            edit_track  = s.track;
            open_edit   = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Titre, groupe ou chanteur, album et numéro de piste."));
        bool has_codes = cached_exists(song_folder(s.id) / "audio.json");
        // 1.0.47 (b1, validé par Herve) : « Recomposer » devient « Nouvelle version (mêmes paroles) », « Réutiliser »
        // devient « Modifier et renvoyer… » ; « Refaire le son » (ex « Re-rendu ») passe dans le menu « Actions ▾ ».
        flow.next(tr("Nouvelle version (mêmes paroles)"));
        ImGui::BeginDisabled(s.kind == "extend");
        if (soft_button(tr("Nouvelle version (mêmes paroles)"), P.pink)) recompose(s);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(s.kind == "extend"
                                  ? tr("Une suite dépend de son morceau d'origine : faites plutôt une nouvelle version de celui-ci.")
                                  : tr("Nouvelle composition avec le même style et les mêmes paroles.\nÀ utiliser si le morceau grésille : « Refaire le son » garde la même composition."));
        }
        flow.next(tr("Prolonger"));
        ImGui::BeginDisabled(!has_codes || s.audio_seconds >= 340);
        if (soft_button(tr("Prolonger"), P.violet)) {
            extend_id      = s.id;
            extend_title   = s.title + tr(" (suite)");
            extend_seconds = 60;
            extend_lyrics  = s.instrumental ? "[verse]\n[chorus]\n[outro]\n" : "[Verse]\n\n\n[Chorus]\n\n\n[Outro]\n";
            open_extend    = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(s.audio_seconds >= 340 ? tr("Le morceau atteint déjà la durée maximale (6 min).")
                                                     : tr("Crée un nouveau morceau qui reprend celui-ci et le prolonge."));
        }
        flow.next(tr("Modifier et renvoyer…"));
        if (ImGui::SmallButton(tr("Modifier et renvoyer…"))) reuse(s);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Recopie tous les réglages de ce morceau dans « Nouveau morceau » pour les modifier avant de l'envoyer."));
        flow.next(tr("Paroles"));
        lyrics_button(s);  // 1.0.95
        flow.next(tr("Actions ▾"));
        if (ImGui::SmallButton(tr("Actions ▾"))) ImGui::OpenPopup("##actions");
        draw_song_menu(s);
        flow.next(tr("Style perso…"));
        ImGui::BeginDisabled(trim(s.style).empty());
        if (ImGui::SmallButton(tr("Style perso…"))) open_song_style_save(s);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Enregistre le style de ce morceau (avec ses LoRA et son style à éviter) dans vos styles."));
        flow.next(tr("Supprimer"));
        if (danger_button(tr("Supprimer"))) pending_delete = s.id;
    } else {
        if (s.status == "interrupted" || s.status == "error" || s.status == "cancelled") {
            if (soft_button(tr("↻ Relancer"), P.green)) studio->relaunch(s.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Remet ce morceau dans la file, avec les mêmes réglages."));
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(tr("Modifier et renvoyer…"))) reuse(s);
        ImGui::SameLine();
        if (danger_button(tr("Supprimer"))) pending_delete = s.id;
    }

    // Liseré de couleur à gauche de la carte
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(5 * g_dpi, ws.y), ImGui::GetColorU32(color),
                                              ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

  // cartes dépliées en affichage compact
std::string App::ellipsize(const std::string & text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string cut = text;
    while (!cut.empty() && ImGui::CalcTextSize((cut + "…").c_str()).x > width) {
        do cut.pop_back(); while (!cut.empty() && (cut.back() & 0xC0) == 0x80);  // ne coupe pas un caractère UTF-8
    }
    return cut + "…";
}

// Lecture / pause d'un morceau (bouton des affichages compact et pochette).
void App::toggle_play(const Song & s) {
    fs::path master  = playable_file(s.id);
    bool     loaded  = player.id() == s.id;
    bool     playing = loaded && player.playing();
    if (!loaded && !player.open(s.id, master.wstring())) {
        notify(tr("Lecture impossible : ") + narrow(master.wstring()));
    } else if (playing) {
        player.pause();
    } else {
        player.set_volume(settings.volume);
        player.play();
        mark_heard(s);
    }
}

void App::export_song(const Song & s, const char * format) {
    std::wstring target = save_dialog(hwnd, s, format);
    if (target.empty()) return;
    fs::path src = song_folder(s.id) / (std::string("audio.") + format);
    notify(CopyFileW(src.c_str(), target.c_str(), FALSE) ? tr("Exporté : ") + narrow(target) : tr("Échec de l'export vers ") + narrow(target));
}

// Menu « Actions » d'un morceau terminé.
void App::draw_song_menu(const Song & s) {
    if (ImGui::BeginPopup("##actions")) {
        if (ImGui::Selectable(tr("↓ Exporter en FLAC"))) export_song(s, "flac");
        if (ImGui::Selectable(tr("↓ Exporter en WAV"))) export_song(s, "wav");
        if (ImGui::Selectable(tr("↓ Exporter en MP3"))) export_song(s, "mp3");
        if (ImGui::Selectable(tr("Ouvrir le dossier"))) {
            std::wstring arg = L"/select,\"" + (song_folder(s.id) / "audio.flac").wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr("Vidéo…"))) {
            open_video_dialog({ s }, s.title, s.album.empty() ? std::string() : album_key(s.artist, s.album));
        }
        if (ImGui::Selectable(tr("Clip animé…"))) {
            open_video_dialog({ s }, s.title, s.album.empty() ? std::string() : album_key(s.artist, s.album), true);
        }
        ImGui::Separator();
        draw_tools_menu({ s }, display_title(s.title), false);  // 1.0.97
        ImGui::Separator();
        if (ImGui::Selectable(tr("Modifier le titre, le groupe, l'album…"))) {
            edit_id     = s.id;
            edit_title  = s.title;
            edit_artist = s.artist;
            edit_album  = s.album;
            edit_track  = s.track;
            open_edit   = true;
        }
        bool has_codes = cached_exists(song_folder(s.id) / "audio.json");
        if (ImGui::Selectable(tr("Refaire le son (même composition)…"), false, has_codes ? 0 : ImGuiSelectableFlags_Disabled)) {
            rerender_id   = s.id;
            open_rerender = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Même composition, nouvelle synthèse audio (timbre et mixage différents)."));
        if (ImGui::Selectable(tr("Refaire à partir d'ici…"), false, has_codes ? 0 : ImGuiSelectableFlags_Disabled)) ask_redo(s);  // 1.1.006
        {  // 1.0.87
            const bool has_score = score_info(s).present;
            if (ImGui::Selectable(tr("Voir la partition…"), false, has_score ? 0 : ImGuiSelectableFlags_Disabled)) {
                score_view_id   = s.id;
                open_score_view = true;
            }
            if (!has_score && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Morceau créé sans partition (mode « Sans partition » ou avant la version 1.0.87)."));
            if (ImGui::Selectable(tr("Utiliser cette partition…"), false, has_score ? 0 : ImGuiSelectableFlags_Disabled)) use_song_score(s);
            if (!has_score) {  // 1.0.89 : partition reconnue par SheetSage2 dans le son du morceau
                fs::path audio = playable_file(s.id);
                const bool ok  = !audio.empty() && studio->can_transcribe() && !plan_busy() && !studio->local_active() && !ai_local_busy();
                if (ImGui::Selectable(tr("Reconnaître sa partition…"), false, ok ? 0 : ImGuiSelectableFlags_Disabled))
                    start_recognize(audio, display_title(s.title));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("SheetSage2 écoute le morceau et écrit sa partition dans l'onglet « Partition » (≈ 1 min pour 3 min) : pour en faire un nouveau morceau avec d'autres paroles ou un autre style."));
            }
            if (has_score && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Reprend le morceau et sa partition dans l'onglet « Partition » : même mélodie et mêmes accords, à transposer ou à rejouer dans un autre style."));
        }
        if (ImGui::Selectable(tr("Nouvelle version (mêmes paroles)"), false, s.kind == "extend" ? ImGuiSelectableFlags_Disabled : 0)) recompose(s);
        if (ImGui::Selectable(tr("Prolonger…"), false, (!has_codes || s.audio_seconds >= 340) ? ImGuiSelectableFlags_Disabled : 0)) {
            extend_id      = s.id;
            extend_title   = s.title + tr(" (suite)");
            extend_seconds = 60;
            extend_lyrics  = s.instrumental ? "[verse]\n[chorus]\n[outro]\n" : "[Verse]\n\n\n[Chorus]\n\n\n[Outro]\n";
            open_extend    = true;
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr("Modifier et renvoyer…"))) reuse(s);
        if (ImGui::Selectable(tr("Enregistrer comme style personnalisé…"), false, trim(s.style).empty() ? ImGuiSelectableFlags_Disabled : 0))
            open_song_style_save(s);
        ImGui::PushStyleColor(ImGuiCol_Text, P.danger);
        if (ImGui::Selectable(tr("Supprimer…"))) pending_delete = s.id;
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
}

// 1.1.002 : carte rangée de la disposition A (proposition C de la revue 1.0.98) : lecture, puis 4 boutons et 3 menus.
// Chaque action des 14 boutons et du menu « Actions ▾ » de la carte actuelle se retrouve une seule fois.
void App::draw_song_actions_c(const Song & s) {
    draw_player(s);
    const bool  has_codes = cached_exists(song_folder(s.id) / "audio.json");
    const std::string key = s.album.empty() ? std::string() : album_key(s.artist, s.album);
    lyrics_button(s);
    ImGui::SameLine();
    if (soft_button(tr("Télécharger ▾"), P.green)) ImGui::OpenPopup("##c_dl");
    ImGui::SameLine();
    if (soft_button(tr("Refaire ▾"), P.violet)) ImGui::OpenPopup("##c_redo");
    ImGui::SameLine();
    if (soft_button(tr("Vidéos ▾"), P.teal)) ImGui::OpenPopup("##c_video");
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Plus ▾"))) ImGui::OpenPopup("##c_more");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Outils audio, partition, infos du morceau, suppression."));
    if (ImGui::BeginPopup("##c_dl")) {
        if (ImGui::Selectable(tr("↓ Exporter en FLAC"))) export_song(s, "flac");
        if (ImGui::Selectable(tr("↓ Exporter en WAV"))) export_song(s, "wav");
        if (ImGui::Selectable(tr("↓ Exporter en MP3"))) export_song(s, "mp3");
        if (ImGui::Selectable(tr("Ouvrir le dossier"))) {
            std::wstring arg = L"/select,\"" + (song_folder(s.id) / "audio.flac").wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##c_redo")) {
        if (ImGui::Selectable(tr("Nouvelle version (mêmes paroles)"), false, s.kind == "extend" ? ImGuiSelectableFlags_Disabled : 0)) recompose(s);
        if (ImGui::Selectable(tr("Prolonger…"), false, (!has_codes || s.audio_seconds >= 340) ? ImGuiSelectableFlags_Disabled : 0)) {
            extend_id      = s.id;
            extend_title   = s.title + tr(" (suite)");
            extend_seconds = 60;
            extend_lyrics  = s.instrumental ? "[verse]\n[chorus]\n[outro]\n" : "[Verse]\n\n\n[Chorus]\n\n\n[Outro]\n";
            open_extend    = true;
        }
        if (ImGui::Selectable(tr("Modifier et renvoyer…"))) reuse(s);
        if (ImGui::Selectable(tr("Refaire le son (même composition)…"), false, has_codes ? 0 : ImGuiSelectableFlags_Disabled)) {
            rerender_id   = s.id;
            open_rerender = true;
        }
        if (ImGui::Selectable(tr("Refaire à partir d'ici…"), false, has_codes ? 0 : ImGuiSelectableFlags_Disabled)) ask_redo(s);  // 1.1.006
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Garde le début du morceau et recompose tout ce qui suit la section choisie (nouveau morceau)."));
        if (ImGui::Selectable(tr("Enregistrer comme style personnalisé…"), false, trim(s.style).empty() ? ImGuiSelectableFlags_Disabled : 0))
            open_song_style_save(s);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##c_video")) {
        if (ImGui::Selectable(tr("Vidéo…"))) open_video_dialog({ s }, s.title, key);
        if (ImGui::Selectable(tr("Clip animé…"))) open_video_dialog({ s }, s.title, key, true);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##c_more")) {
        draw_tools_menu({ s }, display_title(s.title), false);  // mastering, paroles synchronisées, pistes, fiches…
        ImGui::Separator();
        const bool has_score = score_info(s).present;
        if (ImGui::Selectable(tr("Voir la partition…"), false, has_score ? 0 : ImGuiSelectableFlags_Disabled)) {
            score_view_id   = s.id;
            open_score_view = true;
        }
        if (ImGui::Selectable(tr("Utiliser cette partition…"), false, has_score ? 0 : ImGuiSelectableFlags_Disabled)) use_song_score(s);
        if (!has_score) {
            fs::path   audio = playable_file(s.id);
            const bool ok    = !audio.empty() && studio->can_transcribe() && !plan_busy() && !studio->local_active() && !ai_local_busy();
            if (ImGui::Selectable(tr("Reconnaître sa partition…"), false, ok ? 0 : ImGuiSelectableFlags_Disabled)) start_recognize(audio, display_title(s.title));
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr("Modifier le titre, le groupe, l'album…"))) {
            edit_id     = s.id;
            edit_title  = s.title;
            edit_artist = s.artist;
            edit_album  = s.album;
            edit_track  = s.track;
            open_edit   = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, P.danger);
        if (ImGui::Selectable(tr("Supprimer…"))) pending_delete = s.id;
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
}

std::string App::song_summary(const Song & s, bool with_lang) const {
    std::string out = fmt_time(s.audio_seconds) + "  ·  " + model_label(s.model);
    if (!s.solver.empty()) out += std::string("  ·  ") + solver_label(s.solver);
    if (with_lang && s.instrumental && !s.heard_vocals_lang.empty()) {
        int li = language_index_for_code(s.heard_vocals_lang);
        out += "  ·  " + trf("Voix détectée : %s", li > 0 ? tr(LANGUAGES[li].label) : s.heard_vocals_lang.c_str());
    }
    if (with_lang && !s.instrumental && !s.lyrics_lang.empty()) {
        int li = language_index_for_code(s.lyrics_lang);
        if (li > 0) out += "  ·  " + trf("Paroles : %s", tr(LANGUAGES[li].label));
    }
    if (!s.adapters.empty()) out += tr("  ·  LoRA ×") + std::to_string(s.adapters.size());
    // 1.0.93 : la tonalité de la partition n'est plus ajoutée ici (elle raccourcissait le titre des cartes compactes) ;
    // elle reste sur la carte détaillée et dans « Voir la partition… »
    return out;
}
