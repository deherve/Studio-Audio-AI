// Composer avec l'IA (voir ai_writer.hpp).
#include "ai_writer.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <thread>

#include "i18n.hpp"
#include "net.hpp"
#include "studio.hpp"  // widen, narrow
#include "yyjson.h"

// ---------------------------------------------------------------------------- Services et serveurs

const std::vector<AiService> & ai_services() {
    // Adresses « compatibles OpenAI » : la même requête marche partout, seuls l'adresse, la clé et le modèle changent.
    // Les modèles proposés ne sont qu'un point de départ : « Lister les modèles » montre ceux du compte.
    static const std::vector<AiService> list = {
        { "openai", "OpenAI (ChatGPT)", AI_OPENAI, "https://api.openai.com/v1", "gpt-4.1-mini", "https://platform.openai.com/api-keys" },
        { "anthropic", "Claude (Anthropic)", AI_ANTHROPIC, "https://api.anthropic.com/v1", "claude-sonnet-5-5", "https://console.anthropic.com/settings/keys" },
        { "gemini", "Google Gemini", AI_OPENAI, "https://generativelanguage.googleapis.com/v1beta/openai", "gemini-2.5-flash", "https://aistudio.google.com/apikey" },
        { "mistral", "Mistral AI", AI_OPENAI, "https://api.mistral.ai/v1", "mistral-large-latest", "https://console.mistral.ai/api-keys" },
        { "openrouter", "OpenRouter", AI_OPENAI, "https://openrouter.ai/api/v1", "openrouter/auto", "https://openrouter.ai/keys" },
        { "deepseek", "DeepSeek", AI_OPENAI, "https://api.deepseek.com/v1", "deepseek-chat", "https://platform.deepseek.com/api_keys" },
        { "groq", "Groq", AI_OPENAI, "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile", "https://console.groq.com/keys" },
        { "xai", "xAI (Grok)", AI_OPENAI, "https://api.x.ai/v1", "grok-3-mini", "https://console.x.ai" },
        { "together", "Together AI", AI_OPENAI, "https://api.together.xyz/v1", "meta-llama/Llama-3.3-70B-Instruct-Turbo", "https://api.together.xyz/settings/api-keys" },
        { "perplexity", "Perplexity", AI_OPENAI, "https://api.perplexity.ai", "sonar", "https://www.perplexity.ai/settings/api" },
        { "fireworks", "Fireworks AI", AI_OPENAI, "https://api.fireworks.ai/inference/v1", "accounts/fireworks/models/llama-v3p3-70b-instruct", "https://fireworks.ai/account/api-keys" },
        { "cerebras", "Cerebras", AI_OPENAI, "https://api.cerebras.ai/v1", "llama-3.3-70b", "https://cloud.cerebras.ai" },
        { "cohere", "Cohere", AI_OPENAI, "https://api.cohere.ai/compatibility/v1", "command-a-03-2025", "https://dashboard.cohere.com/api-keys" },
        { "huggingface", "Hugging Face", AI_OPENAI, "https://router.huggingface.co/v1", "meta-llama/Llama-3.3-70B-Instruct", "https://huggingface.co/settings/tokens" },
        { "custom", N_("Autre service compatible OpenAI"), AI_OPENAI, "", "", "" },
    };
    return list;
}

const AiService * find_ai_service(const std::string & id) {
    for (const AiService & s : ai_services()) if (id == s.id) return &s;
    return nullptr;
}

const std::vector<AiServerKind> & ai_server_kinds() {
    static const std::vector<AiServerKind> list = {
        { "Ollama", 11434 }, { "LM Studio", 1234 }, { "llama-server / LocalAI", 8080 }, { "KoboldCpp", 5001 },
        { "Jan", 1337 },     { "text-generation-webui", 5000 }, { "vLLM", 8000 }, { "GPT4All", 4891 },
    };
    return list;
}

// Noms des modèles d'une réponse « /models » ({"data":[{"id":…}]} ; Ollama natif : {"models":[{"name":…}]}).
static std::vector<std::string> model_ids(const std::string & body) {
    std::vector<std::string> out;
    yyjson_doc *             doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) return out;
    yyjson_val * root = yyjson_doc_get_root(doc);
    for (const char * key : { "data", "models" }) {
        size_t       i, n;
        yyjson_val * item;
        yyjson_arr_foreach(yyjson_obj_get(root, key), i, n, item) {
            for (const char * field : { "id", "name" }) {
                yyjson_val * v = yyjson_obj_get(item, field);
                if (yyjson_is_str(v)) {
                    out.push_back(yyjson_get_str(v));
                    break;
                }
            }
        }
    }
    yyjson_doc_free(doc);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<AiServer> ai_detect_servers() {
    std::vector<AiServer> found;
    for (const AiServerKind & k : ai_server_kinds()) {
        std::string base = "http://127.0.0.1:" + std::to_string(k.port) + "/v1";
        std::string body;
        int         code = http_request("GET", base + "/models", {}, "", body, 3000);
        if (code != 200) continue;
        AiServer s;
        s.name   = k.name;
        s.url    = base;
        s.models = model_ids(body);
        found.push_back(std::move(s));
    }
    return found;
}

// Message d'erreur d'un service : {"error":{"message":…}} (OpenAI, Anthropic…) ou {"error":"…"} ou {"message":…}.
static std::string service_error(const std::string & body) {
    std::string  msg;
    yyjson_doc * doc = yyjson_read(body.data(), body.size(), 0);
    if (doc) {
        yyjson_val * root = yyjson_doc_get_root(doc);
        yyjson_val * e    = yyjson_obj_get(root, "error");
        yyjson_val * m    = yyjson_is_obj(e) ? yyjson_obj_get(e, "message") : e;
        if (!yyjson_is_str(m)) m = yyjson_obj_get(root, "message");
        if (!yyjson_is_str(m)) m = yyjson_obj_get(root, "detail");
        if (yyjson_is_str(m)) msg = yyjson_get_str(m);
        yyjson_doc_free(doc);
    }
    if (msg.empty()) msg = body.substr(0, 300);
    return msg;
}

static std::vector<std::pair<std::string, std::string>> auth_headers(const AiEndpoint & ep) {
    std::vector<std::pair<std::string, std::string>> h;
    if (ep.kind == AI_ANTHROPIC) {
        h.push_back({ "x-api-key", ep.key });
        h.push_back({ "anthropic-version", "2023-06-01" });
    } else if (!ep.key.empty()) {
        h.push_back({ "Authorization", "Bearer " + ep.key });
    }
    return h;
}

static std::string trim_slash(std::string url) {
    while (!url.empty() && (url.back() == '/' || url.back() == ' ')) url.pop_back();
    return url;
}

// Erreur lisible d'après le code HTTP.
static void fill_error(AiCallResult & r, int code, const std::string & body) {
    if (code == 0) {
        r.error = N_("Le service ne répond pas (pas de connexion, adresse fausse ou serveur arrêté).");
    } else if (code == 401 || code == 403) {
        r.error     = N_("Clé d'API refusée : %s");
        r.error_arg = service_error(body);
    } else if (code == 404) {
        r.error     = N_("Modèle ou adresse introuvable : %s");
        r.error_arg = service_error(body);
    } else if (code == 429) {
        r.error     = N_("Trop de demandes ou crédit épuisé chez le service : %s");
        r.error_arg = service_error(body);
    } else {
        r.error     = N_("Le service a répondu par une erreur : %s");
        r.error_arg = std::to_string(code) + " · " + service_error(body);
    }
}

AiCallResult ai_chat(const AiEndpoint & ep, const std::string & system, const std::string & user, int timeout_ms) {
    AiCallResult r;
    std::string  base = trim_slash(ep.base_url);
    if (base.empty()) {
        r.error = N_("Adresse du service manquante (Réglages › IA).");
        return r;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        bool             with_temperature = attempt == 0;  // second essai sans : certains modèles la refusent
        yyjson_mut_doc * doc              = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val * o                = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, o);
        yyjson_mut_obj_add_strcpy(doc, o, "model", ep.model.c_str());
        yyjson_mut_val * msgs = yyjson_mut_obj_add_arr(doc, o, "messages");
        std::string      url;
        if (ep.kind == AI_ANTHROPIC) {
            url = base + "/messages";
            yyjson_mut_obj_add_strcpy(doc, o, "system", system.c_str());
            yyjson_mut_obj_add_int(doc, o, "max_tokens", 6000);  // 1.0.49 : paroles « très longues » (≈ 130 lignes)
        } else {
            url                = base + "/chat/completions";
            // Serveur sur ce PC (IA intégrée, Ollama, LM Studio…) : longueur de réponse fixée, certains serveurs
            // s'arrêtant sinon après quelques centaines de mots (1.0.28). Les services en ligne gardent leur réglage.
            if (base.rfind("http://127.0.0.1", 0) == 0 || base.rfind("http://localhost", 0) == 0) yyjson_mut_obj_add_int(doc, o, "max_tokens", 5000);  // 1.0.49 (contexte local de 8192)
            yyjson_mut_val * s = yyjson_mut_arr_add_obj(doc, msgs);
            yyjson_mut_obj_add_str(doc, s, "role", "system");
            yyjson_mut_obj_add_strcpy(doc, s, "content", system.c_str());
        }
        yyjson_mut_val * u = yyjson_mut_arr_add_obj(doc, msgs);
        yyjson_mut_obj_add_str(doc, u, "role", "user");
        yyjson_mut_obj_add_strcpy(doc, u, "content", user.c_str());
        if (with_temperature) yyjson_mut_obj_add_real(doc, o, "temperature", 0.8);
        char *      json = yyjson_mut_write(doc, 0, nullptr);
        std::string payload = json ? json : "";
        free(json);
        yyjson_mut_doc_free(doc);

        std::string body;
        int         code = http_request("POST", url, auth_headers(ep), payload, body, timeout_ms);
        if (code == 400 && with_temperature && body.find("temperature") != std::string::npos) continue;
        if (code != 200) {
            fill_error(r, code, body);
            return r;
        }
        // Réponse : choices[0].message.content (OpenAI) ou content[].text (Anthropic).
        yyjson_doc * rd = yyjson_read(body.data(), body.size(), 0);
        if (rd) {
            yyjson_val * root = yyjson_doc_get_root(rd);
            if (ep.kind == AI_ANTHROPIC) {
                size_t       i, n;
                yyjson_val * part;
                yyjson_arr_foreach(yyjson_obj_get(root, "content"), i, n, part) {
                    yyjson_val * t = yyjson_obj_get(part, "text");
                    if (yyjson_is_str(t)) r.text += yyjson_get_str(t);
                }
            } else {
                yyjson_val * c = yyjson_arr_get_first(yyjson_obj_get(root, "choices"));
                yyjson_val * t = yyjson_obj_get(yyjson_obj_get(c, "message"), "content");
                if (yyjson_is_str(t)) r.text = yyjson_get_str(t);
            }
            yyjson_doc_free(rd);
        }
        r.ok = !r.text.empty();
        if (!r.ok) {
            r.error     = N_("Réponse vide ou inattendue du service : %s");
            r.error_arg = body.substr(0, 300);
        }
        return r;
    }
    return r;
}

std::vector<std::string> ai_list_models(const AiEndpoint & ep, std::string & error) {
    std::string body;
    int         code = http_request("GET", trim_slash(ep.base_url) + "/models", auth_headers(ep), "", body, 15000);
    if (code != 200) {
        AiCallResult r;
        fill_error(r, code, body);
        error = i18n::trf(i18n::tr(r.error.c_str()), r.error_arg.c_str());
        return {};
    }
    return model_ids(body);
}

// ---------------------------------------------------------------------------- Moteur intégré

