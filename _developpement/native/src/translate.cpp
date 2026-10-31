// Traduction des paroles (voir translate.hpp).
#include "translate.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <thread>

#include "i18n.hpp"  // N_ : textes traduits par l'interface
#include "net.hpp"
#include "studio.hpp"  // narrow, widen
#include "yyjson.h"

// ---------------------------------------------------------------------------- Langues

const std::vector<TranslateLang> & translate_languages() {
    // Les 10 langues chantées par YuE2, puis les autres (Internet seulement si llm == nullptr).
    static const std::vector<TranslateLang> langs = {
        { "en", "English", "English", true },
        { "fr", "Français", "French", true },
        { "es", "Español", "Spanish", true },
        { "it", "Italiano", "Italian", true },
        { "de", "Deutsch", "German", true },
        { "pt", "Português", "Portuguese", true },
        { "ja", "日本語", "Japanese", true },
        { "ko", "한국어", "Korean", true },
        { "zh-CN", "中文（普通话）", "Simplified Chinese (Mandarin)", true },
        { "yue", "粵語", "Cantonese (written in Traditional Chinese characters)", true },
        // Hors YuE2, mais pris en charge par le modèle local (Qwen2.5 : 29 langues)
        { "nl", "Nederlands", "Dutch", false },
        { "ru", "Русский", "Russian", false },
        { "pl", "Polski", "Polish", false },
        { "tr", "Türkçe", "Turkish", false },
        { "ar", "العربية", "Arabic", false },
        { "vi", "Tiếng Việt", "Vietnamese", false },
        { "th", "ไทย", "Thai", false },
        { "id", "Bahasa Indonesia", "Indonesian", false },
        { "cs", "Čeština", "Czech", false },
        { "uk", "Українська", "Ukrainian", false },
        { "zh-TW", "中文（繁體）", "Traditional Chinese", false },
        // Internet seulement
        { "ca", "Català", nullptr, false },
        { "eu", "Euskara", nullptr, false },
        { "gl", "Galego", nullptr, false },
        { "br", "Brezhoneg", nullptr, false },
        { "co", "Corsu", nullptr, false },
        { "oc", "Occitan", nullptr, false },
        { "ga", "Gaeilge", nullptr, false },
        { "cy", "Cymraeg", nullptr, false },
        { "sv", "Svenska", nullptr, false },
        { "no", "Norsk", nullptr, false },
        { "da", "Dansk", nullptr, false },
        { "fi", "Suomi", nullptr, false },
        { "is", "Íslenska", nullptr, false },
        { "el", "Ελληνικά", nullptr, false },
        { "ro", "Română", nullptr, false },
        { "hu", "Magyar", nullptr, false },
        { "sk", "Slovenčina", nullptr, false },
        { "sl", "Slovenščina", nullptr, false },
        { "hr", "Hrvatski", nullptr, false },
        { "sr", "Српски", nullptr, false },
        { "bg", "Български", nullptr, false },
        { "lt", "Lietuvių", nullptr, false },
        { "lv", "Latviešu", nullptr, false },
        { "et", "Eesti", nullptr, false },
        { "he", "עברית", nullptr, false },
        { "fa", "فارسی", nullptr, false },
        { "hi", "हिन्दी", nullptr, false },
        { "bn", "বাংলা", nullptr, false },
        { "ta", "தமிழ்", nullptr, false },
        { "ur", "اردو", nullptr, false },
        { "ms", "Bahasa Melayu", nullptr, false },
        { "tl", "Filipino", nullptr, false },
        { "sw", "Kiswahili", nullptr, false },
        { "af", "Afrikaans", nullptr, false },
        { "la", "Latina", nullptr, false },
        { "eo", "Esperanto", nullptr, false },
        { "ht", "Kreyòl ayisyen", nullptr, false },
    };
    return langs;
}

const TranslateLang * find_translate_lang(const std::string & code) {
    for (const TranslateLang & l : translate_languages()) {
        if (code == l.code) return &l;
    }
    return nullptr;
}

const TranslateModelFile & translate_model_file() {
    static const TranslateModelFile f = {
        "qwen2.5-3b-instruct-q4_k_m.gguf",
        "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf",
        2104932768ull,
    };
    return f;
}

