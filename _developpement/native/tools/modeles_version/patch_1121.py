# 1.1.021 : passe de relecture des paroles écrites par l'IA (réglage ai_polish). Usage : python -X utf8 patch_1121.py
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'


def patch(name, pairs):
    p = SRC + '\\' + name
    raw = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:70])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


# ---------------------------------------------------------------- ai_writer.hpp
patch('ai_writer.hpp', [(
    'constexpr int AI_LONG_LINES = 110;',
    '''// 1.1.021 : passe de relecture : l'IA ne réécrit que les lignes qui sonnent « IA » (images toutes faites, abstractions, calques de
// l'anglais). Le résultat n'est gardé que si les sections et le nombre de lignes de chaque section sont identiques ; un refrain répété
// reste identique partout. Rend le nombre de lignes changées (0 = paroles inchangées).
int ai_polish_song(AiSong & song, const std::string & language, const std::vector<std::string> & avoid, const AiAsk & ask,
                   const std::atomic<bool> * cancel);
constexpr int AI_LONG_LINES = 110;''')])

# ---------------------------------------------------------------- ai_writer.cpp
patch('ai_writer.cpp', [(
    'std::string ai_avoid_retry_prompt(const std::vector<std::string> & found) {',
    '''int ai_polish_song(AiSong & song, const std::string & language, const std::vector<std::string> & avoid, const AiAsk & ask,
                   const std::atomic<bool> * cancel) {
    if (!song.ok || ai_sung_lines(song.lyrics) < 4 || (cancel && *cancel)) return 0;
    const std::string lang = language.empty() ? std::string("English") : language;
    std::string       u    = "Here are the lyrics of a song, in " + lang + ":\\n\\n" + song.lyrics +
                      "\\n\\nEdit these lyrics. Rewrite ONLY the lines that sound generic or machine-written: stock images (shadows dancing, "
                      "whispers in the wind, echoes, tapestries, a symphony of...), abstract or grand words, \\"it is not X, it is Y\\" "
                      "constructions, lists of three empty words, and phrases translated word for word from English. Replace each of them with a "
                      "concrete, specific and natural line that a native songwriter would write. Keep every other line exactly as it is. "
                      "Keep the same [Section] tags, the same number of lines in each section, the same rhyme scheme and about the same "
                      "number of syllables per line, so that the song stays singable. A line that is repeated (a chorus) must stay identical "
                      "every time. Do not add or remove any line or section. Stay in " + lang + ".";
    if (!avoid.empty()) {
        u += " Never use these words: ";
        for (size_t i = 0; i < avoid.size(); i++) u += (i ? ", " : "") + avoid[i];
        u += ".";
    }
    if (g_natural) u += natural_rules(lang);
    u += "\\n\\nReply with the full lyrics only, as plain text with their [Section] tags: no JSON, no comment, no explanation.";
    AiCallResult r = ask("You are an experienced songwriter and lyrics editor. You only answer with plain text.", u);
    if (!r.ok) return 0;
    int         removed = 0;
    std::string fresh   = ai_clean_lyrics(r.text, false, removed);
    if (fresh.empty() || fresh.find('{') != std::string::npos) return 0;
    struct Block {
        std::string              tag;
        std::vector<std::string> lines;
    };
    auto blocks_of = [](const std::string & text) {
        std::vector<Block> blocks(1);
        for (const std::string & l : lines_of(text)) {
            std::string t = trim(l);
            if (t.empty()) continue;
            if (t[0] == '[') blocks.push_back({ t, {} });
            else blocks.back().lines.push_back(t);
        }
        return blocks;
    };
    std::vector<Block> a = blocks_of(song.lyrics), b = blocks_of(fresh);
    if (a.size() != b.size()) return 0;
    for (size_t i = 0; i < a.size(); i++)
        if (fold_word(a[i].tag) != fold_word(b[i].tag) || a[i].lines.size() != b[i].lines.size()) return 0;
    std::map<std::string, std::string> chosen;  // ligne d'origine → ligne gardée : un refrain répété reste identique partout
    int                                changed = 0;
    std::string                        out;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].tag.empty() && a[i].lines.empty()) continue;
        if (!out.empty()) out += "\\n\\n";
        out += a[i].tag;
        for (size_t k = 0; k < a[i].lines.size(); k++) {
            const std::string & o   = a[i].lines[k];
            std::string         n   = b[i].lines[k];
            const std::string   key = fold_line(o);
            auto                it  = chosen.find(key);
            if (it != chosen.end()) n = it->second;
            else {
                // une ligne devenue beaucoup plus longue ou plus courte ne se chanterait plus sur la même musique : l'originale reste
                if (n.size() * 10 > o.size() * 22 || n.size() * 10 < o.size() * 4) n = o;
                chosen[key] = n;
            }
            if (fold_line(n) != key) changed++;
            out += (out.empty() ? "" : "\\n") + n;
        }
    }
    if (changed == 0 || ai_forbidden_found(out, avoid).size() > ai_forbidden_found(song.lyrics, avoid).size()) return 0;
    song.lyrics = out;
    return changed;
}

std::string ai_avoid_retry_prompt(const std::vector<std::string> & found) {''')])