const std::vector<AiModelFile> & ai_model_catalog() {
    // Tailles relevées sur Hugging Face le 23/10/2026 (quantification Q4_K_M : bon compromis qualité / poids).
    static const std::vector<AiModelFile> list = {
        { "qwen25_3b", "Qwen2.5 3B", "bartowski/Qwen2.5-3B-Instruct-GGUF", "Qwen2.5-3B-Instruct-Q4_K_M.gguf", 1929903264ull,
          N_("Très léger et rapide ; paroles simples, surtout en anglais."), 1 },
        { "qwen25_7b", "Qwen2.5 7B", "bartowski/Qwen2.5-7B-Instruct-GGUF", "Qwen2.5-7B-Instruct-Q4_K_M.gguf", 4683074240ull,
          N_("Bon équilibre ; correct en français et en espagnol."), 3 },
        { "llama31_8b", "Llama 3.1 8B", "bartowski/Meta-Llama-3.1-8B-Instruct-GGUF", "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf", 4920739232ull,
          N_("Très bon en anglais, moyen dans les autres langues."), 3 },
        { "qwen3_8b", "Qwen3 8B", "Qwen/Qwen3-8B-GGUF", "Qwen3-8B-Q4_K_M.gguf", 5027783488ull,
          N_("Récent, bonnes rimes, multilingue."), 4 },
        { "gemma2_9b", "Gemma 2 9B", "bartowski/gemma-2-9b-it-GGUF", "gemma-2-9b-it-Q4_K_M.gguf", 5761057728ull,
          N_("Style naturel, bon en français."), 4 },
        { "gemma3_12b", "Gemma 3 12B", "bartowski/google_gemma-3-12b-it-GGUF", "google_gemma-3-12b-it-Q4_K_M.gguf", 7300575264ull,
          N_("Très bonne écriture, multilingue."), 5 },
        { "nemo_12b", "Mistral Nemo 12B", "bartowski/Mistral-Nemo-Instruct-2407-GGUF", "Mistral-Nemo-Instruct-2407-Q4_K_M.gguf", 7477208192ull,
          N_("Excellent en français et dans les langues européennes : conseillé."), 5 },
        { "qwen25_14b", "Qwen2.5 14B", "bartowski/Qwen2.5-14B-Instruct-GGUF", "Qwen2.5-14B-Instruct-Q4_K_M.gguf", 8988110976ull,
          N_("Très bon et multilingue, plus lent."), 5 },
        { "qwen3_14b", "Qwen3 14B", "Qwen/Qwen3-14B-GGUF", "Qwen3-14B-Q4_K_M.gguf", 9001752960ull,
          N_("Le meilleur des modèles moyens, plus lent."), 5 },
        { "mistral_small_24b", "Mistral Small 24B", "bartowski/Mistral-Small-24B-Instruct-2501-GGUF", "Mistral-Small-24B-Instruct-2501-Q4_K_M.gguf",
          14333908672ull, N_("Proche des services en ligne, mais demande une carte de 16 Go ou plus."), 5 },
    };
    return list;
}

std::string ai_model_url(const AiModelFile & m) { return std::string("https://huggingface.co/") + m.repo + "/resolve/main/" + m.file; }

AiFit ai_model_fit(unsigned long long bytes, unsigned long long vram, unsigned long long ram, bool gpu_engine) {
    const unsigned long long GB = 1024ull * 1024 * 1024;
    // Carte graphique : le modèle + environ 1,5 Go (contexte, tampons). Processeur : le modèle + 4 Go pour Windows.
    if (gpu_engine && vram > 0 && bytes + GB * 3 / 2 <= vram) return FIT_GPU;
    // Trop gros pour la mémoire vidéo : une partie sur la carte, le reste en mémoire vive (1.0.28).
    if (gpu_engine && vram >= 4 * GB && (ram == 0 || bytes + 4 * GB <= ram + (vram - GB * 5 / 2))) return FIT_MIXED;
    if (ram == 0 || bytes + 4 * GB <= ram) return FIT_CPU;
    return FIT_TOO_BIG;
}

int ai_gpu_layers(unsigned long long bytes, unsigned long long vram) {
    const unsigned long long GB = 1024ull * 1024 * 1024;
    if (vram == 0) return 0;
    if (bytes == 0 || bytes + GB * 3 / 2 <= vram) return 99;  // tout le modèle (llama.cpp ramène 99 au nombre réel)
    if (vram < 4 * GB) return 0;
    // Les modèles du catalogue ont 28 à 48 couches ; on compte 40 couches de même taille et on garde 2,5 Go libres
    // (contexte, tampons, Windows). Exemple : 14,3 Go sur une carte de 12 Go → 26 couches sur la carte.
    double share = (double) (vram - GB * 5 / 2) / (double) bytes;
    return std::clamp((int) (share * 40.0), 0, 98);
}

static fs::path find_server_in(const fs::path & dir) {
    std::error_code ec;
    if (fs::exists(dir / L"llama-server.exe", ec)) return dir / L"llama-server.exe";
    // Archive décompressée dans un sous-dossier : on regarde un niveau plus bas.
    for (const auto & e : fs::directory_iterator(dir, ec)) {
        if (e.is_directory(ec) && fs::exists(e.path() / L"llama-server.exe", ec)) return e.path() / L"llama-server.exe";
    }
    return {};
}

LocalLlm find_local_llm(const fs::path & app_dir) {
    LocalLlm l;
    std::error_code ec;
    // Version pour la carte graphique d'abord (téléchargée par le Studio), puis la version processeur : outils\llama-vulkan
    // et outils\llama (1.0.42 : les anciens dossiers ia\llama et traduction y sont migrés) ; jamais le dossier parent.
    for (const fs::path & dir : { app_dir / L"outils" / L"llama-vulkan", app_dir / L"outils" / L"llama" }) {
        fs::path s = find_server_in(dir);
        if (s.empty()) continue;
        l.server = s;
        l.vulkan = fs::exists(s.parent_path() / L"ggml-vulkan.dll", ec);
        return l;
    }
    return l;
}

const LlamaPackage & llama_vulkan_package() {
    static const LlamaPackage p = { "b11829", "https://github.com/ggml-org/llama.cpp/releases/download/b11829/llama-b11829-bin-win-vulkan-x64.zip",
                                    "llama-b11829-bin-win-vulkan-x64.zip", 36876542ull };
    return p;
}

// Lance une commande sans fenêtre et attend sa fin. Renvoie le code de sortie, -1 si le lancement échoue.
static int run_wait(const std::wstring & command, DWORD timeout_ms) {
    STARTUPINFOW        si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    std::wstring        cmd = command;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    DWORD code = (DWORD) -1;
    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int) code;
}

bool llama_extract(const fs::path & zip, const fs::path & dir, std::string & error) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    // tar.exe est livré avec Windows 10 et 11 ; il sait ouvrir les archives .zip.
    std::wstring cmd  = L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + zip.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
    int          code = run_wait(cmd, 120000);
    if (code != 0) {
        error = N_("Décompression de llama.cpp impossible (tar.exe de Windows).");
        return false;
    }
    std::error_code ec;
    fs::remove(zip, ec);  // l'archive ne sert plus
    if (find_server_in(dir).empty()) {
        error = N_("llama-server.exe est absent de l'archive téléchargée.");
        return false;
    }
    return true;
}

AiCallResult ai_local_chat(const LocalLlm & llm, const fs::path & model, int gpu_layers, const std::string & system,
                           const std::string & user, std::atomic<int> * stage, std::atomic<bool> * cancel) {
    AiCallResult answer;
    AiCallResult start = ai_local_session(llm, model, gpu_layers, stage, cancel,
                                          [&](const AiAsk & ask) { answer = ask(system, user); });
    return start.ok ? answer : start;
}

AiCallResult ai_local_session(const LocalLlm & llm, const fs::path & model, int gpu_layers, std::atomic<int> * stage,
                              std::atomic<bool> * cancel, const std::function<void(const AiAsk &)> & work) {
    AiCallResult r;
    if (!llm.ready()) {
        r.error = N_("llama.cpp introuvable (dossier « outils\\llama » ou « outils\\llama-vulkan »).");
        return r;
    }
    if (stage) *stage = 0;
    const int    port = 8089;  // port réservé au Studio, différent de ceux des serveurs courants
    fs::path     log = app_temp_dir() / L"studio_yue2_ia.log";  // 1.0.30 : dans le dossier du programme
    int          threads = std::max(4, (int) std::thread::hardware_concurrency() / 2);
    // -ngl : couches sur la carte graphique (99 = tout le modèle, version Vulkan) ; 0 : processeur seul.
    std::wstring cmd = L"\"" + llm.server.wstring() + L"\" -m \"" + model.wstring() + L"\" --host 127.0.0.1 --port " + std::to_wstring(port) +
                       L" -c 8192 -np 1 -ngl " + std::to_wstring(llm.vulkan ? gpu_layers : 0) + L" -t " + std::to_wstring(threads);

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE              out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, nullptr);
    STARTUPINFOW        si{ sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError  = out;
    PROCESS_INFORMATION pi{};
    // Réglages « mode stable » de yue2.cpp (calculs en FP32) : inutiles pour llama.cpp, qui les hériterait et
    // tournerait plus lentement (1.0.28). Retirés le temps du lancement, puis remis.
    static const wchar_t * stable_vars[] = { L"GGML_VK_DISABLE_F16", L"GGML_VK_DISABLE_INTEGER_DOT_PRODUCT", L"GGML_VK_DISABLE_MMVQ" };
    std::wstring           saved[3];
    bool                   had[3] = {};
    for (int i = 0; i < 3; i++) {
        wchar_t buf[64];
        DWORD   n = GetEnvironmentVariableW(stable_vars[i], buf, 64);
        had[i]    = n > 0 && n < 64;
        if (had[i]) saved[i] = buf;
        SetEnvironmentVariableW(stable_vars[i], nullptr);
    }
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                             llm.server.parent_path().c_str(), &si, &pi);
    for (int i = 0; i < 3; i++) {
        if (had[i]) SetEnvironmentVariableW(stable_vars[i], saved[i].c_str());
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) {
        r.error = N_("Le moteur IA local n'a pas pu démarrer.");
        return r;
    }
    auto stop = [&] {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    };
    // Chargement du modèle : on attend que /health réponde 200 (jusqu'à 5 minutes depuis un disque lent).
    std::string base = "http://127.0.0.1:" + std::to_string(port);
    bool        ready = false;
    for (int i = 0; i < 600 && !ready; i++) {
        if (cancel && *cancel) {
            stop();
            r.error = N_("Demande annulée.");
            return r;
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            stop();
            r.error     = N_("Le moteur IA local s'est arrêté pendant le chargement du modèle (détails : %s).");
            r.error_arg = narrow(log.wstring());
            return r;
        }
        std::string body;
        ready = http_request("GET", base + "/health", {}, "", body, 2000) == 200;
        if (!ready) Sleep(500);
    }
    if (!ready) {
        stop();
        r.error = N_("Le modèle met trop de temps à se charger.");
        return r;
    }
    if (stage) *stage = 1;
    // Annulation pendant l'écriture : on arrête le serveur, ce qui coupe la requête en cours.
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
    AiEndpoint ep;
    ep.base_url = base + "/v1";
    ep.model    = narrow(model.filename().wstring());
    AiAsk ask = [&](const std::string & system, const std::string & user) {
        AiCallResult a;
        if (cancel && *cancel) {
            a.error = N_("Demande annulée.");
            return a;
        }
        // Qwen3 réfléchit longuement avant de répondre si on ne le lui interdit pas.
        std::string u = user;
        if (ep.model.find("Qwen3") != std::string::npos) u += "\n/no_think";
        a = ai_chat(ep, system, u, 15 * 60 * 1000);  // processeur : plusieurs minutes possibles
        if (cancel && *cancel) {
            a       = AiCallResult{};
            a.error = N_("Demande annulée.");
        }
        return a;
    };
    work(ask);
    finished = true;
    watch.join();
    stop();
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------- Consignes