LocalTranslator find_local_translator(const fs::path & app_dir, const fs::path & models_dir) {
    LocalTranslator t;
    std::error_code ec;
    // llama-completion.exe : version récente de llama.cpp ; llama-cli.exe : anciennes versions (même usage avec -no-cnv).
    for (const wchar_t * name : { L"llama-completion.exe", L"llama-cli.exe" }) {
        // 1.0.42 : outils\llama seulement (l'ancien dossier traduction y est migré) ; jamais le dossier parent.
        for (const fs::path & dir : { app_dir / "outils" / "llama" }) {
            if (t.exe.empty() && fs::exists(dir / name, ec)) t.exe = dir / name;
        }
    }
    fs::path m = models_dir / "traduction" / widen(translate_model_file().name);
    if (fs::exists(m, ec) && fs::file_size(m, ec) > 100ull * 1048576) t.model = m;
    return t;
}

// ---------------------------------------------------------------------------- Outils

static std::string url_encode(const std::string & s) {
    static const char * hex = "0123456789ABCDEF";
    std::string         out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char) c;
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static std::vector<std::string> split_lines(const std::string & text) {
    std::vector<std::string> lines;
    std::string              cur;
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);
    return lines;
}

static std::string trim_copy(const std::string & s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Ligne à garder telle quelle : vide, balise de section « [Verse] », ou indication entre parenthèses seule.
static bool keep_line(const std::string & line) {
    std::string t = trim_copy(line);
    if (t.empty()) return true;
    return t.front() == '[' && t.back() == ']';
}

// ---------------------------------------------------------------------------- Internet (Google Traduction, gratuit)

// Traduit un texte (plusieurs lignes possibles). Renvoie false si le service ne répond pas.
static bool google_translate(const std::string & text, const std::string & target, std::string & out) {
    std::string url = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=" + target +
                      "&dt=t&q=" + url_encode(text);
    std::string body;
    if (https_get_url(url, body) != 200) return false;
    yyjson_doc * doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) return false;
    yyjson_val * root = yyjson_doc_get_root(doc);
    yyjson_val * segs = yyjson_arr_get(root, 0);
    out.clear();
    size_t       i, n;
    yyjson_val * seg;
    yyjson_arr_foreach(segs, i, n, seg) {
        yyjson_val * t = yyjson_arr_get(seg, 0);
        if (yyjson_is_str(t)) out += yyjson_get_str(t);
    }
    yyjson_doc_free(doc);
    return !out.empty();
}

bool translate_web_available() {
    std::string out;
    return google_translate("hello", "fr", out);
}

// Traduit un groupe de lignes chantées consécutives ; garde le même nombre de lignes.
static bool web_block(const std::vector<std::string> & lines, const std::string & target, std::vector<std::string> & out) {
    std::string joined;
    for (size_t i = 0; i < lines.size(); i++) joined += (i ? "\n" : "") + lines[i];
    std::string result;
    // Une adresse trop longue est refusée : au-delà d'environ 1 800 caractères encodés, on coupe le groupe en deux.
    if (url_encode(joined).size() > 1800 && lines.size() > 1) {
        size_t                   half = lines.size() / 2;
        std::vector<std::string> a(lines.begin(), lines.begin() + half), b(lines.begin() + half, lines.end()), ra, rb;
        if (!web_block(a, target, ra) || !web_block(b, target, rb)) return false;
        out = ra;
        out.insert(out.end(), rb.begin(), rb.end());
        return true;
    }
    if (!google_translate(joined, target, result)) return false;
    std::vector<std::string> got = split_lines(result);
    while (got.size() > lines.size() && trim_copy(got.back()).empty()) got.pop_back();
    if (got.size() == lines.size()) {
        out = got;
        return true;
    }
    // Lignes fusionnées par le service : on retraduit ligne par ligne.
    out.clear();
    for (const std::string & l : lines) {
        std::string one;
        if (!google_translate(l, target, one)) return false;
        std::replace(one.begin(), one.end(), '\n', ' ');
        out.push_back(trim_copy(one));
    }
    return true;
}

// ---------------------------------------------------------------------------- Local (llama.cpp + Qwen2.5)

