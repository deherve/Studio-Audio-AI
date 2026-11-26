# 1.1.018 : essai --natural-test sortie.txt : meme demande de paroles francaises sans puis avec les consignes d'ecriture naturelle.
import io, sys
p = r"C:\Users\deher\Downloads\dev\music\_developpement\native\src\main.cpp"
t = io.open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
a = "        // Essai (1.0.70) : « --lengthen-test sortie.txt »"
assert t.count(a) == 1
new = '''        // Essai (1.1.018) : « --natural-test sortie.txt » : paroles françaises par l'IA locale, sans puis avec les consignes d'écriture naturelle.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--natural-test") {
            app.local_llm = find_local_llm(app.app_dir());
            AiRequest req    = app.ai_request();
            req.language     = "French";
            req.language_tag = "French";
            req.instrumental = false;
            req.length       = 0;
            req.influence.clear();
            req.theme = "une gare de nuit en hiver";
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<bool> stop{ false };
            std::atomic<int>  stage{ 0 };
            FILE *            f = _wfopen(argv[2], L"wb");
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                for (int natural = 0; natural < 2; natural++) {
                    ai_set_natural(natural != 0);
                    AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(req));
                    AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                    if (f) fprintf(f, "=== naturel=%d ok=%d titre=%s\n%s\n\n", natural, (int) a.ok, s.title.c_str(), s.lyrics.c_str());
                }
            });
            if (f) fclose(f);
            return 0;
        }
'''
t = t.replace(a, new + a)
io.open(p, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n"))
print("OK")