std::string ai_system_prompt(bool instrumental) {
    std::string p =
        "You are a professional songwriter. You write songs for YuE, an AI music generator that reads a list of style tags "
        "and lyrics split into sections.\n"
        "Reply with ONE JSON object and nothing else: no markdown, no code fence, no comment before or after.\n"
        "The object has exactly these keys:\n"
        "- \"title\": the song title, in the language of the lyrics.\n"
        "- \"language\": the language of the lyrics, as an English word (for example \"French\").\n"
        "- \"style\": 6 to 12 short music tags in English, separated by commas: genre, sub-genre, mood, main instruments, "
        "vocal type (for example \"male vocals\"), and the tempo as \"<number> BPM\". Never put an artist, band or song name in the tags.\n";
    if (instrumental) {
        p += "- \"lyrics\": the structure only, one section tag per line, in lower case, chosen from: [intro], [verse], [pre-chorus], "
             "[chorus], [bridge], [outro]. No other text at all: no words to sing.\n"
             "This is an INSTRUMENTAL track: the \"style\" must contain the tags \"instrumental\" and \"no vocals\", and no vocal tag at all "
             "(no singer, voice, choir, rap, humming or vocal type).\n";
    } else {
        p += "- \"lyrics\": the full lyrics, lines separated by \\n.\n"
             "Rules for the lyrics:\n"
             "1. A section starts with a line that contains only its tag, chosen from: [Intro], [Verse], [Pre-Chorus], [Chorus], [Bridge], "
             "[Interlude], [Outro].\n"
             "2. A tag contains only the section name: never a number, a colon, a dash, a description or an instruction inside or after it "
             "(write [Verse], not [Verse 1] nor [Verse - soft] nor [Guitar Solo: fast]).\n"
             "3. [Intro], [Interlude] and [Outro] are instrumental: they stay EMPTY, with no line below them. Use [Interlude] for any guitar "
             "solo or instrumental break, and never describe the solo, the instruments or the playing anywhere.\n"
             "4. Under the other tags, write only the sung lines: no stage directions, no parentheses describing the music, no asterisks, "
             "no notes, no speaker names.\n"
             "5. Leave one empty line between sections. Verses have 4 to 8 lines, choruses 4 to 6 lines and are repeated identically.\n"
             "6. Write original lyrics that are easy to sing, with rhymes and a natural rhythm in the chosen language. Never copy existing "
             "lyrics of the artist.\n";
    }
    return p;
}

// trim est défini plus bas (lecture des réponses) : petite copie pour les consignes.
static std::string trim_text(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\r\n,"), b = s.find_last_not_of(" \t\r\n,");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string ai_user_prompt(const AiRequest & r) {
    std::string lang = r.language.empty() ? "English" : r.language;
    std::string p    = r.instrumental ? "Create an instrumental track" : "Create a song in " + lang;
    if (!r.influence.empty()) p += " inspired by the band or singer \"" + r.influence + "\"";
    if (!r.theme.empty()) p += (r.instrumental ? " evoking the theme \"" : " on the theme \"") + r.theme + "\"";
    p += ".";
    if (!r.mood.empty()) p += "\nMood: " + r.mood + ".";
    if (!r.title.empty()) p += "\nUse exactly this title: \"" + r.title + "\".";
    // 1.0.49 : la durée réelle suit la quantité de paroles (le modèle s'arrête à la fin du texte) : chaque longueur
    // impose aussi un nombre de lignes, sinon l'IA écrit des couplets courts et tout dure environ 3 minutes.
    static const char * lengths[] = {
        "Length: short (about 2 to 3 minutes): intro, verse, chorus, verse, chorus, outro. Verses of 4 lines, choruses of 4 lines (about 20 lines of lyrics in total).",
        "Length: normal (about 3 to 4 minutes): intro, verse, pre-chorus, chorus, verse, pre-chorus, chorus, bridge, chorus, outro. Verses of 8 lines, pre-choruses of 2 lines, choruses of 4 to 6 lines, bridge of 4 lines (about 40 lines of lyrics in total).",
        "Length: long (about 5 to 6 minutes): intro, verse, pre-chorus, chorus, verse, pre-chorus, chorus, interlude, verse, pre-chorus, chorus, bridge, chorus, chorus, outro. Verses of 8 lines, pre-choruses of 2 to 4 lines, choruses of 6 lines, bridge of 4 to 6 lines (about 65 lines of lyrics in total).",
        "Length: very long (about 9 to 11 minutes, it will be generated in three chained parts): intro, then FIVE verse / pre-chorus / chorus groups, two bridges, interludes between the groups, a final double chorus and an outro. Verses of 8 lines, pre-choruses of 4 lines, choruses of 6 lines, bridges of 6 lines (about 110 to 130 lines of lyrics in total, the same amount in the first and second half).",
    };
    static const char * inst_lengths[] = {
        "Length: short (about 2 to 3 minutes): about 6 sections.",
        "Length: normal (about 3 to 4 minutes): about 9 sections.",
        "Length: long (about 5 to 6 minutes): about 14 sections.",
        "Length: very long (about 9 to 11 minutes, it will be generated in three chained parts): about 24 sections, with a clear development in the second half.",
    };
    p += "\n" + std::string((r.instrumental ? inst_lengths : lengths)[std::clamp(r.length, 0, 3)]);
    if (!r.influence.empty()) p += "\nThe style tags describe the sound of " + r.influence + " with generic words only, without the name.";
    if (!r.album_context.empty()) p += "\n" + r.album_context;
    if (!r.extra.empty()) p += "\nOther wishes: " + r.extra;
    // Anti-répétition (1.0.33) : vocabulaire et titres neufs.
    if (!r.instrumental) {
        p += "\nUse fresh, concrete and surprising images; avoid the usual clichés of AI-written songs.";
        if (!r.avoid.empty()) {
            p += "\nNever use these words, nor their plural, in the title or the lyrics: ";
            for (size_t i = 0; i < r.avoid.size(); i++) p += (i ? ", " : "") + r.avoid[i];
            p += ".";
        }
    } else if (!r.avoid.empty()) {  // 1.0.34 : le titre d'un instrumental aussi
        p += "\nNever use these words, nor their plural, in the title: ";
        for (size_t i = 0; i < r.avoid.size(); i++) p += (i ? ", " : "") + r.avoid[i];
        p += ".";
    }
    if (!trim_text(r.negative).empty()) p += "\nNever use these styles or sounds in the style tags, and do not write music that sounds like them: " + trim_text(r.negative) + ".";
    if (!r.instrumental && !r.used_lines.empty()) {  // 1.0.73 : 40 dernières lignes de l'album, pour la longueur de la consigne
        p += "\nEvery line must be new. These lines were already sung in earlier songs of the album: never reuse them, nor their images:";
        for (size_t i = r.used_lines.size() > 40 ? r.used_lines.size() - 40 : 0; i < r.used_lines.size(); i++) p += "\n- " + r.used_lines[i];
    }
    if (!r.used_titles.empty()) {
        p += "\nThe title must be different from these titles, and must not reuse their main words: ";
        for (size_t i = 0; i < r.used_titles.size(); i++) p += (i ? "; " : "") + ("\"" + r.used_titles[i] + "\"");
        p += ".";
    }
    p += "\nAnswer with the JSON object only.";
    return p;
}


std::string ai_retry_prompt() {
    return "Your previous answer could not be read. Answer again with ONE valid JSON object only, with the keys \"title\", \"language\", "
           "\"style\" and \"lyrics\", and nothing before or after it.";
}

// ---------------------------------------------------------------------------- Lecture et nettoyage

static std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::string lower_ascii(std::string s) {
    for (char & c : s) c = (char) tolower((unsigned char) c);
    return s;
}

static std::vector<std::string> lines_of(const std::string & text) {
    std::vector<std::string> out;
    size_t                   pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        std::string l = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

// Section YuE2 correspondant à un nom donné par l'IA (« Verse 2 », « Guitar Solo - fast », « Refrain »…) ; "" si inconnu.
static std::string canon_section(const std::string & raw) {
    std::string t = lower_ascii(raw);
    // On regarde d'abord le début (avant « : », « - », « | », « ( »…), puis tout le texte.
    size_t      cut  = t.find_first_of(":|(,[{");
    size_t      dash = t.find(" - ");
    if (dash != std::string::npos && (cut == std::string::npos || dash < cut)) cut = dash;
    std::string head = trim(cut == std::string::npos ? t : t.substr(0, cut));
    for (const std::string & s : { head, t }) {
        auto has = [&](const char * w) { return s.find(w) != std::string::npos; };
        if (has("pre-chorus") || has("pre chorus") || has("prechorus") || has("pre-refrain") || has("pr\xc3\xa9-refrain") || has("pre-hook")) return "Pre-Chorus";
        if (has("chorus") || has("refrain") || has("hook")) return "Chorus";
        if (has("verse") || has("couplet") || has("strophe")) return "Verse";
        if (has("bridge") || has("pont")) return "Bridge";
        if (has("intro")) return "Intro";
        if (has("outro") || has("ending") || has("coda") || has("fade") || s == "end" || has("final")) return "Outro";
        if (has("solo") || has("interlude") || has("instrumental") || has("break") || has("drop") || has("riff")) return "Interlude";
    }
    return "";
}

// Ligne de titre de section ? Renvoie le nom YuE2, sinon "".
// Formes reconnues : « [Verse 1] », « **[Chorus]** », « (Chorus) », « ## Verse », « Verse 2: », « **Bridge** ».
static std::string section_of_line(const std::string & line, bool & bracketed) {
    std::string s = trim(line);
    bracketed     = false;
    if (s.empty()) return "";
    bool marked = false;
    while (!s.empty() && (s[0] == '#' || s[0] == '*' || s[0] == '_')) {
        s.erase(0, 1);
        marked = true;
    }
    while (!s.empty() && (s.back() == '*' || s.back() == '_')) {
        s.pop_back();
        marked = true;
    }
    s = trim(s);
    if (s.size() >= 2 && ((s.front() == '[' && s.back() == ']') || (s.front() == '(' && s.back() == ')') || (s.front() == '{' && s.back() == '}'))) {
        bracketed = true;
        s         = trim(s.substr(1, s.size() - 2));
    } else if (!s.empty() && s.back() == ':') {
        marked = true;
        s.pop_back();
    }
    if (!bracketed && !marked) return "";
    std::string c = canon_section(s);
    if (c.empty()) return "";
    // Un titre de section est court ; « (Chorus) » au milieu d'une phrase n'en est pas un.
    if (!bracketed && std::count(s.begin(), s.end(), ' ') > 3) return "";
    return c;
}

// Retire les marques de mise en forme et les indications d'une ligne chantée. Renvoie "" si la ligne entière
// est une indication (« (guitar riff) », « *whispered* », « [softly] », « Note: … »).
static std::string clean_sung_line(const std::string & raw, int & removed) {
    std::string s = trim(raw);
    if (s.empty()) return "";
    if (s.rfind("- ", 0) == 0 || s.rfind("• ", 0) == 0) s = trim(s.substr(s[0] == '-' ? 2 : 4));
    std::string low = lower_ascii(s);
    for (const char * w : { "note:", "notes:", "n.b.", "(note", "explanation", "here is", "here's", "voici" }) {
        if (low.rfind(w, 0) == 0) {
            removed++;
            return "";
        }
    }
    auto wrapped = [](const std::string & t, char a, char b) { return t.size() >= 2 && t.front() == a && t.back() == b; };
    if (wrapped(s, '(', ')') || wrapped(s, '[', ']') || wrapped(s, '{', '}') || wrapped(s, '*', '*') || wrapped(s, '_', '_')) {
        removed++;
        return "";
    }
    // Indications entre astérisques ou crochets au milieu de la ligne : retirées.
    for (char open : { '*', '[' }) {
        char   close = open == '*' ? '*' : ']';
        size_t a;
        while ((a = s.find(open)) != std::string::npos) {
            size_t b = s.find(close, a + 1);
            if (b == std::string::npos) {
                s.erase(a, 1);
                continue;
            }
            s.erase(a, b - a + 1);
            removed++;
        }
    }
    // Répétitions notées « (x2) », « (×2) », « (repeat) » en fin de ligne.
    size_t p = s.rfind('(');
    if (p != std::string::npos && s.back() == ')') {
        std::string in = lower_ascii(s.substr(p + 1, s.size() - p - 2));
        if (in.rfind("x", 0) == 0 || in.rfind("\xc3\x97", 0) == 0 || in.find("repeat") != std::string::npos || in.find("bis") == 0) {
            s.erase(p);
            removed++;
        }
    }
    s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
    return trim(s);
}

// Structure d'un instrumental écrite librement (1.0.28) : « intro, verse, chorus », « [intro] [verse] », « 1. Intro »,
// « Intro → Verse → Chorus »… Chaque morceau court qui nomme une section devient une balise en minuscules.
static std::string instrumental_structure(const std::string & text, int & removed) {
    std::string out, cur;
    auto flush = [&] {
        std::string t = trim(cur);
        cur.clear();
        // Numéros, puces et marques retirés : « 1. Intro », « - verse », « **chorus** ».
        while (!t.empty() && (isdigit((unsigned char) t[0]) || strchr(".)-*#_\"' \t", t[0]))) t.erase(0, 1);
        while (!t.empty() && strchr(".*_\"' \t:", t.back())) t.pop_back();
        if (t.empty()) return;
        if (std::count(t.begin(), t.end(), ' ') > 3) {  // une phrase n'est pas un nom de section
            removed++;
            return;
        }
        std::string sec = canon_section(t);
        if (sec.empty()) {
            removed++;
            return;
        }
        std::string tag = lower_ascii(sec);
        if (tag == "interlude") tag = "bridge";  // la LoRA instrumentale ne connaît pas [interlude]
        out += "[" + tag + "]\n";
    };
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        // Séparateurs : retour à la ligne, virgule, point-virgule, barre, crochets, flèches « -> » et « → ».
        if (c == '\n' || c == '\r' || c == ',' || c == ';' || c == '|' || c == '[' || c == ']' || c == '(' || c == ')') flush();
        else if (c == '-' && i + 1 < text.size() && text[i + 1] == '>') {
            flush();
            i++;
        } else if ((unsigned char) c == 0xE2 && i + 2 < text.size() && (unsigned char) text[i + 1] == 0x86 && (unsigned char) text[i + 2] == 0x92) {
            flush();
            i += 2;
        } else cur += c;
    }
    flush();
    return out;
}