// Lance llama.cpp et récupère sa sortie (stdout). Renvoie le code de sortie, -1 si le lancement échoue.
static int run_capture(const std::wstring & command, std::string & output, std::atomic<bool> * cancel,
                       std::atomic<float> * progress, int expected_lines) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE              rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE       nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = nul;  // journal de chargement de llama.cpp : ignoré
    si.hStdInput  = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring        cmd = command;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                             nullptr, &si, &pi);
    CloseHandle(wr);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }
    // Surveillance de l'annulation : on arrête le programme, ce qui débloque la lecture.
    std::atomic<bool> finished{ false };
    std::thread       watch([&] {
        while (!finished) {
            if (cancel && *cancel) {
                TerminateProcess(pi.hProcess, 1);
                break;
            }
            Sleep(100);
        }
    });
    char  buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        output.append(buf, got);
        if (progress && expected_lines > 0) {
            int lines = (int) std::count(output.begin(), output.end(), '\n');
            *progress = std::min(0.95f, 0.1f + 0.85f * (float) lines / (float) expected_lines);
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    finished = true;
    watch.join();
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int) code;
}

// Les lignes à traduire sont numérotées (« 3| texte ») : le modèle répond avec les mêmes numéros, ce qui
// garantit qu'aucune ligne ne se décale. Une ligne manquante dans la réponse reste dans la langue d'origine.
static bool local_translate(const std::vector<std::string> & lines, const TranslateLang & lang, const LocalTranslator & local,
                            std::vector<std::string> & out, int & kept, std::atomic<float> * progress, std::atomic<bool> * cancel,
                            std::string & error) {
    std::string numbered;
    for (size_t i = 0; i < lines.size(); i++) numbered += std::to_string(i + 1) + "| " + lines[i] + "\n";
    // Gabarit de conversation de Qwen2.5 (ChatML).
    std::string prompt =
        "<|im_start|>system\nYou are a professional translator of song lyrics.<|im_end|>\n"
        "<|im_start|>user\nTranslate each numbered line of these song lyrics into " + std::string(lang.llm) + ".\n"
        "Rules: answer with exactly one line per numbered line, starting with the same number followed by \"|\" "
        "(for example \"1| ...\", \"2| ...\"). Keep the meaning and a natural, singable style. "
        "Do not add comments, notes or explanations.\n\n" +
        numbered + "<|im_end|>\n<|im_start|>assistant\n1|";  // réponse amorcée par « 1| » : le modèle garde la numérotation
    fs::path prompt_file = app_temp_dir() / L"studio_yue2_traduction.txt";  // 1.0.30 : dans le dossier du programme
    {
        FILE * f = _wfopen(prompt_file.c_str(), L"wb");
        if (!f) {
            error = N_("Fichier temporaire impossible à écrire.");
            return false;
        }
        fwrite(prompt.data(), 1, prompt.size(), f);
        fclose(f);
    }
    int threads = std::max(4, (int) std::thread::hardware_concurrency() / 2);
    int budget  = std::clamp((int) lines.size() * 40 + 64, 256, 3072);  // jetons de réponse
    // Processeur uniquement (-ngl 0) : la carte graphique reste libre pour les générations.
    std::wstring cmd = L"\"" + local.exe.wstring() + L"\" -m \"" + local.model.wstring() + L"\" -f \"" + prompt_file.wstring() +
                       L"\" -n " + std::to_wstring(budget) + L" -c 8192 --temp 0.2 --top-p 0.9 -ngl 0 -t " + std::to_wstring(threads) +
                       L" --no-display-prompt -no-cnv";
    std::string output;
    int         code = run_capture(cmd, output, cancel, progress, (int) lines.size());
    std::error_code ec;
    fs::remove(prompt_file, ec);
    if (cancel && *cancel) {
        error = N_("Traduction annulée.");
        return false;
    }
    if (code != 0 && output.empty()) {
        error = N_("Le moteur de traduction local n'a pas pu démarrer.");
        return false;
    }
    // llama-completion écrit « [end of text] » à la fin de sa sortie : on le retire.
    size_t eot = output.find("[end of text]");
    if (eot != std::string::npos) output.erase(eot);
    output = "1|" + output;  // l'amorce « 1| » fait partie de la réponse du modèle
    out  = lines;
    kept = (int) lines.size();
    std::vector<bool> done(lines.size(), false);
    for (const std::string & raw : split_lines(output)) {
        std::string l = trim_copy(raw);
        // Variantes vues : « |1| texte », « | 1| texte », « **1|** texte » : on saute ces caractères de tête.
        size_t s0 = l.find_first_not_of("|* \t");
        if (s0 == std::string::npos) continue;
        l        = l.substr(s0);
        size_t p = 0;
        while (p < l.size() && isdigit((unsigned char) l[p])) p++;
        if (p == 0 || p >= l.size() || (l[p] != '|' && l[p] != '.' && l[p] != ')' && l[p] != ':')) continue;
        int n = atoi(l.substr(0, p).c_str());
        if (n < 1 || n > (int) lines.size() || done[n - 1]) continue;
        std::string t = trim_copy(l.substr(p + 1));
        if (t.empty()) continue;
        out[n - 1]  = t;
        done[n - 1] = true;
        kept--;
    }
    if (kept == (int) lines.size()) {
        error = N_("Le modèle local n'a pas renvoyé de traduction exploitable.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------- Traduction des paroles

TranslateResult translate_lyrics(const std::string & lyrics, const std::string & target, bool use_web,
                                 const LocalTranslator & local, std::atomic<float> * progress, std::atomic<bool> * cancel) {
    TranslateResult       r;
    const TranslateLang * lang = find_translate_lang(target);
    if (!lang) {
        r.error = N_("Langue inconnue.");
        return r;
    }
    std::vector<std::string> lines = split_lines(lyrics);
    // Groupes de lignes chantées consécutives (entre deux balises ou lignes vides).
    std::vector<std::pair<size_t, size_t>> blocks;  // début, fin (exclue)
    for (size_t i = 0; i < lines.size();) {
        if (keep_line(lines[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < lines.size() && !keep_line(lines[j])) j++;
        blocks.push_back({ i, j });
        i = j;
    }
    if (blocks.empty()) {
        r.error = N_("Aucune parole à traduire.");
        return r;
    }
    if (progress) *progress = 0.02f;

    // 1. Internet
    if (use_web) {
        std::vector<std::string> result = lines;
        bool                     ok     = true;
        for (size_t b = 0; b < blocks.size() && ok; b++) {
            if (cancel && *cancel) {
                r.error = N_("Traduction annulée.");
                return r;
            }
            std::vector<std::string> src(lines.begin() + blocks[b].first, lines.begin() + blocks[b].second), dst;
            ok = web_block(src, target, dst);
            if (ok) std::copy(dst.begin(), dst.end(), result.begin() + blocks[b].first);
            if (progress) *progress = (float) (b + 1) / (float) blocks.size();
        }
        if (ok) {
            r.ok     = true;
            r.engine = "web";
            for (size_t i = 0; i < result.size(); i++) r.text += (i ? "\n" : "") + result[i];
            return r;
        }
    }

    // 2. Hors ligne : modèle local
    if (!local.ready()) {
        r.error = use_web ? N_("Pas de connexion à Internet et pas de modèle de traduction local (Réglages › Analyse et paroles).")
                          : N_("Pas de modèle de traduction local (Réglages › Analyse et paroles).");
        return r;
    }
    if (!lang->llm) {
        r.error = N_("Pas de connexion à Internet : cette langue n'est disponible qu'avec la traduction par Internet.");
        return r;
    }
    std::vector<std::string> src, dst;
    for (auto & [a, b] : blocks) src.insert(src.end(), lines.begin() + a, lines.begin() + b);
    if (progress) *progress = 0.1f;
    if (!local_translate(src, *lang, local, dst, r.kept, progress, cancel, r.error)) return r;
    std::vector<std::string> result = lines;
    size_t                   k      = 0;
    for (auto & [a, b] : blocks) {
        for (size_t i = a; i < b; i++) result[i] = dst[k++];
    }
    r.ok     = true;
    r.engine = "local";
    for (size_t i = 0; i < result.size(); i++) r.text += (i ? "\n" : "") + result[i];
    if (progress) *progress = 1.f;
    return r;
}