# ---------------------------------------------------------------- app.hpp (réglage)
patch('app.hpp', [
    ('    bool                ai_natural = true;  // 1.1.018 : consignes d\'écriture naturelle pour les paroles écrites par l\'IA\n',
     '    bool                ai_natural = true;  // 1.1.018 : consignes d\'écriture naturelle pour les paroles écrites par l\'IA\n'
     '    bool                ai_polish  = true;  // 1.1.021 : passe de relecture des paroles (réécrit les lignes qui sonnent « IA »)\n'),
    ('    v = yyjson_obj_get(r, "ai_natural");\n    if (yyjson_is_bool(v)) s.ai_natural = yyjson_get_bool(v);\n',
     '    v = yyjson_obj_get(r, "ai_natural");\n    if (yyjson_is_bool(v)) s.ai_natural = yyjson_get_bool(v);\n'
     '    v = yyjson_obj_get(r, "ai_polish");\n    if (yyjson_is_bool(v)) s.ai_polish = yyjson_get_bool(v);\n'),
    ('    yyjson_mut_obj_add_bool(doc, o, "ai_natural", s.ai_natural);\n',
     '    yyjson_mut_obj_add_bool(doc, o, "ai_natural", s.ai_natural);\n    yyjson_mut_obj_add_bool(doc, o, "ai_polish", s.ai_polish);\n')])

# ---------------------------------------------------------------- app_ai.cpp (Composer avec l'IA)
patch('app_ai.cpp', [
    ('    const bool very_long = req.length == 3 && !req.instrumental;\n    std::thread([job, system, user, ep, llm, model, layers, avoid, very_long] {',
     '    const bool very_long = req.length == 3 && !req.instrumental;\n    const bool polish    = settings.ai_polish && !req.instrumental;  // 1.1.021\n'
     '    const std::string lang = req.language;\n    std::thread([job, system, user, ep, llm, model, layers, avoid, very_long, polish, lang] {'),
    ('            if (very_long)\n                ai_lengthen_song(job->song, AI_LONG_LINES, avoid, [&](const std::string & sy, const std::string & u) {',
     '            if (polish && job->song.ok && !job->cancel)  // 1.1.021 : relecture avant la suite d\'un morceau très long\n'
     '                ai_polish_song(job->song, lang, avoid, [&](const std::string & sy, const std::string & u) {\n'
     '                    return job->local ? ai_local_chat(llm, model, layers, sy, u, &job->stage, &job->cancel) : ai_chat(ep, sy, u, 180000);\n'
     '                }, &job->cancel);\n'
     '            if (very_long)\n                ai_lengthen_song(job->song, AI_LONG_LINES, avoid, [&](const std::string & sy, const std::string & u) {')])