std::string ai_clean_lyrics(const std::string & lyrics, bool instrumental, int & removed) {
    if (instrumental) return instrumental_structure(lyrics, removed);
    struct Section {
        std::string              name;
        std::vector<std::string> lines;
    };
    std::vector<Section> sections;
    for (const std::string & raw : lines_of(lyrics)) {
        bool        bracketed = false;
        std::string sec       = section_of_line(raw, bracketed);
        if (!sec.empty()) {
            std::string t = trim(raw);
            // Balise décrite (« [Verse 1] », « [Guitar Solo: …] ») : ramenée au seul nom de section.
            if (t != "[" + sec + "]") removed++;
            sections.push_back({ sec, {} });
            continue;
        }
        std::string t = trim(raw);
        if (t.empty()) continue;
        if (sections.empty()) {  // texte avant la première section (présentation, titre…) : ignoré
            removed++;
            continue;
        }
        Section & cur = sections.back();
        if (instrumental || cur.name == "Intro" || cur.name == "Interlude" || cur.name == "Outro") {
            removed++;  // rien sous une balise instrumentale (description de solo, d'instruments…)
            continue;
        }
        std::string sung = clean_sung_line(t, removed);
        if (!sung.empty()) cur.lines.push_back(sung);
    }
    std::string out;
    for (const Section & s : sections) {
        if (instrumental) {
            std::string tag = lower_ascii(s.name);
            if (tag == "interlude") tag = "bridge";  // la LoRA instrumentale ne connaît pas [interlude]
            out += "[" + tag + "]\n";
        } else {
            out += "[" + s.name + "]\n";
            for (const std::string & l : s.lines) out += l + "\n";
            out += "\n";
        }
    }
    return out;
}

std::string ai_clean_style(const std::string & style, int & removed) {
    std::vector<std::string> out;
    std::string              cur;
    auto flush = [&] {
        std::string t = trim(cur);
        cur.clear();
        while (!t.empty() && (t[0] == '#' || t[0] == '"' || t[0] == '\'' || t[0] == '-' || t[0] == '*')) t = trim(t.substr(1));
        while (!t.empty() && (t.back() == '.' || t.back() == '"' || t.back() == '\'' || t.back() == '*')) t.pop_back();
        t = trim(t);
        if (t.empty()) return;
        // Une balise est courte : une phrase ou « Tempo: 120 » n'en est pas une.
        if (t.find(':') != std::string::npos || std::count(t.begin(), t.end(), ' ') > 4 || t.size() > 40) {
            removed++;
            return;
        }
        std::string low = lower_ascii(t);
        // « 120 bpm » → « 120 BPM » (forme attendue par le curseur de tempo du Studio)
        size_t b = low.find(" bpm");
        if (b != std::string::npos && b > 0 && isdigit((unsigned char) low[0])) low = low.substr(0, b) + " BPM";
        for (const std::string & o : out) {
            if (lower_ascii(o) == lower_ascii(low)) return;
        }
        out.push_back(low);
    };
    for (char c : style) {
        if (c == ',' || c == ';' || c == '\n' || c == '|') flush();
        else cur += c;
    }
    flush();
    std::string joined;
    for (const std::string & t : out) joined += (joined.empty() ? "" : ", ") + t;
    return joined;
}

// Rend lisible un JSON écrit par une IA : retours à la ligne bruts dans les chaînes remplacés par \n.
static std::string repair_json(const std::string & s) {
    std::string out;
    bool        in_str = false, esc = false;
    for (char c : s) {
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            else if (c == '\n') {
                out += "\\n";
                continue;
            } else if (c == '\r') continue;
            else if (c == '\t') {
                out += "\\t";
                continue;
            }
        } else if (c == '"') in_str = true;
        out += c;
    }
    return out;
}

static std::string json_text(yyjson_val * obj, const char * key) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    if (yyjson_is_str(v)) return yyjson_get_str(v);
    std::string  joined;  // liste de chaînes : lignes (paroles) ou balises (style)
    size_t       i, n;
    yyjson_val * item;
    yyjson_arr_foreach(v, i, n, item) {
        if (yyjson_is_str(item)) joined += (joined.empty() ? "" : (strcmp(key, "lyrics") == 0 ? "\n" : ", ")) + std::string(yyjson_get_str(item));
    }
    return joined;
}

AiSong ai_parse_song(const std::string & raw, bool instrumental) {
    AiSong      song;
    std::string text = raw;
    // Réflexion affichée par certains modèles (<think>…</think>) : retirée.
    size_t th;
    while ((th = text.find("<think>")) != std::string::npos) {
        size_t e = text.find("</think>", th);
        text.erase(th, e == std::string::npos ? std::string::npos : e + 8 - th);
    }
    size_t a = text.find('{'), b = text.rfind('}');
    if (a != std::string::npos && b != std::string::npos && b > a) {
        std::string  json = repair_json(text.substr(a, b - a + 1));
        yyjson_doc * doc  = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS);
        if (doc) {
            yyjson_val * r = yyjson_doc_get_root(doc);
            song.title     = json_text(r, "title");
            song.language  = json_text(r, "language");
            song.style     = json_text(r, "style");
            song.lyrics    = json_text(r, "lyrics");
            if (song.lyrics.empty()) song.lyrics = json_text(r, "structure");
            yyjson_doc_free(doc);
        }
    }
    if (song.lyrics.empty()) {  // pas de JSON lisible : réponse en texte (« Title: … », « Style: … », puis les sections)
        bool in_lyrics = false;
        for (const std::string & l : lines_of(text)) {
            std::string t = trim(l), low = lower_ascii(t);
            while (!low.empty() && (low[0] == '*' || low[0] == '#')) {
                low.erase(0, 1);
                t.erase(0, 1);
            }
            auto value = [&](const std::string & s) { return trim(s.substr(s.find(':') + 1)); };
            if (!in_lyrics && (low.rfind("title", 0) == 0 || low.rfind("titre", 0) == 0) && t.find(':') != std::string::npos) song.title = value(t);
            else if (!in_lyrics && low.rfind("style", 0) == 0 && t.find(':') != std::string::npos) song.style = value(t);
            else if (!in_lyrics && (low.rfind("language", 0) == 0 || low.rfind("langue", 0) == 0) && t.find(':') != std::string::npos) song.language = value(t);
            else if (!in_lyrics && (low.rfind("structure", 0) == 0 || low.rfind("lyrics", 0) == 0 || low.rfind("paroles", 0) == 0) &&
                     t.find(':') != std::string::npos) {
                in_lyrics = true;  // « Structure : intro, verse, chorus » ou « Paroles : » suivi des sections
                song.lyrics += value(t) + "\n";
            }
            else {
                bool br = false;
                if (!section_of_line(l, br).empty()) in_lyrics = true;
                if (in_lyrics) song.lyrics += l + "\n";
            }
        }
    }
    for (char q : { '"', '*' }) {
        song.title.erase(std::remove(song.title.begin(), song.title.end(), q), song.title.end());
    }
    song.title  = trim(song.title);
    song.style  = ai_clean_style(song.style, song.removed);
    song.lyrics = ai_clean_lyrics(song.lyrics, instrumental, song.removed);
    // Instrumental sans structure lisible : structure normale proposée, pour que le morceau parte quand même
    // dans « Nouveau morceau » (on peut la modifier ensuite).
    if (instrumental && song.lyrics.empty() && !song.style.empty()) {
        song.lyrics = "[intro]\n[verse]\n[chorus]\n[verse]\n[chorus]\n[bridge]\n[chorus]\n[outro]\n";
        song.removed++;
    }
    song.ok     = !song.lyrics.empty() && !song.style.empty();
    return song;
}

// ---------------------------------------------------------------------------- Anti-répétition (1.0.33)

std::vector<std::string> ai_default_avoid_words() {
    return { "nuit",    "cœur",   "âme",    "rêve",    "lumière", "feu",    "étoile", "ombre",  "flamme", "éternité",
             "destin",  "silence", "horizon", "night",  "heart",   "soul",   "dream",  "light",  "fire",   "star",
             "shadow",  "forever", "destiny", "echo",   "flame",   "whisper" };
}

// Minuscules et lettres sans accent (UTF-8 : lettres latines accentuées les plus courantes ; « œ » → « oe »).
static std::string fold_word(const std::string & s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char) s[i];
        if (c < 0x80) {
            out += (char) tolower(c);
            continue;
        }
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char raw = (unsigned char) s[++i];
            if (raw == 0x9F) {  // ß
                out += "ss";
                continue;
            }
            unsigned char d = raw | 0x20;  // majuscules C3 80-9E → minuscules C3 A0-BE
            const char *  m = nullptr;
            if (d >= 0xA0 && d <= 0xA5) m = "a";
            else if (d == 0xA6) m = "ae";
            else if (d == 0xA7) m = "c";
            else if (d >= 0xA8 && d <= 0xAB) m = "e";
            else if (d >= 0xAC && d <= 0xAF) m = "i";
            else if (d == 0xB1) m = "n";
            else if ((d >= 0xB2 && d <= 0xB6) || d == 0xB8) m = "o";
            else if (d >= 0xB9 && d <= 0xBC) m = "u";
            else if (d == 0xBD || d == 0xBF) m = "y";
            out += m ? m : "?";
            continue;
        }
        if (c == 0xC5 && i + 1 < s.size() && ((unsigned char) s[i + 1] == 0x93 || (unsigned char) s[i + 1] == 0x92)) {
            out += "oe";
            i++;
            continue;
        }
        out += (char) c;  // autres écritures (japonais, coréen…) : gardées telles quelles
    }
    return out;
}

// Mots du texte, repliés (fold_word) ; séparateurs = tout ce qui n'est ni lettre ASCII ni octet UTF-8.
static std::vector<std::string> words_of(const std::string & text) {
    std::vector<std::string> out;
    std::string              cur;
    auto                     flush = [&] {
        if (!cur.empty()) out.push_back(fold_word(cur));
        cur.clear();
    };
    for (char ch : text) {
        unsigned char c = (unsigned char) ch;
        if (isalpha(c) || c >= 0x80) cur += ch;
        else flush();
    }
    flush();
    return out;
}

std::vector<std::string> ai_forbidden_found(const std::string & text, const std::vector<std::string> & avoid) {
    std::vector<std::string> words = words_of(text), found;
    for (const std::string & a : avoid) {
        std::string w = fold_word(trim(a));
        if (w.empty()) continue;
        bool hit = false;
        for (const std::string & t : words) {
            if (t == w || t == w + "s" || t == w + "x" || t == w + "es") {
                hit = true;
                break;
            }
        }
        // Expression de plusieurs mots (« forever and ever ») : recherche dans le texte replié.
        if (!hit && w.find(' ') != std::string::npos) hit = fold_word(text).find(w) != std::string::npos;
        if (hit) found.push_back(trim(a));
    }
    return found;
}