# ---------------------------------------------------------------- app_cover.cpp (album IA)
patch('app_cover.cpp', [
    ('    std::thread([job, req, base, ep, llm, model, layers] {\n        auto work = [&](const AiAsk & ask) {',
     '    const bool polish = settings.ai_polish;  // 1.1.021\n    std::thread([job, req, base, ep, llm, model, layers, polish] {\n        auto work = [&](const AiAsk & ask) {'),
    ('                if (r.length == 3 && !r.instrumental) ai_lengthen_song(s, AI_LONG_LINES, r.avoid, ask, &job->cancel, album_lines);',
     '                if (polish && s.ok && !r.instrumental && !job->cancel) ai_polish_song(s, req.language, r.avoid, ask, &job->cancel);  // 1.1.021\n'
     '                if (r.length == 3 && !r.instrumental) ai_lengthen_song(s, AI_LONG_LINES, r.avoid, ask, &job->cancel, album_lines);')])

# ---------------------------------------------------------------- app_aiset.cpp (case)
patch('app_aiset.cpp', [(
    '    if (ImGui::Checkbox(tr("Éviter aussi les mots qui reviennent dans les derniers morceaux écrits par l\'IA"), &settings.ai_auto_avoid)) save_settings(settings, settings_path);',
    '    if (ImGui::Checkbox(tr("Relire les paroles : l\'IA corrige les lignes qui sonnent « IA »"), &settings.ai_polish)) save_settings(settings, settings_path);\n'
    '    if (ImGui::IsItemHovered())\n'
    '        ImGui::SetTooltip("%s", tr("Après avoir écrit les paroles, l\'IA les relit et ne réécrit que les lignes toutes faites ou traduites de l\'anglais. Les sections, le nombre de lignes et les refrains restent identiques. Compter quelques secondes de plus par chanson ; désactivez si le résultat vous déçoit."));\n'
    '    if (ImGui::Checkbox(tr("Éviter aussi les mots qui reviennent dans les derniers morceaux écrits par l\'IA"), &settings.ai_auto_avoid)) save_settings(settings, settings_path);')])

# ---------------------------------------------------------------- main.cpp (essai)
patch('main.cpp', [(
    '        // Essai (1.0.70) : « --lengthen-test sortie.txt »',
    '''        // Essai (1.1.021) : « --polish-test sortie.txt » : paroles françaises par l'IA locale, puis passe de relecture ; avant / après.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--polish-test") {
            app.local_llm = find_local_llm(app.app_dir());
            AiRequest req    = app.ai_request();
            req.language     = "French";
            req.language_tag = "French";
            req.instrumental = false;
            req.length       = 0;
            req.influence.clear();
            req.theme = _wgetenv(L"STUDIO_TEST_THEME") ? narrow(_wgetenv(L"STUDIO_TEST_THEME")) : std::string("une gare de nuit en hiver");
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<bool> stop{ false };
            std::atomic<int>  stage{ 0 };
            FILE *            f = _wfopen(argv[2], L"wb");
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                for (int n = 0; n < 2; n++) {
                    AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(req));
                    AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                    if (!s.ok) { if (f) fprintf(f, "=== essai %d : paroles illisibles\\n%s\\n", n, a.text.c_str()); continue; }
                    std::string before = s.lyrics;
                    auto        t0     = std::chrono::steady_clock::now();
                    int         changed = ai_polish_song(s, "French", req.avoid, ask, &stop);
                    double      secs    = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    if (f) fprintf(f, "=== essai %d : %s : %d ligne(s) changee(s) en %.0f s\\n--- avant\\n%s\\n--- apres\\n%s\\n\\n", n, s.title.c_str(), changed, secs, before.c_str(), s.lyrics.c_str());
                }
            });
            if (f) fclose(f);
            return 0;
        }
        // Essai (1.0.70) : « --lengthen-test sortie.txt »''')])
print('OK')