std::vector<std::string> ai_frequent_words(const std::vector<std::string> & texts, int min_songs, size_t max_words) {
    // Mots-outils de 4 lettres et plus (français, anglais, espagnol, italien, allemand, portugais), repliés.
    static const char * stop[] = {
        "avec", "pour", "dans", "sans", "sous", "vers", "chez", "mais", "donc", "comme", "quand", "toujours", "jamais", "encore",
        "notre", "votre", "leurs", "nous", "vous", "elle", "elles", "ils", "tout", "tous", "toute", "toutes", "cette", "ceux",
        "celui", "celle", "rien", "plus", "moins", "tres", "bien", "alors", "ainsi", "aussi", "meme", "entre", "apres", "avant",
        "depuis", "pendant", "parce", "suis", "sommes", "etes", "sont", "etait", "serai", "sera", "avoir", "etre", "fait", "faire",
        "peux", "veux", "dire", "voir", "viens", "vais", "allons", "laisse", "encore", "chaque", "quelque", "quoi", "ici",
        "this", "that", "these", "those", "with", "from", "into", "onto", "your", "yours", "their", "there", "where", "when",
        "what", "which", "while", "will", "would", "could", "should", "have", "been", "being", "were", "they", "them", "then",
        "than", "just", "only", "every", "never", "always", "about", "again", "still", "through", "here", "like", "know",
        "come", "make", "take", "gonna", "wanna", "cause", "away", "back", "down", "over", "under", "more", "some", "each",
        "para", "como", "pero", "todo", "toda", "todos", "esta", "este", "estoy", "eres", "somos", "nada", "siempre", "nunca",
        "cuando", "donde", "porque", "tengo", "quiero", "sono", "della", "dello", "nella", "questo", "questa", "quando", "sempre",
        "anche", "come", "nicht", "eine", "einen", "einem", "einer", "ich", "mich", "dich", "sich", "wenn", "dann", "noch",
        "immer", "nach", "durch", "unter", "uber", "sind", "habe", "wird", "mein", "dein", "unsere", "voce", "minha", "nosso",
        "nossa", "sempre", "quando", "porque", "estou", "tudo",
    };
    auto is_stop = [&](const std::string & w) {
        for (const char * s : stop) if (w == s) return true;
        return false;
    };
    std::map<std::string, int> songs;  // mot → nombre de textes où il apparaît
    std::map<std::string, std::string> shown;  // mot replié → première écriture rencontrée (avec accents)
    for (const std::string & text : texts) {
        std::set<std::string> seen;
        // Écritures d'origine, dans le même ordre que words_of
        std::string cur;
        std::vector<std::string> originals;
        for (char ch : text + " ") {
            unsigned char c = (unsigned char) ch;
            if (isalpha(c) || c >= 0x80) cur += ch;
            else if (!cur.empty()) {
                originals.push_back(cur);
                cur.clear();
            }
        }
        for (const std::string & o : originals) {
            std::string w = fold_word(o);
            if (w.size() < 4 || is_stop(w) || !seen.insert(w).second) continue;
            songs[w]++;
            if (!shown.count(w)) {  // affiché comme écrit (minuscules ASCII), accents gardés
                std::string l = o;
                for (char & c : l) c = (char) tolower((unsigned char) c);
                shown[w] = l;
            }
        }
    }
    std::vector<std::pair<int, std::string>> list;
    for (const auto & [w, n] : songs) if (n >= min_songs) list.push_back({ n, w });
    std::sort(list.begin(), list.end(), [](const auto & a, const auto & b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<std::string> out;
    for (const auto & [n, w] : list) {
        if (out.size() >= max_words) break;
        out.push_back(shown[w]);
    }
    return out;
}

// 1.0.70 : nombre de lignes chantées (hors balises de section)
int ai_sung_lines(const std::string & lyrics) {
    std::istringstream in(lyrics);
    int                n = 0;
    for (std::string l; std::getline(in, l);) {
        size_t a = l.find_first_not_of(" \t\r");
        if (a != std::string::npos && l[a] != '[') n++;
    }
    return n;
}

// 1.0.73 : ligne comparée sans majuscules, accents ni ponctuation (texte non latin : la ligne telle quelle)
static std::string fold_line(const std::string & l) {
    std::string f = fold_word(l), out;
    for (char c : f) {
        if (isalnum((unsigned char) c)) out += c;
        else if (c == ' ' && !out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? trim(l) : out;
}

std::vector<std::string> ai_song_lines(const std::string & lyrics) {
    std::vector<std::string> out;
    for (const std::string & l : lines_of(lyrics)) {
        std::string t = trim(l);
        if (!t.empty() && t[0] != '[') out.push_back(t);
    }
    return out;
}

std::set<std::string> ai_known_lines(const std::vector<std::string> & lines) {
    std::set<std::string> known;
    for (const std::string & l : lines) known.insert(fold_line(l));
    return known;
}

int ai_drop_repeated_lines(std::string & lyrics, std::set<std::string> & known, int chorus_keep) {
    struct Block {
        std::string              tag;
        std::vector<std::string> lines;
        bool                     had = false;  // avait des lignes chantées avant le tri
    };
    std::vector<Block> blocks(1);
    for (const std::string & l : lines_of(lyrics)) {
        std::string t = trim(l);
        if (t.empty()) continue;
        if (t[0] == '[') blocks.push_back({ t, {}, false });
        else blocks.back().lines.push_back(t), blocks.back().had = true;
    }
    int         removed = 0;
    std::string out;
    for (Block & b : blocks) {
        const std::string tag    = fold_word(b.tag);
        const bool        repeat = tag.find("chorus") != std::string::npos || tag.find("refrain") != std::string::npos || tag.find("hook") != std::string::npos;
        std::vector<std::string> kept;
        for (size_t i = 0; i < b.lines.size(); i++) {
            const std::string & l     = b.lines[i];
            const bool          fresh = known.insert(fold_line(l)).second;
            if (fresh || (repeat && (chorus_keep < 0 || (int) i < chorus_keep))) kept.push_back(l);
            else removed++;
        }
        if (b.tag.empty() && kept.empty()) continue;
        if (b.had && kept.empty()) continue;  // section vidée : retirée avec sa balise
        if (!out.empty()) out += "\n\n";
        out += b.tag;
        for (const std::string & l : kept) out += (out.empty() ? "" : "\n") + l;
    }
    if (removed) lyrics = out;
    return removed;
}

// 1.0.70 : l'IA locale s'arrête vers 50 lignes quand on lui en demande 120 : une ligne chantée dure ≈ 6,5 s, soit 5 à 6 min
// au lieu de 9. On lui demande la suite (6 fois au plus), insérée avant la balise de fin.
void ai_lengthen_song(AiSong & song, int target, const std::vector<std::string> & avoid, const AiAsk & ask, const std::atomic<bool> * cancel,
                      const std::vector<std::string> & used_lines) {
    std::set<std::string> known = ai_known_lines(used_lines);
    for (const std::string & l : ai_song_lines(song.lyrics)) known.insert(fold_line(l));
    auto without_outro = [](std::string text) {
        size_t last = text.find_last_not_of(" \t\r\n");
        text.resize(last == std::string::npos ? 0 : last + 1);
        size_t start = text.rfind('\n');
        start        = start == std::string::npos ? 0 : start + 1;
        std::string tail = text.substr(start);
        for (char & c : tail) c = (char) tolower((unsigned char) c);
        if (tail.rfind("[outro", 0) == 0) text.resize(start);
        last = text.find_last_not_of(" \t\r\n");
        text.resize(last == std::string::npos ? 0 : last + 1);
        return text;
    };
    for (int attempt = 0; attempt < 6 && song.ok && !(cancel && *cancel); attempt++) {
        const int have = ai_sung_lines(song.lyrics);
        if (have >= target) return;
        std::string u = "Here is a song in progress, titled \"" + song.title + "\":\n\n" + without_outro(song.lyrics) +
                        "\n\nThe song is not finished: write its continuation, in the same language and the same story, with about " +
                        std::to_string(std::max(30, target - have)) +
                        " more sung lines: new verses with new words, a bridge, then the chorus again. Use the same section tags. Do not write an outro. "
                        "Every verse line must be new: never reuse a line written above. When the chorus comes back, keep its first two lines and "
                        "write new words for the others.";
        if (!avoid.empty()) {
            u += " Never use these words: ";
            for (size_t i = 0; i < avoid.size(); i++) u += (i ? ", " : "") + avoid[i];
            u += ".";
        }
        u += "\n\nReply with the new sections only, as plain text with their [Section] tags: no JSON, no comment.";
        AiCallResult r = ask("You are a songwriter. You only answer with a JSON object.", u);
        if (!r.ok) return;
        int         removed = 0;
        std::string more    = ai_clean_lyrics(r.text, false, removed);
        if (more.find('{') != std::string::npos) continue;
        ai_drop_repeated_lines(more, known, 2);  // 1.0.73 : lignes recopiées retirées ; la boucle redemande ce qui manque
        if (ai_sung_lines(more) < 4) continue;
        song.lyrics = without_outro(song.lyrics) + "\n\n" + without_outro(more) + "\n\n[Outro]";
    }
}

std::string ai_avoid_retry_prompt(const std::vector<std::string> & found) {
    std::string p = "Your song uses forbidden words: ";
    for (size_t i = 0; i < found.size(); i++) p += (i ? ", " : "") + found[i];
    p += ". Write the whole song again, with a new title, without any of these words, and answer with the JSON object only.";
    return p;
}

// ---------------------------------------------------------------------------- Album (1.0.33)

std::string ai_album_system_prompt() {
    return "You are a music producer who plans complete albums for YuE, an AI music generator.\n"
           "Reply with ONE JSON object and nothing else: no markdown, no code fence, no comment before or after.\n"
           "The object has exactly these keys:\n"
           "- \"band\": the name of the band (an original name, never the name of an existing artist).\n"
           "- \"album\": the album title.\n"
           "- \"sound\": 5 to 8 short music tags in English, separated by commas, describing the sound shared by the whole album: "
           "genre, sub-genre, main instruments, vocal type. No artist name, no tempo.\n"
           "- \"tracks\": an array with one object per track, in album order, each with the keys \"title\" (in the language of the "
           "songs), \"theme\" (one short sentence in English: what this track is about), \"mood\" (one or two English words), "
           "\"bpm\" (a number) and \"instrumental\" (true or false).\n"
           "- \"cover_prompt\": one or two English sentences describing the album cover picture (subject, composition, colours, "
           "lighting, mood), for an image generator. No text, no letters, no logo, no band name in the picture.\n"
           "Every track has its own subject, mood and tempo: vary them across the album (slow, mid-tempo and fast tracks; "
           "light and dark moods). Titles are all different and do not repeat the same words.";
}

std::string ai_album_user_prompt(const AiAlbumRequest & r) {
    int         n = std::clamp(r.count, 1, 50);
    std::string p = "Plan an album of " + std::to_string(n) + (n > 1 ? " tracks" : " track");
    if (r.kind == 1) p += ", all instrumental";
    else p += " with songs in " + (r.language.empty() ? std::string("English") : r.language);
    if (!r.influence.empty()) p += ", inspired by the band or singer \"" + r.influence + "\"";
    p += ".";
    if (!r.band.empty()) p += "\nThe band name is exactly \"" + r.band + "\".";
    else if (!r.influence.empty()) p += "\nInvent an original band name that fits this style (not \"" + r.influence + "\").";
    else p += "\nInvent an original band name.";
    if (!r.album.empty()) p += "\nThe album title is exactly \"" + r.album + "\".";
    if (!r.theme.empty()) p += "\nAlbum concept: " + r.theme + ".";
    if (!r.mood.empty()) p += "\nOverall mood: " + r.mood + ".";
    if (r.kind == 0) p += "\nAll tracks are sung (\"instrumental\": false).";
    else if (r.kind == 1) p += "\nAll tracks are instrumental (\"instrumental\": true).";
    else if (std::min(r.instrumentals, r.max_instrumentals) > 0)
        p += "\nExactly " + std::to_string(std::min({ r.instrumentals, r.max_instrumentals, n })) + " of the tracks are instrumental, the others are sung.";
    else p += "\nAll tracks are sung (\"instrumental\": false).";
    if (!r.influence.empty()) p += "\nThe \"sound\" tags describe the sound of " + r.influence + " with generic words only, without the name.";
    if (!r.avoid.empty()) {  // 1.0.34 : aussi dans le nom du groupe et le titre de l'album (et pour les instrumentaux)
        p += "\nNever use these words, nor their plural, in the band name, the album title or the track titles: ";
        for (size_t i = 0; i < r.avoid.size(); i++) p += (i ? ", " : "") + r.avoid[i];
        p += ".";
    }
    if (!trim(r.negative).empty()) p += "\nNever use these styles or sounds in the \"sound\" tags: " + trim(r.negative) + ".";
    // 1.0.47 : album à compléter ou groupe déjà connu
    if (r.complete) p += "\nThese tracks are NEW tracks added to the existing album \"" + r.album + "\": keep its sound and its concept.";
    if (!trim(r.sound).empty()) p += "\nThe \"sound\" of every track is: " + trim(r.sound) + " (keep it, you may vary tempo and mood).";
    if (!r.used_albums.empty() && !r.complete) {
        p += "\nThe band already released these albums, choose a different album title: ";
        for (size_t i = 0; i < r.used_albums.size(); i++) p += (i ? ", " : "") + ("\"" + r.used_albums[i] + "\"");
        p += ".";
    }
    if (!r.used_titles.empty()) {
        p += "\nThese track titles already exist, never reuse them nor close variants: ";
        for (size_t i = 0; i < r.used_titles.size() && i < 200; i++) p += (i ? ", " : "") + ("\"" + r.used_titles[i] + "\"");
        p += ".";
    }
    if (!r.extra.empty()) p += "\nOther wishes: " + r.extra;
    p += "\nThe \"tracks\" array has exactly " + std::to_string(n) + " objects. Answer with the JSON object only.";
    return p;
}

AiAlbumPlan ai_parse_album_plan(const std::string & raw, const AiAlbumRequest & r) {
    AiAlbumPlan plan;
    std::string text = raw;
    size_t      th;
    while ((th = text.find("<think>")) != std::string::npos) {
        size_t e = text.find("</think>", th);
        text.erase(th, e == std::string::npos ? std::string::npos : e + 8 - th);
    }
    const int n = std::clamp(r.count, 1, 50);
    size_t    a = text.find('{'), b = text.rfind('}');
    if (a != std::string::npos && b != std::string::npos && b > a) {
        std::string  json = repair_json(text.substr(a, b - a + 1));
        yyjson_doc * doc  = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS);
        if (doc) {
            yyjson_val * root = yyjson_doc_get_root(doc);
            plan.band         = trim(json_text(root, "band"));
            plan.album        = trim(json_text(root, "album"));
            plan.sound        = json_text(root, "sound");
            plan.cover_prompt = trim(json_text(root, "cover_prompt"));
            size_t       i, m;
            yyjson_val * t;
            yyjson_arr_foreach(yyjson_obj_get(root, "tracks"), i, m, t) {
                AiTrackPlan tp;
                if (yyjson_is_str(t)) tp.title = yyjson_get_str(t);  // simple liste de titres
                else {
                    tp.title = json_text(t, "title");
                    tp.theme = json_text(t, "theme");
                    tp.mood  = json_text(t, "mood");
                    yyjson_val * bpm = yyjson_obj_get(t, "bpm");
                    if (yyjson_is_num(bpm)) tp.bpm = (int) yyjson_get_num(bpm);
                    else if (yyjson_is_str(bpm)) tp.bpm = atoi(yyjson_get_str(bpm));
                    yyjson_val * inst = yyjson_obj_get(t, "instrumental");
                    tp.instrumental = yyjson_is_true(inst) || (yyjson_is_str(inst) && lower_ascii(yyjson_get_str(inst)) == "true");
                }
                for (char q : { '"', '*' }) tp.title.erase(std::remove(tp.title.begin(), tp.title.end(), q), tp.title.end());
                tp.title = trim(tp.title);
                tp.bpm   = tp.bpm >= 50 && tp.bpm <= 220 ? tp.bpm : 0;
                plan.tracks.push_back(tp);
            }
            yyjson_doc_free(doc);
        }
    }
    if (plan.tracks.empty() && plan.band.empty()) return plan;  // rien de lisible : ok = false
    int removed = 0;
    plan.sound  = ai_clean_style(plan.sound, removed);
    // Choix imposés par l'utilisateur, puis nombre exact de pistes.
    if (!r.band.empty()) plan.band = r.band;
    if (!r.album.empty()) plan.album = r.album;
    if (!trim(r.sound).empty()) plan.sound = trim(r.sound);  // 1.0.47 : son de l'album ou du groupe gardé tel quel
    if (plan.band.empty()) plan.band = "Studio Audio AI";  // 1.0.85 : jamais un nom réel
    if (plan.album.empty()) plan.album = plan.band;
    if ((int) plan.tracks.size() > n) plan.tracks.resize(n);
    while ((int) plan.tracks.size() < n) plan.tracks.push_back(AiTrackPlan{});
    // Répartition des instrumentaux : tout chanté, tout instrumental, ou mélangé (nombre demandé, sinon celui de l'IA).
    if (r.kind == 0 || r.kind == 1) {
        for (AiTrackPlan & tp : plan.tracks) tp.instrumental = r.kind == 1;
    } else {
        // Mélangé : le nombre demandé, max_instrumentals au plus (1.0.35, Réglages › IA) ; 1.0.70 : 0 = aucun.
        int have = 0;
        for (AiTrackPlan & tp : plan.tracks) have += tp.instrumental ? 1 : 0;
        int cap  = std::clamp(r.max_instrumentals, 0, n);
        int want = std::min(r.instrumentals, cap);
        have     = 0;
        for (AiTrackPlan & tp : plan.tracks) have += tp.instrumental ? 1 : 0;
        // Trop : on garde les premiers ; pas assez : on en ajoute, répartis sur l'album (jamais la première piste).
        for (int i = n - 1; i >= 0 && have > want; i--) if (plan.tracks[i].instrumental) { plan.tracks[i].instrumental = false; have--; }
        for (int k = 1; have < want && k <= 2 * n; k++) {
            int i = std::clamp((int) ((double) k * n / (want + 1)), 0, n - 1);
            if (i == 0 && n > 1) i = 1;
            for (int j = i; j < n && have < want; j++) if (!plan.tracks[j].instrumental) { plan.tracks[j].instrumental = true; have++; break; }
        }
    }
    // Titres vides, en double ou interdits : « Piste N » (le morceau recevra un titre de l'IA à l'écriture).
    std::set<std::string> seen;
    for (const std::string & t : r.used_titles) seen.insert(fold_word(t));  // 1.0.47 : titres déjà faits
    for (int i = 0; i < n; i++) {
        AiTrackPlan & tp  = plan.tracks[i];
        std::string   key = fold_word(tp.title);
        bool          bad = tp.title.empty() || !seen.insert(key).second || !ai_forbidden_found(tp.title, r.avoid).empty();
        if (bad) tp.title.clear();
    }
    plan.ok = true;
    return plan;
}

std::vector<std::string> ai_plan_forbidden(const AiAlbumPlan & plan, const AiAlbumRequest & r) {
    std::string names;
    if (trim(r.band).empty()) names += plan.band + "\n";    // nom tapé par l'utilisateur : jamais vérifié ni changé
    if (trim(r.album).empty()) names += plan.album + "\n";
    return names.empty() ? std::vector<std::string>{} : ai_forbidden_found(names, r.avoid);
}

std::string ai_plan_retry_prompt(const std::vector<std::string> & found) {
    std::string p = "The band name or the album title uses forbidden words: ";
    for (size_t i = 0; i < found.size(); i++) p += (i ? ", " : "") + found[i];
    p += ". Plan the album again with a new band name and a new album title, without any of these words, and answer with the JSON object only.";
    return p;
}

std::string ai_strip_negative(const std::string & style, const std::string & negative) {
    std::set<std::string> bad;
    for (size_t a = 0; a <= negative.size();) {
        size_t      b = negative.find(',', a);
        std::string t = lower_ascii(trim(negative.substr(a, b == std::string::npos ? std::string::npos : b - a)));
        if (!t.empty()) bad.insert(t);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    if (bad.empty()) return style;
    std::string out;
    for (size_t a = 0; a <= style.size();) {
        size_t      b = style.find(',', a);
        std::string t = trim(style.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (!t.empty() && !bad.count(lower_ascii(t))) out += (out.empty() ? "" : ", ") + t;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

std::string ai_title_retry_prompt(const std::string & old_title, const std::vector<std::string> & found, const std::vector<std::string> & avoid) {
    std::string p = "The song title \"" + old_title + "\" uses forbidden words (";
    for (size_t i = 0; i < found.size(); i++) p += (i ? ", " : "") + found[i];
    p += "). Give a new title in the same language, without any of these words: ";
    for (size_t i = 0; i < avoid.size(); i++) p += (i ? ", " : "") + avoid[i];
    p += ". Answer with ONE JSON object and nothing else: {\"title\": \"...\"}";
    return p;
}

std::string ai_parse_title(const std::string & raw) {
    std::string text = raw;
    size_t      th;
    while ((th = text.find("<think>")) != std::string::npos) {
        size_t e = text.find("</think>", th);
        text.erase(th, e == std::string::npos ? std::string::npos : e + 8 - th);
    }
    std::string title;
    size_t      a = text.find('{'), b = text.rfind('}');
    if (a != std::string::npos && b != std::string::npos && b > a) {
        std::string json = repair_json(text.substr(a, b - a + 1));
        if (yyjson_doc * doc = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS)) {
            title = json_text(yyjson_doc_get_root(doc), "title");
            yyjson_doc_free(doc);
        }
    }
    if (title.empty()) {  // texte simple : première ligne non vide, sans « Title: »
        for (const std::string & l : lines_of(text)) {
            std::string t = trim(l);
            if (t.empty()) continue;
            std::string low = lower_ascii(t);
            if ((low.rfind("title", 0) == 0 || low.rfind("titre", 0) == 0) && t.find(':') != std::string::npos) t = trim(t.substr(t.find(':') + 1));
            title = t;
            break;
        }
    }
    for (char q : { '"', '*' }) title.erase(std::remove(title.begin(), title.end(), q), title.end());
    title = trim(title);
    return title.size() > 80 ? std::string() : title;  // plus long : ce n'est pas un titre
}

bool ai_is_voice_tag(const std::string & tag) {
    std::string t = lower_ascii(trim(tag));
    if (t.rfind("no ", 0) == 0 || t.rfind("without ", 0) == 0 || t == "instrumental") return false;
    // Parties de mot sûres ; « rap », « vox »… seulement en mot entier (« trap » ou « tenor sax » ne sont pas des voix).
    for (const char * w : { "vocal", "voice", "singer", "singing", "choir", "spoken", "humming", "falsetto", "a cappella", "acapella", "beatbox",
                            "lyric", "crooner", "growl", "scream", "duet" }) {
        if (t.find(w) != std::string::npos) return true;
    }
    std::string padded = " " + t + " ";
    for (const char * w : { " rap ", " rapper ", " vox ", " sing ", " hum ", " chant ", " harmonies " }) {
        if (padded.find(w) != std::string::npos) return true;
    }
    return false;
}

// Balises d'un style à virgules (sans blancs), dans l'ordre.
static std::vector<std::string> style_tags(const std::string & style) {
    std::vector<std::string> out;
    for (size_t a = 0; a <= style.size();) {
        size_t      b = style.find(',', a);
        std::string t = trim(style.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (!t.empty()) out.push_back(t);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

std::string ai_force_instrumental_style(const std::string & style) {
    std::vector<std::string> kept;
    bool                     inst = false, novocal = false;
    for (const std::string & t : style_tags(style)) {
        std::string low = lower_ascii(t);
        if (ai_is_voice_tag(t)) continue;
        inst    = inst || low == "instrumental";
        novocal = novocal || low == "no vocals";
        kept.push_back(t);
    }
    if (!novocal) kept.insert(kept.begin(), "no vocals");
    if (!inst) kept.insert(kept.begin(), "instrumental");
    std::string out;
    for (const std::string & t : kept) out += (out.empty() ? "" : ", ") + t;
    return out;
}

std::string ai_instrumental_negative(const std::string & negative) {
    std::vector<std::string> tags = style_tags(negative);
    for (const char * w : { "vocals", "singing", "voice", "choir", "humming", "spoken word", "rap", "lyrics" }) {
        bool have = false;
        for (const std::string & t : tags) have = have || lower_ascii(t) == w;
        if (!have) tags.push_back(w);
    }
    std::string out;
    for (const std::string & t : tags) out += (out.empty() ? "" : ", ") + t;
    return out;
}

std::vector<std::string> ai_dedupe_words(const std::vector<std::string> & words, int & removed) {
    std::vector<std::string> out, keys;
    removed = 0;
    auto same = [](const std::string & a, const std::string & b) {
        for (const char * s : { "", "s", "x", "es" }) {
            if (a == b + s || b == a + s) return true;
        }
        return false;
    };
    for (const std::string & w : words) {
        std::string t = trim(w), k = fold_word(t);
        if (k.empty()) continue;
        bool dup = false;
        for (const std::string & o : keys) dup = dup || same(k, o);
        if (dup) {
            removed++;
            continue;
        }
        keys.push_back(k);
        out.push_back(t);
    }
    return out;
}

std::string ai_track_context(const AiAlbumPlan & plan, int index) {
    const AiTrackPlan & tp = plan.tracks[index];
    std::string         p  = "This is track " + std::to_string(index + 1) + " of " + std::to_string(plan.tracks.size()) + " of the album \"" +
                     plan.album + "\" by the band \"" + plan.band + "\".";
    if (!plan.sound.empty()) p += "\nThe album sound, shared by all its tracks, is: " + plan.sound + ". Start the style tags from it.";
    if (tp.bpm > 0) p += "\nTempo of this track: about " + std::to_string(tp.bpm) + " BPM.";
    if (!tp.mood.empty()) p += "\nMood of this track: " + tp.mood + ".";
    if (!tp.theme.empty()) p += "\nSubject of this track: " + tp.theme + ".";
    if (tp.instrumental) p += "\nThis track is INSTRUMENTAL: no vocals at all. Remove every vocal tag of the album sound from its style, and add \"instrumental, no vocals\".";
    return p;
}

// ---------------------------------------------------------------------------- Pochettes (1.0.36)

const std::vector<LocalImageModel> & local_image_models() {
    // Tailles relevées sur Hugging Face (Z-Image le 24/10/2026, klein et MOSS le 28/10/2026). Licences Apache 2.0.
    static const std::vector<LocalImageModel> list = {
        { "klein", "FLUX.2 klein 4B", "flux2-klein",
          { { "leejet/FLUX.2-klein-4B-GGUF", "flux-2-klein-4b-Q4_0.gguf", "flux-2-klein-4b-Q4_0.gguf", 2460378560ull },
            { "unsloth/Qwen3-4B-GGUF", "Qwen3-4B-Q4_K_M.gguf", "Qwen3-4B-Q4_K_M.gguf", 2497281312ull },
            { "unsloth/MOSS-Image-SFT-FP8", "vae/diffusion_pytorch_model.safetensors", "flux2_vae.safetensors", 249521372ull } },
          4, 1.0f, 14, 5.9f },
        { "zimage", "Z-Image Turbo", "",
          { { "leejet/Z-Image-Turbo-GGUF", "z_image_turbo-Q4_0.gguf", "z_image_turbo-Q4_0.gguf", 3683370944ull },
            { "unsloth/Qwen3-4B-Instruct-2507-GGUF", "Qwen3-4B-Instruct-2507-Q4_K_M.gguf", "Qwen3-4B-Instruct-2507-Q4_K_M.gguf", 2497281120ull },
            { "Comfy-Org/z_image_turbo", "split_files/vae/ae.safetensors", "ae.safetensors", 335304388ull } },
          5, 1.0f, 22, 8.5f },  // 1.0.72 : banc du 2/11/2026 : Q4_0 + 5 étapes = 39 s en 1344 × 768, Q4_K + 8 étapes = 60 s, rendu presque identique
        { "moss", "MOSS-Image", "moss-image",
          { { "unsloth/MOSS-Image-SFT-GGUF", "MOSS-Image-SFT-UD-Q4_K_XL.gguf", "MOSS-Image-SFT-UD-Q4_K_XL.gguf", 3419844768ull },
            { "unsloth/Qwen3.5-9B-GGUF", "Qwen3.5-9B-Q4_K_M.gguf", "Qwen3.5-9B-Q4_K_M.gguf", 5680522464ull },
            { "unsloth/MOSS-Image-SFT-FP8", "vae/diffusion_pytorch_model.safetensors", "flux2_vae.safetensors", 249521372ull },
            { "unsloth/MOSS-Image-SFT-FP8", "tokenizer/tokenizer.json", "tokenizer.json", 19989325ull } },
          20, 3.0f, 180, 8.4f },
        // 1.0.58 : un seul fichier (modèle, texte et VAE ensemble) ; 13 s en 768 × 768, 17 s en 1344 × 768 (banc du 31/10/2026)
        { "sdxl", "SDXL-Lightning", "sdxl-lightning",
          { { "ByteDance/SDXL-Lightning", "sdxl_lightning_4step.safetensors", "sdxl_lightning_4step.safetensors", 6938040682ull } },
          4, 1.0f, 10, 6.6f },
    };
    return list;
}

const LocalImageModel & local_image_model(const std::string & id) {
    for (const LocalImageModel & m : local_image_models()) if (id == m.id) return m;
    return local_image_models().front();
}

fs::path local_image_dir(const fs::path & images_root, const LocalImageModel & m) {
    return *m.subdir ? images_root / widen(m.subdir) : images_root;
}

unsigned long long local_image_bytes(const LocalImageModel & m) {
    unsigned long long n = 0;
    for (const ImageModelFile & f : m.files) n += f.bytes;
    return n;
}

std::string image_model_url(const ImageModelFile & f) { return std::string("https://huggingface.co/") + f.repo + "/resolve/main/" + f.path; }

bool image_model_ready(const fs::path & images_root, const LocalImageModel & m) {
    std::error_code ec;
    fs::path        dir = local_image_dir(images_root, m);
    for (const ImageModelFile & f : m.files) {
        auto n = fs::file_size(dir / widen(f.file), ec);
        if (ec || n < f.bytes) return false;
    }
    return true;
}

fs::path find_sd_cli(const fs::path & app_dir) {
    std::error_code ec;
    fs::path        p = app_dir / L"outils" / L"sd-vulkan" / L"sd-cli.exe";
    return fs::exists(p, ec) ? p : fs::path();
}

const std::vector<ImageService> & image_services() {
    static const std::vector<ImageService> list = {
        { "local", N_("Local (carte graphique)"), "", "", "" },
        { "openai", "OpenAI (gpt-image-1)", "https://api.openai.com/v1", "gpt-image-1", "openai" },
        { "xai", "xAI (Grok)", "https://api.x.ai/v1", "grok-2-image", "xai" },
        { "together", "Together AI (FLUX.1 schnell)", "https://api.together.xyz/v1", "black-forest-labs/FLUX.1-schnell", "together" },
        { "custom", N_("Autre service compatible OpenAI"), "", "", "custom" },
    };
    return list;
}

const ImageService * find_image_service(const std::string & id) {
    for (const ImageService & s : image_services()) if (id == s.id) return &s;
    return nullptr;
}

// Fichiers du modèle pour sd-cli et sd-server. Un seul fichier = SDXL-Lightning : décodage par tuiles, sinon 48 s au lieu de 5,5 s.
// 1.0.72 : Z-Image aussi : sans tuiles, 159 s de décodage en 1344 × 768 au lieu de 5 s.
static std::wstring image_weights(const fs::path & dir, const std::vector<ImageModelFile> & f) {
    if (f.size() == 1) return L" -m \"" + (dir / widen(f[0].file)).wstring() + L"\" --vae-tiling";
    return L" --diffusion-model \"" + (dir / widen(f[0].file)).wstring() + L"\" --llm \"" + (dir / widen(f[1].file)).wstring() + L"\" --vae \"" +
           (dir / widen(f[2].file)).wstring() + L"\"" + (f[0].file == std::string("z_image_turbo-Q4_0.gguf") ? L" --vae-tiling" : L"") + (f.size() > 3 ? L" --tokenizer \"" + (dir / widen(f[3].file)).wstring() + L"\"" : std::wstring());
}

AiCallResult ai_image_local(const fs::path & sd_cli, const fs::path & images_root, const LocalImageModel & m, const std::string & prompt,
                            int width, int height, const fs::path & out_png, std::atomic<bool> * cancel, bool offload) {
    AiCallResult r;
    if (sd_cli.empty()) {
        r.error = N_("Moteur d'images introuvable (dossier « outils\\sd-vulkan »).");
        return r;
    }
    if (!image_model_ready(images_root, m)) {
        r.error = N_("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes.");
        return r;
    }
    const auto &   f   = m.files;
    const fs::path dir = local_image_dir(images_root, m);
    std::string    p   = prompt;
    for (char & c : p) if (c == '"' || c == '\r' || c == '\n') c = c == '"' ? '\'' : ' ';
    std::error_code ec;
    fs::remove(out_png, ec);
    // Réglages de la fiche du modèle (étapes, cfg) ; graine au hasard ; décodage découpé si la mémoire manque (automatique).
    wchar_t cfg[16];
    swprintf(cfg, 16, L"%.1f", m.cfg);
    std::wstring weights = image_weights(dir, f) + (f.size() == 1 ? L" --sampling-method euler --scheduler sgm_uniform" : L"");
    std::wstring cmd = L"\"" + sd_cli.wstring() + L"\"" + weights +
                       (offload ? L" --offload-to-cpu" : L"") + L" -p \"" + widen(p) + L"\" --cfg-scale " + cfg + L" --steps " +
                       std::to_wstring(m.steps) + L" -W " + std::to_wstring(width) + L" -H " + std::to_wstring(height) +
                       L" --seed -1 -o \"" + out_png.wstring() + L"\"";
    fs::path            log = app_temp_dir() / L"studio_yue2_image.log";
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE              out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, nullptr);
    STARTUPINFOW        si{ sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError  = out;
    PROCESS_INFORMATION pi{};
    // Comme pour llama.cpp : pas de réglages « mode stable » de yue2.cpp (calculs FP32) pour ce moteur.
    static const wchar_t * stable_vars[] = { L"GGML_VK_DISABLE_F16", L"GGML_VK_DISABLE_INTEGER_DOT_PRODUCT", L"GGML_VK_DISABLE_MMVQ" };
    std::wstring           saved[3];
    bool                   had[3] = {};
    for (int i = 0; i < 3; i++) {
        wchar_t buf[64];
        DWORD   n = GetEnvironmentVariableW(stable_vars[i], buf, 64);
        had[i]    = n > 0 && n < 64;
        if (had[i]) saved[i] = buf;
        SetEnvironmentVariableW(stable_vars[i], nullptr);
    }
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                             sd_cli.parent_path().c_str(), &si, &pi);
    for (int i = 0; i < 3; i++) {
        if (had[i]) SetEnvironmentVariableW(stable_vars[i], saved[i].c_str());
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) {
        r.error = N_("Le moteur d'images n'a pas pu démarrer.");
        return r;
    }
    bool cancelled = false;
    while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT) {
        if (cancel && *cancel) {
            TerminateProcess(pi.hProcess, 1);
            cancelled = true;
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (cancelled) {
        r.error = N_("Demande annulée.");
        return r;
    }
    if (!fs::exists(out_png, ec) || fs::file_size(out_png, ec) < 1000) {
        r.error     = N_("Le moteur d'images n'a pas produit d'image (détails : %s).");
        r.error_arg = narrow(log.wstring());
        return r;
    }
    r.ok   = true;
    r.text = narrow(out_png.wstring());
    return r;
}

// Octets d'un texte base64 (CryptStringToBinaryA de Windows).
static std::string base64_bytes(const std::string & b64) {
    DWORD n = 0;
    if (!CryptStringToBinaryA(b64.data(), (DWORD) b64.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return "";
    std::string out(n, '\0');
    if (!CryptStringToBinaryA(b64.data(), (DWORD) b64.size(), CRYPT_STRING_BASE64, (BYTE *) out.data(), &n, nullptr, nullptr)) return "";
    out.resize(n);
    return out;
}

// ---- 1.0.61 : modèle d'images gardé chargé
static const int SD_SERVER_PORT = 8091;  // port réservé au Studio, à côté de celui de l'IA de texte

LocalImageServer::LocalImageServer(const fs::path & sd_cli, const fs::path & images_root, const LocalImageModel & model, bool offload)
    : sd_cli_(sd_cli), root_(images_root), model_(model), offload_(offload) {}

LocalImageServer::~LocalImageServer() {
    if (!process_) return;
    TerminateProcess((HANDLE) process_, 0);
    WaitForSingleObject((HANDLE) process_, 5000);
    CloseHandle((HANDLE) process_);
}

bool LocalImageServer::start(std::atomic<bool> * cancel) {
    std::error_code ec;
    fs::path        exe = sd_cli_.parent_path() / L"sd-server.exe";
    if (!fs::exists(exe, ec) || !image_model_ready(root_, model_)) return false;
    fs::path     log = app_temp_dir() / L"studio_yue2_image.log";
    std::wstring cmd = L"\"" + exe.wstring() + L"\"" + image_weights(local_image_dir(root_, model_), model_.files) + (offload_ ? L" --offload-to-cpu" : L"") +
                       L" --listen-port " + std::to_wstring(SD_SERVER_PORT);
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE              out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, nullptr);
    STARTUPINFOW        si{ sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError  = out;
    PROCESS_INFORMATION pi{};
    // Comme pour sd-cli : pas de réglages « mode stable » de yue2.cpp pour ce moteur.
    static const wchar_t * stable_vars[] = { L"GGML_VK_DISABLE_F16", L"GGML_VK_DISABLE_INTEGER_DOT_PRODUCT", L"GGML_VK_DISABLE_MMVQ" };
    std::wstring           saved[3];
    bool                   had[3] = {};
    for (int i = 0; i < 3; i++) {
        wchar_t buf[64];
        DWORD   n = GetEnvironmentVariableW(stable_vars[i], buf, 64);
        had[i]    = n > 0 && n < 64;
        if (had[i]) saved[i] = buf;
        SetEnvironmentVariableW(stable_vars[i], nullptr);
    }
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                             sd_cli_.parent_path().c_str(), &si, &pi);
    for (int i = 0; i < 3; i++) {
        if (had[i]) SetEnvironmentVariableW(stable_vars[i], saved[i].c_str());
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    std::string base = "http://127.0.0.1:" + std::to_string(SD_SERVER_PORT);
    for (int i = 0; i < 240; i++) {  // 2 minutes au plus pour charger le modèle
        if ((cancel && *cancel) || WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) return false;
        std::string body;
        if (http_request("GET", base + "/v1/models", {}, "", body, 2000) == 200) return true;
        Sleep(500);
    }
    return false;
}

AiCallResult LocalImageServer::generate(const std::string & prompt, int width, int height, const fs::path & out_png, std::atomic<bool> * cancel) {
    AiCallResult r;
    r.error = N_("Le moteur d'images n'a pas produit d'image (détails : %s).");
    r.error_arg = narrow((app_temp_dir() / L"studio_yue2_image.log").wstring());
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "prompt", prompt.c_str());
    yyjson_mut_obj_add_int(doc, root, "width", width);
    yyjson_mut_obj_add_int(doc, root, "height", height);
    yyjson_mut_obj_add_int(doc, root, "steps", model_.steps);
    yyjson_mut_obj_add_real(doc, root, "cfg_scale", model_.cfg);
    yyjson_mut_obj_add_int(doc, root, "seed", -1);
    if (model_.files.size() == 1) {  // SDXL-Lightning : réglages de sa fiche
        yyjson_mut_obj_add_str(doc, root, "sampler_name", "euler");
        yyjson_mut_obj_add_str(doc, root, "scheduler", "sgm_uniform");
    }
    char *      text = yyjson_mut_write(doc, 0, nullptr);
    std::string payload = text ? text : "";
    free(text);
    yyjson_mut_doc_free(doc);
    // Annulation pendant le calcul : le serveur est arrêté, ce qui coupe la requête en cours.
    std::atomic<bool> finished{ false };
    HANDLE            process = (HANDLE) process_;
    std::thread       watch([&] {
        while (!finished) {
            if (cancel && *cancel) {
                TerminateProcess(process, 1);
                break;
            }
            Sleep(100);
        }
    });
    std::string body;
    int         code = http_request("POST", "http://127.0.0.1:" + std::to_string(SD_SERVER_PORT) + "/sdapi/v1/txt2img",
                                    { { "Content-Type", "application/json" } }, payload, body, 300000);
    finished = true;
    watch.join();
    if (code != 200) return r;
    yyjson_doc * res = yyjson_read(body.data(), body.size(), 0);
    if (!res) return r;
    yyjson_val * first = yyjson_arr_get_first(yyjson_obj_get(yyjson_doc_get_root(res), "images"));
    std::string  b64   = yyjson_is_str(first) ? yyjson_get_str(first) : "";
    yyjson_doc_free(res);
    size_t comma = b64.find(',');
    if (comma != std::string::npos) b64.erase(0, comma + 1);
    std::string bytes = base64_bytes(b64);
    if (bytes.size() < 1000) return r;
    std::ofstream(out_png, std::ios::binary | std::ios::trunc) << bytes;
    r = AiCallResult{};
    r.ok   = true;
    r.text = narrow(out_png.wstring());
    return r;
}

AiCallResult ai_image_online(const std::string & service, const AiEndpoint & ep, const std::string & prompt, int width, int height,
                             const fs::path & out_base) {
    AiCallResult r;
    std::string  base = trim_slash(ep.base_url);
    if (base.empty()) {
        r.error = N_("Adresse du service manquante (Réglages › IA).");
        return r;
    }
    // Taille demandée selon la forme : carré, paysage (YouTube) ou portrait (TikTok).
    const bool       wide = width > height, tall = height > width;
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o    = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_strcpy(doc, o, "model", ep.model.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "prompt", prompt.c_str());
    yyjson_mut_obj_add_int(doc, o, "n", 1);
    if (service == "together") {  // largeur et hauteur en pixels (multiples de 64), réponse en base64
        yyjson_mut_obj_add_int(doc, o, "width", wide ? 1024 : tall ? 576 : 1024);
        yyjson_mut_obj_add_int(doc, o, "height", wide ? 576 : tall ? 1024 : 1024);
        yyjson_mut_obj_add_int(doc, o, "steps", 4);
        yyjson_mut_obj_add_str(doc, o, "response_format", "base64");
    } else if (service == "xai") {  // pas de choix de taille chez xAI
        yyjson_mut_obj_add_str(doc, o, "response_format", "b64_json");
    } else {  // OpenAI et services compatibles
        yyjson_mut_obj_add_str(doc, o, "size", wide ? "1536x1024" : tall ? "1024x1536" : "1024x1024");
    }
    char *      json    = yyjson_mut_write(doc, 0, nullptr);
    std::string payload = json ? json : "";
    free(json);
    yyjson_mut_doc_free(doc);
    std::string body;
    int         code = http_request("POST", base + "/images/generations", auth_headers(ep), payload, body, 300000);
    if (code != 200) {
        fill_error(r, code, body);
        return r;
    }
    std::string bytes;
    if (yyjson_doc * rd = yyjson_read(body.data(), body.size(), 0)) {
        yyjson_val * item = yyjson_arr_get_first(yyjson_obj_get(yyjson_doc_get_root(rd), "data"));
        yyjson_val * b64  = yyjson_obj_get(item, "b64_json");
        yyjson_val * url  = yyjson_obj_get(item, "url");
        if (yyjson_is_str(b64)) bytes = base64_bytes(yyjson_get_str(b64));
        else if (yyjson_is_str(url)) {
            std::string img;
            if (http_request("GET", yyjson_get_str(url), {}, "", img, 120000) == 200) bytes = img;
        }
        yyjson_doc_free(rd);
    }
    if (bytes.size() < 1000) {
        r.error     = N_("Réponse vide ou inattendue du service : %s");
        r.error_arg = body.substr(0, 300);
        return r;
    }
    // Format d'après les premiers octets
    const char * ext = ".png";
    if ((unsigned char) bytes[0] == 0xFF && (unsigned char) bytes[1] == 0xD8) ext = ".jpg";
    else if (bytes.compare(0, 4, "RIFF") == 0) ext = ".webp";
    fs::path dst = out_base;
    dst += ext;
    FILE * f = _wfopen(dst.c_str(), L"wb");
    if (!f) {
        r.error = N_("Écriture de l'image impossible.");
        return r;
    }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    r.ok   = true;
    r.text = narrow(dst.wstring());
    return r;
}

// ---------------------------------------------------------------------------- Clés d'API (DPAPI)

std::string ai_protect_key(const std::string & key) {
    if (key.empty()) return "";
    DATA_BLOB in{ (DWORD) key.size(), (BYTE *) key.data() }, out{};
    if (!CryptProtectData(&in, L"Studio YuE2", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return "";
    DWORD n = 0;
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    std::string text(n, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &n);
    LocalFree(out.pbData);
    text.resize(n);
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
}

std::string ai_unprotect_key(const std::string & stored) {
    if (stored.empty()) return "";
    DWORD n = 0;
    if (!CryptStringToBinaryA(stored.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return "";
    std::vector<BYTE> bin(n);
    if (!CryptStringToBinaryA(stored.c_str(), 0, CRYPT_STRING_BASE64, bin.data(), &n, nullptr, nullptr)) return "";
    DATA_BLOB in{ n, bin.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return "";
    std::string key((const char *) out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return key;
}
