#include "studio.hpp"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#include "fiches.hpp"
#include "gpuinfo.hpp"
#include "i18n.hpp"
#include "suno.hpp"
#include "yyjson.h"

// Options du moteur (--no-fa, --clamp-fp16) : choisies selon la carte et le moteur, voir engine_flags() dans gpuinfo.cpp.
// LoRA communautaire Mothersuperior/YuE2-instrumental-cot-full-loras (CC BY-NC 4.0).
static const char *    INSTRUMENTAL_LORA = "ar_lora_inst_v3abc.bf16.safetensors";
static const char *    DEFAULT_STRUCTURE = "[intro]\n[verse]\n[chorus]\n[verse]\n[chorus]\n[outro]";

// ---------------------------------------------------------------------------- utilitaires

std::wstring widen(const std::string & s) {
    if (s.empty()) {
        return {};
    }
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring & w) {
    if (w.empty()) {
        return {};
    }
    int         n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string safe_filename(const std::string & title) {
    std::string out;
    bool        space = false;
    for (unsigned char c : title) {
        bool bad = c < 0x20 || strchr("<>:\"/\\|?*", c) != nullptr;
        if (bad || c == ' ' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += (char) c;
    }
    while (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    if (out.size() > 100) {
        out.resize(100);  // peut couper un caractère UTF-8 : on recule jusqu'à une frontière
        while (!out.empty() && ((unsigned char) out.back() & 0xC0) == 0x80) {
            out.pop_back();
        }
        if (!out.empty() && ((unsigned char) out.back() & 0x80)) {
            out.pop_back();
        }
    }
    return out.empty() ? "chanson" : out;
}

double now_seconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

bool other_synth_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{ sizeof(entry) };
    bool            found = false;
    for (BOOL ok = Process32FirstW(snap, &entry); ok && !found; ok = Process32NextW(snap, &entry)) {
        found = _wcsicmp(entry.szExeFile, L"yue-synth.exe") == 0;
    }
    CloseHandle(snap);
    return found;
}

static std::wstring quote(const std::wstring & arg) {
    std::wstring out = L"\"";
    size_t       slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
        } else {
            out.append(slashes, L'\\');
        }
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

static std::string read_file(const fs::path & path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

static void write_file_atomic(const fs::path & path, const char * data, size_t size) {
    fs::path tmp = path;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data, (std::streamsize) size);
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Lance une commande et transmet chaque ligne de stderr (ou de stdout) ('\r' et '\n' séparent les lignes).
static int run_process(std::wstring command, const std::function<void(const std::string &)> & on_line,
                       const std::function<void(HANDLE)> & on_start, bool read_stdout = false) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE              read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = read_stdout ? write_end : nul;
    si.hStdError  = read_stdout ? nul : write_end;
    PROCESS_INFORMATION pi{};
    apply_gpu_env();  // variables GGML_VK_* selon la carte (AMD : noyaux FP32 ; Nvidia : noyaux rapides), héritées par le programme
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &si, &pi);
    CloseHandle(write_end);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(read_end);
        return -1;
    }
    if (on_start) {
        on_start(pi.hProcess);
    }
    // Limiteur d'utilisation de la carte (Réglages › Système) : pour les programmes du moteur (yue-synth,
    // yue-transcribe…), on suspend le processus une fraction de chaque cycle de 200 ms. À 90 %, il travaille
    // 180 ms puis attend 20 ms : la carte souffle, la génération dure un peu plus longtemps.
    // Phase audio (lignes « [NAR] Step k/32 ») : c'est le moteur lui-même qui fait la pause entre deux pas,
    // car chaque pas envoie d'un bloc environ 0,3 s de calcul à la carte, qu'une suspension n'interrompt pas.
    // La suspension par cycles ne sert donc qu'aux autres phases (composition, décodage).
    std::atomic<bool> finished{ false }, nar_phase{ false };
    std::thread       limiter;
    if (command.find(L"yue-") != std::wstring::npos) {
        limiter = std::thread([&finished, &nar_phase, process = pi.hProcess] {
            using NtProc   = LONG(NTAPI *)(HANDLE);
            HMODULE ntdll  = GetModuleHandleW(L"ntdll.dll");
            auto    pause  = ntdll ? (NtProc) GetProcAddress(ntdll, "NtSuspendProcess") : nullptr;
            auto    resume = ntdll ? (NtProc) GetProcAddress(ntdll, "NtResumeProcess") : nullptr;
            if (!pause || !resume) return;
            while (!finished) {
                int limit = std::clamp(gpu_profile().gpu_limit.load(), 10, 100);
                if (limit >= 100 || nar_phase) {
                    Sleep(200);
                    continue;
                }
                DWORD off = (DWORD) (2 * (100 - limit));  // millisecondes de pause sur 200
                Sleep(200 - off);
                if (finished) break;
                pause(process);
                Sleep(off);
                resume(process);
            }
        });
    }
    std::string pending;
    char        buffer[4096];
    DWORD       got = 0;
    while (ReadFile(read_end, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; i++) {
            char c = buffer[i];
            if (c == '\r' || c == '\n') {
                if (!nar_phase && pending.find("[NAR]") != std::string::npos) nar_phase = true;
                if (!pending.empty() && on_line) {
                    on_line(pending);
                }
                pending.clear();
            } else {
                pending += c;
            }
        }
    }
    if (!pending.empty() && on_line) {
        on_line(pending);
    }
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    finished = true;
    if (limiter.joinable()) limiter.join();
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (on_start) {
        on_start(nullptr);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int) code;
}


// ---------------------------------------------------------------------------- JSON

static std::string get_str(yyjson_val * obj, const char * key, const std::string & fallback = "") {
    yyjson_val * v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : fallback;
}

static double get_num(yyjson_val * obj, const char * key, double fallback = 0) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    return yyjson_is_num(v) ? yyjson_get_num(v) : fallback;
}

static int64_t get_int(yyjson_val * obj, const char * key, int64_t fallback) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    if (yyjson_is_sint(v)) {
        return yyjson_get_sint(v);
    }
    if (yyjson_is_uint(v)) {
        return (int64_t) yyjson_get_uint(v);
    }
    if (yyjson_is_real(v)) {
        return (int64_t) yyjson_get_real(v);
    }
    return fallback;
}

static bool get_bool(yyjson_val * obj, const char * key, bool fallback) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    return yyjson_is_bool(v) ? yyjson_get_bool(v) : fallback;
}

// Mesures d'une génération relevées dans le journal de yue-synth (aussi pour les anciens morceaux).
static void parse_timing_line(const std::string & line, Song & s) {
    int    n = 0, frames = 0;
    double sec = 0, ms_step = 0;
    if (sscanf(line.c_str(), "[AR] Score: %d tokens over %*d songs, %*d steps, %lf s", &n, &sec) == 2) {
        s.score_tokens = n;
        s.score_s      = sec;
    } else if (sscanf(line.c_str(), "[AR] Semantic: %d tokens over %*d songs, %*d steps, %lf s", &n, &sec) == 2) {
        s.sem_tokens = n;
        s.sem_s      = sec;
    } else if (sscanf(line.c_str(), "[NAR] Solved: T_lat=%d, %*d variations, %*d steps, %*lf ms (%lf ms/step)", &frames,
                      &ms_step) == 2) {
        s.frames     = frames;
        s.nar_step_s = ms_step / 1000.0;
    }
}

static void timing_from_log(const fs::path & log, Song & s) {
    std::ifstream in(log, std::ios::binary);
    std::string   line;
    while (std::getline(in, line)) {
        parse_timing_line(line, s);
    }
}

static void place_song(const Song & s, bool force = false);  // 1.0.43 : dossier du morceau sous Audios (plus bas)

static bool load_song(const fs::path & meta, Song & song) {
    std::string  text = read_file(meta);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) {
        return false;
    }
    yyjson_val * r         = yyjson_doc_get_root(doc);
    song.id                = get_str(r, "id");
    song.title             = get_str(r, "title");
    song.style             = get_str(r, "style");
    song.lyrics            = get_str(r, "lyrics");
    song.cot               = get_str(r, "cot", "full");
    song.instrumental      = get_bool(r, "instrumental", false);
    song.instrumental_lora = get_bool(r, "instrumental_lora", true);
    song.lm_seed           = get_int(r, "lm_seed", -1);
    song.seed              = get_int(r, "seed", -1);
    song.duration          = get_num(r, "duration", 360);
    song.status            = get_str(r, "status", "error");
    song.stage             = get_str(r, "stage");
    song.message           = get_str(r, "message");
    song.progress          = get_num(r, "progress");
    song.created           = get_num(r, "created");
    song.started           = get_num(r, "started");
    song.finished          = get_num(r, "finished");
    song.audio_seconds     = get_num(r, "audio_seconds");
    song.kind              = get_str(r, "kind", "new");
    song.parent            = get_str(r, "parent");
    song.parent_title      = get_str(r, "parent_title");
    song.source_file       = get_str(r, "source_file");
    song.long_rest         = get_str(r, "long_rest");  // 1.0.48
    song.long_join         = get_num(r, "long_join", 0);
    song.model             = get_str(r, "model");
    song.solver            = get_str(r, "solver");
    song.lyrics_lang          = get_str(r, "lyrics_lang");
    song.lyrics_lang_declared = get_bool(r, "lyrics_lang_declared", false);
    song.lyrics_lang_check    = (int) get_num(r, "lyrics_lang_check", 0);
    song.heard_vocals_lang    = get_str(r, "heard_vocals_lang");
    if (song.lyrics_lang.empty() && !song.instrumental && !song.lyrics.empty()) {  // morceaux d'avant cette fonction
        song.lyrics_lang = detect_lyrics_language(song.style, song.lyrics, &song.lyrics_lang_declared);
    }
    song.fade_seconds      = get_num(r, "fade_seconds");
    song.negative_style    = get_str(r, "negative_style");
    song.negative_scale    = get_num(r, "negative_scale", 1.5);
    {
        size_t       i2, m2;
        yyjson_val * item;
        yyjson_arr_foreach(yyjson_obj_get(r, "adapters"), i2, m2, item) {
            std::string name = get_str(item, "name");
            if (!name.empty()) song.adapters.push_back({ name, get_num(item, "scale", 1.0) });
        }
    }
    song.artist            = get_str(r, "artist");
    song.album             = get_str(r, "album");
    song.track             = get_str(r, "track");
    song.eta               = get_num(r, "eta");
    song.eta_at            = get_num(r, "eta_at");
    song.score_tokens      = (int) get_int(r, "score_tokens", 0);
    song.sem_tokens        = (int) get_int(r, "sem_tokens", 0);
    song.frames            = (int) get_int(r, "frames", 0);
    song.score_s           = get_num(r, "score_s");
    song.sem_s             = get_num(r, "sem_s");
    song.nar_step_s        = get_num(r, "nar_step_s");
    song.auto_duration     = get_bool(r, "auto_duration", false);
    song.held              = get_bool(r, "held", false);
    song.validated         = get_bool(r, "validated", false);
    song.heard             = get_bool(r, "heard", true);  // les anciens morceaux sont réputés écoutés
    song.flagged           = get_bool(r, "flagged", false);
    song.truncated         = get_bool(r, "truncated", false);
    song.expected_seconds  = get_num(r, "expected_seconds");
    yyjson_val * seeds     = yyjson_obj_get(r, "used_seeds");
    song.has_used_seeds    = yyjson_is_obj(seeds);
    if (song.has_used_seeds) {
        song.used_lm_seed = get_int(seeds, "lm_seed", 0);
        song.used_seed    = get_int(seeds, "seed", 0);
    }
    yyjson_doc_free(doc);
    if (song.status == "done" && song.frames == 0) {
        timing_from_log(meta.parent_path() / "generation.log", song);  // morceaux d'avant les mesures
    }
    return !song.id.empty();
}

static void add_opt_real(yyjson_mut_doc * doc, yyjson_mut_val * obj, const char * key, double v) {
    if (v > 0) {
        yyjson_mut_obj_add_real(doc, obj, key, v);
    } else {
        yyjson_mut_obj_add_null(doc, obj, key);
    }
}

static std::string write_doc(yyjson_mut_doc * doc, yyjson_write_flag flags = 0) {
    size_t      len  = 0;
    char *      json = yyjson_mut_write(doc, flags, &len);
    std::string out(json ? json : "", len);
    free(json);
    return out;
}

void Studio::save(const Song & s) {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_strcpy(doc, o, "id", s.id.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "title", s.title.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "style", s.style.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "lyrics", s.lyrics.c_str());
    yyjson_mut_obj_add_bool(doc, o, "instrumental", s.instrumental);
    yyjson_mut_obj_add_bool(doc, o, "instrumental_lora", s.instrumental_lora);
    if (!s.model.empty()) yyjson_mut_obj_add_strcpy(doc, o, "model", s.model.c_str());
    if (!s.solver.empty()) yyjson_mut_obj_add_strcpy(doc, o, "solver", s.solver.c_str());
    if (!s.lyrics_lang.empty()) {
        yyjson_mut_obj_add_strcpy(doc, o, "lyrics_lang", s.lyrics_lang.c_str());
        yyjson_mut_obj_add_bool(doc, o, "lyrics_lang_declared", s.lyrics_lang_declared);
    }
    if (s.lyrics_lang_check > 0) yyjson_mut_obj_add_int(doc, o, "lyrics_lang_check", s.lyrics_lang_check);
    if (!s.heard_vocals_lang.empty()) yyjson_mut_obj_add_strcpy(doc, o, "heard_vocals_lang", s.heard_vocals_lang.c_str());
    if (s.fade_seconds > 0) yyjson_mut_obj_add_real(doc, o, "fade_seconds", s.fade_seconds);
    if (!s.negative_style.empty()) {
        yyjson_mut_obj_add_strcpy(doc, o, "negative_style", s.negative_style.c_str());
        yyjson_mut_obj_add_real(doc, o, "negative_scale", s.negative_scale);
    }
    if (!s.heard) yyjson_mut_obj_add_bool(doc, o, "heard", false);
    if (!s.adapters.empty()) {
        yyjson_mut_val * arr = yyjson_mut_obj_add_arr(doc, o, "adapters");
        for (const auto & [name, scale] : s.adapters) {
            yyjson_mut_val * a = yyjson_mut_arr_add_obj(doc, arr);
            yyjson_mut_obj_add_strcpy(doc, a, "name", name.c_str());
            yyjson_mut_obj_add_real(doc, a, "scale", scale);
        }
    }
    yyjson_mut_obj_add_strcpy(doc, o, "cot", s.cot.c_str());
    yyjson_mut_obj_add_sint(doc, o, "lm_seed", s.lm_seed);
    yyjson_mut_obj_add_sint(doc, o, "seed", s.seed);
    yyjson_mut_obj_add_real(doc, o, "duration", s.duration);
    yyjson_mut_obj_add_strcpy(doc, o, "status", s.status.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "stage", s.stage.c_str());
    yyjson_mut_obj_add_real(doc, o, "progress", s.progress);
    yyjson_mut_obj_add_strcpy(doc, o, "message", s.message.c_str());
    yyjson_mut_obj_add_real(doc, o, "created", s.created);
    add_opt_real(doc, o, "started", s.started);
    add_opt_real(doc, o, "finished", s.finished);
    add_opt_real(doc, o, "audio_seconds", s.audio_seconds);
    if (s.has_used_seeds) {
        yyjson_mut_val * seeds = yyjson_mut_obj_add_obj(doc, o, "used_seeds");
        yyjson_mut_obj_add_sint(doc, seeds, "lm_seed", s.used_lm_seed);
        yyjson_mut_obj_add_sint(doc, seeds, "seed", s.used_seed);
    } else {
        yyjson_mut_obj_add_null(doc, o, "used_seeds");
    }
    yyjson_mut_obj_add_strcpy(doc, o, "kind", s.kind.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "parent", s.parent.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "parent_title", s.parent_title.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "source_file", s.source_file.c_str());
    if (!s.long_rest.empty()) yyjson_mut_obj_add_strcpy(doc, o, "long_rest", s.long_rest.c_str());  // 1.0.48
    if (s.long_join > 0) yyjson_mut_obj_add_real(doc, o, "long_join", s.long_join);
    yyjson_mut_obj_add_strcpy(doc, o, "artist", s.artist.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "album", s.album.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "track", s.track.c_str());
    yyjson_mut_obj_add_real(doc, o, "eta", s.eta);
    yyjson_mut_obj_add_real(doc, o, "eta_at", s.eta_at);
    yyjson_mut_obj_add_sint(doc, o, "score_tokens", s.score_tokens);
    yyjson_mut_obj_add_sint(doc, o, "sem_tokens", s.sem_tokens);
    yyjson_mut_obj_add_sint(doc, o, "frames", s.frames);
    yyjson_mut_obj_add_real(doc, o, "score_s", s.score_s);
    yyjson_mut_obj_add_real(doc, o, "sem_s", s.sem_s);
    yyjson_mut_obj_add_real(doc, o, "nar_step_s", s.nar_step_s);
    yyjson_mut_obj_add_bool(doc, o, "auto_duration", s.auto_duration);
    yyjson_mut_obj_add_bool(doc, o, "held", s.held);
    yyjson_mut_obj_add_bool(doc, o, "validated", s.validated);
    yyjson_mut_obj_add_bool(doc, o, "flagged", s.flagged);
    yyjson_mut_obj_add_bool(doc, o, "truncated", s.truncated);
    yyjson_mut_obj_add_real(doc, o, "expected_seconds", s.expected_seconds);
    std::string json   = write_doc(doc, YYJSON_WRITE_PRETTY);
    place_song(s);  // 1.0.43 : dossier rangé selon groupe, album et titre
    fs::path folder = song_folder(s.id);
    fs::create_directories(folder);
    write_file_atomic(folder / "meta.json", json.data(), json.size());
    yyjson_mut_doc_free(doc);
    std::error_code ec;
    mtimes_[s.id] = fs::last_write_time(folder / "meta.json", ec);
}

// En instrumental, les paroles se réduisent aux balises de section.
static std::string structure_only(const std::string & lyrics) {
    std::istringstream in(lyrics);
    std::string        line, tags;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
        if (a == std::string::npos) {
            continue;
        }
        line = line.substr(a, b - a + 1);
        if (line.size() > 2 && line.front() == '[' && line.back() == ']' && line.find(']') == line.size() - 1) {
            tags += (tags.empty() ? "" : "\n") + line;
        }
    }
    return tags.empty() ? DEFAULT_STRUCTURE : tags;
}

// Balises négatives (expérimental, moteur yue2.cpp modifié : patches\yue2cpp_negative_style.patch).
// Le moteur compare à chaque pas de la composition le morceau « normal » à un morceau guidé par le style à éviter,
// et s'éloigne de ce dernier : résultat = négatif + force × (normal − négatif). Rien n'est ajouté si le champ est vide.
static void add_negative(yyjson_mut_doc * doc, yyjson_mut_val * o, const Song & s) {
    if (s.negative_style.empty()) return;
    yyjson_mut_obj_add_strcpy(doc, o, "negative_style", s.negative_style.c_str());
    yyjson_mut_obj_add_real(doc, o, "cfg_scale", std::clamp(s.negative_scale, 1.05, 4.0));
}

// Requête yue2.cpp d'un nouveau morceau (ou d'une reprise, qui y ajoute sa partition).
static yyjson_mut_doc * new_request(const Song & s, bool & uses_adapter) {
    std::string lyrics = s.lyrics, style = s.style;
    uses_adapter       = false;
    if (s.instrumental) {
        lyrics = structure_only(s.lyrics);
        if (s.instrumental_lora) {
            uses_adapter = true;
        } else {
            std::string lower = style;
            for (auto & c : lower) {
                c = (char) tolower((unsigned char) c);
            }
            if (lower.find("instrumental") == std::string::npos) {
                style = "instrumental, " + style;
            }
            // Sans la LoRA instrumentale, le modèle de base chante dès qu'une section peut recevoir du texte :
            // couplets, refrains et ponts deviennent des interludes.
            std::istringstream in(lyrics);
            std::string        line, out;
            while (std::getline(in, line)) {
                std::string low = line;
                for (auto & c : low) c = (char) tolower((unsigned char) c);
                if (low == "[verse]" || low == "[chorus]" || low == "[pre-chorus]" || low == "[bridge]") line = "[interlude]";
                out += (out.empty() ? "" : "\n") + line;
            }
            lyrics = out;
        }
    }
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_strcpy(doc, o, "style", style.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "lyrics", lyrics.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "cot", s.cot.c_str());
    yyjson_mut_obj_add_sint(doc, o, "lm_seed", s.lm_seed);
    yyjson_mut_obj_add_sint(doc, o, "seed", s.seed);
    yyjson_mut_obj_add_real(doc, o, "duration", s.duration);
    yyjson_mut_obj_add_str(doc, o, "output_format", "wav24");  // qualité maximale : 24 bits, 48 kHz, 32 étapes
    // Par défaut (peak_clip=10), yue2.cpp écrête volontairement les 0,001% d'échantillons les plus forts
    // (crête dure, sans limiteur) pour maximiser le volume perçu : cela s'entend comme un grésillement
    // sur les passages forts. 0 = normalisation au vrai pic, sans aucun écrêtage.
    yyjson_mut_obj_add_int(doc, o, "peak_clip", 0);
    add_negative(doc, o, s);
    if (!s.solver.empty()) {
        const SolverSpec & sp = solver_spec(s.solver);
        yyjson_mut_obj_add_str(doc, o, "solver", sp.engine);
        yyjson_mut_obj_add_int(doc, o, "steps", sp.steps);
    }
    uses_adapter = uses_adapter || !s.adapters.empty();
    if (uses_adapter) {
        yyjson_mut_val * arr = yyjson_mut_obj_add_arr(doc, o, "adapters");
        if (s.instrumental && s.instrumental_lora) {
            yyjson_mut_val * a = yyjson_mut_arr_add_obj(doc, arr);
            yyjson_mut_obj_add_str(doc, a, "name", INSTRUMENTAL_LORA);
            yyjson_mut_obj_add_real(doc, a, "scale", 1.0);
        }
        for (const auto & [name, scale] : s.adapters) {
            yyjson_mut_val * a = yyjson_mut_arr_add_obj(doc, arr);
            yyjson_mut_obj_add_strcpy(doc, a, "name", name.c_str());
            yyjson_mut_obj_add_real(doc, a, "scale", scale);
        }
    }
    return doc;
}

// LoRA absentes du dossier adapters (supprimées par l'utilisateur) : retirées de la requête sans rien dire (1.0.29,
// demande de Herve : une LoRA supprimée ne doit plus être réclamée). Sans la LoRA instrumentale, un instrumental
// reçoit la balise « instrumental » comme avec le modèle de base. Renvoie vrai s'il reste au moins une LoRA.
static bool drop_missing_adapters(yyjson_mut_doc * doc, const fs::path & dir) {
    yyjson_mut_val * o   = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val * arr = yyjson_mut_obj_get(o, "adapters");
    if (!arr) return false;
    bool   lost_instrumental = false;
    size_t i = 0;
    while (i < yyjson_mut_arr_size(arr)) {
        yyjson_mut_val * a    = yyjson_mut_arr_get(arr, i);
        const char *     name = yyjson_mut_get_str(yyjson_mut_obj_get(a, "name"));
        std::error_code  ec;
        if (name && !fs::exists(dir / widen(name), ec)) {
            lost_instrumental |= strcmp(name, INSTRUMENTAL_LORA) == 0;
            yyjson_mut_arr_remove(arr, i);
        } else {
            i++;
        }
    }
    if (lost_instrumental) {
        yyjson_mut_val * st    = yyjson_mut_obj_get(o, "style");
        std::string      style = st && yyjson_mut_is_str(st) ? yyjson_mut_get_str(st) : "";
        std::string      lower = style;
        for (auto & c : lower) c = (char) tolower((unsigned char) c);
        if (lower.find("instrumental") == std::string::npos) {
            yyjson_mut_obj_remove_key(o, "style");
            yyjson_mut_obj_add_strcpy(doc, o, "style", ("instrumental, " + style).c_str());
        }
    }
    if (yyjson_mut_arr_size(arr) == 0) {
        yyjson_mut_obj_remove_key(o, "adapters");
        return false;
    }
    return true;
}

// Requête de rejeu écrite par yue-synth à côté de l'audio : codes sémantiques, partition, graines.
static yyjson_mut_doc * read_replay(const fs::path & file) {
    std::string  text = read_file(file);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) {
        return nullptr;
    }
    yyjson_mut_doc * out = yyjson_doc_mut_copy(doc, nullptr);
    yyjson_doc_free(doc);
    return out;
}

static void put_str(yyjson_mut_doc * doc, yyjson_mut_val * o, const char * key, const std::string & v) {
    yyjson_mut_obj_remove_key(o, key);
    yyjson_mut_obj_add_strcpy(doc, o, key, v.c_str());
}

static void put_num(yyjson_mut_doc * doc, yyjson_mut_val * o, const char * key, double v) {
    yyjson_mut_obj_remove_key(o, key);
    yyjson_mut_obj_add_real(doc, o, key, v);
}

static void put_int(yyjson_mut_doc * doc, yyjson_mut_val * o, const char * key, int64_t v) {
    yyjson_mut_obj_remove_key(o, key);
    yyjson_mut_obj_add_sint(doc, o, key, v);
}

double media_duration(const fs::path & file) {
    std::wstring cmd = ffprobe_exe() + L" -v error -show_entries format=duration -of csv=p=0 " + quote(file.wstring());
    std::string  out;
    run_process(cmd, [&](const std::string & l) { out += l; }, nullptr, true);
    return atof(out.c_str());
}

// Calé sur les morceaux générés : une ligne chantée dure ≈ 6 s, chaque section ajoute ≈ 6 s
// d'instrumental ; intro et fin ≈ 15 s. Les instrumentaux suivent la durée typique de chaque section.
double estimate_seconds(const std::string & lyrics, bool instrumental) {
    std::istringstream in(lyrics);
    std::string        line;
    double             total = 0;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
        if (a == std::string::npos) {
            continue;
        }
        line = line.substr(a, b - a + 1);
        if (line.front() == '[' && line.back() == ']') {
            std::string tag = line.substr(1, line.size() - 2);
            for (auto & c : tag) c = (char) tolower((unsigned char) c);
            if (instrumental) {
                total += tag.rfind("intro", 0) == 0 ? 15 : tag.rfind("outro", 0) == 0 ? 20
                       : tag.rfind("pre", 0) == 0   ? 15 : tag.rfind("bridge", 0) == 0 ? 25 : 30;
            } else {
                total += tag.rfind("intro", 0) == 0 || tag.rfind("outro", 0) == 0 ? 15
                       : tag.rfind("interlude", 0) == 0 ? 15 : 6;
            }
        } else if (!instrumental) {
            total += 6;
        }
    }
    return std::clamp(total, 30.0, 360.0);
}

int run_command(std::wstring command, const std::function<void(const std::string &)> & on_line,
                const std::function<void(void *)> & on_start, bool read_stdout) {
    return run_process(std::move(command), on_line,
                       on_start ? std::function<void(HANDLE)>([on_start](HANDLE h) { on_start(h); })
                                : std::function<void(HANDLE)>(),
                       read_stdout);
}

std::wstring quote_arg(const std::wstring & arg) {
    return quote(arg);
}

// ---------------------------------------------------------------------------- Dossiers de l'application (1.0.30)

// Dossier du programme et dossier des données, fixés par Paths::locate (les outils de la même application
// les partagent : le Créateur de styles appelle aussi locate).
static fs::path g_app_root, g_app_data, g_app_config, g_app_cache;

// ---- Dossiers des morceaux (1.0.43) : Audios\<Groupe>\<Album>\<NN. Titre>\ (sans album : Audios\<Groupe>\<Titre>\).
// Le dossier de chaque morceau est retrouvé par son identifiant (meta.json), où qu'il soit sous Audios.
static std::mutex                      g_folders_m;
static std::map<std::string, fs::path> g_folders;  // identifiant -> dossier du morceau
static fs::path                        g_audios;

// Nom de dossier lisible et valable sous Windows (caractères interdits remplacés, pas de point final, 80 caractères).
static std::wstring folder_component(const std::string & text, const wchar_t * fallback) {
    std::wstring w = widen(text), out;
    for (wchar_t c : w) out += (c < 32 || wcschr(L"\\/:*?\"<>|", c)) ? L'_' : c;
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    while (!out.empty() && out.front() == L' ') out.erase(out.begin());
    if (out.size() > 80) out.resize(80);
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    if (out.empty()) out = fallback;
    std::wstring up = out;
    CharUpperBuffW(up.data(), (DWORD) up.size());
    for (const wchar_t * r : { L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"LPT1", L"LPT2", L"LPT3" })
        if (up == r) out += L"_";
    return out;
}

fs::path album_folder(const std::string & artist, const std::string & album) {
    fs::path g = g_audios / folder_component(artist, L"Sans groupe");
    return album.empty() ? g : g / folder_component(album, L"Sans titre");
}

// Dossier voulu pour un morceau : « NN. Titre » si un numéro de piste est donné (titre qui n'en porte pas déjà un).
static fs::path wanted_folder(const Song & s) {
    std::string name = s.title;
    int         n    = atoi(s.track.c_str());
    bool        numbered = name.size() > 3 && isdigit((unsigned char) name[0]) && isdigit((unsigned char) name[1]) && name[2] == '.';
    if (n > 0 && !numbered) {
        char prefix[16];
        snprintf(prefix, sizeof(prefix), "%02d. ", n);
        name = prefix + name;
    }
    return album_folder(s.artist, s.album) / folder_component(name, L"Sans titre");
}

fs::path song_folder(const std::string & id) {
    std::lock_guard<std::mutex> lock(g_folders_m);
    auto                        it = g_folders.find(id);
    return it != g_folders.end() ? it->second : g_audios / L"_sans_dossier" / widen(id);
}

static void set_song_folder(const std::string & id, const fs::path & dir) {
    std::lock_guard<std::mutex> lock(g_folders_m);
    g_folders[id] = dir;
}

static void forget_song_folder(const std::string & id) {
    std::lock_guard<std::mutex> lock(g_folders_m);
    g_folders.erase(id);
}

// Dossiers vides laissés par un déplacement (album, groupe) : retirés, jamais au-dessus d'Audios.
static void remove_empty_parents(fs::path dir) {
    std::error_code ec;
    while (!g_audios.empty() && dir != g_audios && dir.native().size() > g_audios.native().size() && fs::is_directory(dir, ec) &&
           fs::is_empty(dir, ec)) {
        fs::remove(dir, ec);
        dir = dir.parent_path();
    }
}

static std::ofstream audios_log() {
    std::error_code ec;
    fs::create_directories(g_app_cache, ec);
    return std::ofstream(g_app_cache / L"migration.log", std::ios::binary | std::ios::app);
}

// Range le dossier d'un morceau à l'emplacement voulu par son groupe, son album et son titre. Un morceau dont le
// moteur peut encore écrire dans le dossier (en cours, ou en attente non suspendue) n'est pas déplacé : il le sera
// à son prochain enregistrement. Un déplacement refusé par Windows (fichier ouvert) laisse le dossier où il est.
static void place_song(const Song & s, bool force) {
    fs::path current;
    {
        std::lock_guard<std::mutex> lock(g_folders_m);
        auto                        it = g_folders.find(s.id);
        if (it != g_folders.end()) current = it->second;
    }
    bool movable = s.status == "done" || s.status == "error" || s.status == "cancelled" || (s.status == "queued" && s.held);
    if (!current.empty() && !movable && !force) return;
    fs::path want = wanted_folder(s);
    std::error_code ec;
    if (!current.empty() && current.parent_path() == want.parent_path()) {
        std::wstring a = current.filename().wstring(), b = want.filename().wstring();
        if (a.rfind(b, 0) == 0) return;  // déjà là (éventuellement « Titre (2) »)
    }
    fs::path target = want;
    for (int n = 2; fs::exists(target, ec) && target != current; n++)
        target = want.parent_path() / (want.filename().wstring() + L" (" + std::to_wstring(n) + L")");
    if (current.empty() || !fs::exists(current, ec)) {
        set_song_folder(s.id, target);
        return;
    }
    fs::create_directories(target.parent_path(), ec);
    fs::rename(current, target, ec);
    audios_log() << narrow(current.wstring()) << " -> " << narrow(target.wstring()) << (ec ? " : ECHEC " + ec.message() : std::string()) << "\n";
    if (ec) return;
    set_song_folder(s.id, target);
    remove_empty_parents(current.parent_path());
}

fs::path app_data_dir() {
    return g_app_data;
}

fs::path app_config_dir() {
    return g_app_config;
}

fs::path app_temp_dir() {
    std::error_code ec;
    if (!g_app_cache.empty()) {
        fs::path t = g_app_cache / "temp";
        fs::create_directories(t, ec);
        if (fs::is_directory(t, ec)) return t;
    }
    return fs::temp_directory_path(ec);  // avant locate, ou dossier en lecture seule : dossier temporaire de Windows
}

fs::path app_resource_dir(const wchar_t * name) {
    return g_app_root / L"ressources" / name;  // 1.0.42 : plus d'ancien emplacement (aide et langues y sont migrées)
}

// Chemin dans le dossier du programme ? Comparaison des chemins canoniques (liens, « .. » et majuscules résolus), pour
// qu'aucune suppression ne puisse atteindre un fichier d'un autre dossier (1.0.32, incident de la 1.0.31).
bool inside_app_dir(const fs::path & p) {
    if (g_app_root.empty()) return false;
    std::error_code ec, ec2;
    fs::path        root = fs::weakly_canonical(g_app_root, ec), file = fs::weakly_canonical(p, ec2);
    if (ec || ec2) return false;
    std::wstring r = root.wstring(), f = file.wstring();
    CharLowerBuffW(r.data(), (DWORD) r.size());
    CharLowerBuffW(f.data(), (DWORD) f.size());
    while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return f.size() > r.size() && f.compare(0, r.size(), r) == 0 && (f[r.size()] == L'\\' || f[r.size()] == L'/');
}

// ffmpeg et ffprobe : <app>\outils\ffmpeg (1.0.42 : seul emplacement, ffmpeg.exe de la racine y est migré), sinon le
// PATH. Résolu une seule fois ; le chemin trouvé est renvoyé entre guillemets, prêt pour une ligne de commande.
static std::wstring find_tool(const wchar_t * exe) {
    std::error_code ec;
    fs::path        dir = g_app_root / L"outils" / L"ffmpeg";
    if (!g_app_root.empty() && fs::exists(dir / exe, ec)) return quote((dir / exe).wstring());
    return fs::path(exe).stem().wstring();  // « ffmpeg » : recherche par Windows (PATH)
}

// Mis en cache dès que le dossier du programme est connu (avant Paths::locate, on cherche à chaque appel).
static const std::wstring & cached_tool(std::wstring & cache, const wchar_t * exe) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    if (cache.empty() || g_app_root.empty()) cache = find_tool(exe);
    return cache;
}

const std::wstring & ffmpeg_exe() {
    static std::wstring p;
    return cached_tool(p, L"ffmpeg.exe");
}

const std::wstring & ffprobe_exe() {
    static std::wstring p;
    return cached_tool(p, L"ffprobe.exe");
}

// Données de l'utilisateur rangées dans <app>\donnees (1.0.30). Au premier lancement d'une version 1.0.30, celles
// qui étaient à la racine du programme (versions précédentes) y sont déplacées, sans jamais sortir du dossier du
// programme ni écraser un fichier déjà présent dans donnees.
static void migrate_data(const fs::path & root, const fs::path & data) {
    std::error_code ec;
    for (const wchar_t * name : { L"studio_reglages.json", L"studio_brouillon.json", L"studio_modeles.txt", L"pochettes", L"sante" }) {
        fs::path from = root / name, to = data / name;
        if (fs::exists(from, ec) && !fs::exists(to, ec)) fs::rename(from, to, ec);
    }
}

// 1.0.41 : fichiers jetables de donnees\ rangés dans cache\ (temp, sante, brouillon). Même règle : déplacement dans le
// dossier du programme, jamais d'écrasement. Les réglages (donnees\studio_reglages.json) sont éclatés dans config\ par
// le Studio lui-même (main.cpp, migrate_settings), qui garde l'ancien fichier sous _ancien_studio_reglages.json.
static void migrate_cache(const fs::path & data, const fs::path & cache) {
    std::error_code ec;
    for (const auto & [from, to] : { std::pair<const wchar_t *, const wchar_t *>{ L"temp", L"temp" }, { L"sante", L"sante" },
                                     { L"studio_brouillon.json", L"brouillon.json" } }) {
        if (fs::exists(data / from, ec) && !fs::exists(cache / to, ec)) fs::rename(data / from, cache / to, ec);
    }
}

// 1.0.42 : rangement des modèles et des outils. Déplacement (même disque, instantané) sans jamais écraser : si la
// destination existe déjà, son contenu est complété élément par élément et ce qui reste en double est laissé en place
// et noté. Chaque déplacement est écrit dans cache\migration.log.
static void move_entry(const fs::path & from, const fs::path & to, std::ofstream & log) {
    std::error_code ec;
    if (!fs::exists(from, ec)) return;
    if (!fs::exists(to, ec)) {
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        log << narrow(from.wstring()) << " -> " << narrow(to.wstring()) << (ec ? " : ECHEC " + ec.message() : std::string()) << "\n";
        return;
    }
    if (fs::is_directory(from, ec) && fs::is_directory(to, ec)) {
        std::vector<fs::path> children;
        for (const auto & e : fs::directory_iterator(from, ec)) children.push_back(e.path());
        for (const fs::path & c : children) move_entry(c, to / c.filename(), log);
        if (fs::is_empty(from, ec)) fs::remove(from, ec);  // dossier vide : plus rien à garder
        else log << narrow(from.wstring()) << " : laisse en place (deja present dans " << narrow(to.wstring()) << ")\n";
        return;
    }
    log << narrow(from.wstring()) << " : laisse en place (" << narrow(to.wstring()) << " existe deja)\n";
}

static void migrate_layout(const fs::path & root) {
    std::error_code ec;
    bool            old = false;
    for (const wchar_t * n : { L"models", L"moteur", L"moteur_cuda", L"whisper", L"traduction", L"ia", L"ffmpeg.exe", L"ffprobe.exe",
                               L"aide", L"langues" })
        old |= fs::exists(root / n, ec);
    fs::path m = root / L"modeles";
    for (const wchar_t * n : { L"gguf", L"adapters", L"analysis", L"whisper", L"YuE2-3B", L"YuE2-Vae" }) old |= fs::exists(m / n, ec);
    if (!old) return;
    fs::create_directories(root / L"cache", ec);
    std::ofstream log(root / L"cache" / L"migration.log", std::ios::binary | std::ios::app);
    log << "---- rangement 1.0.42\n";
    move_entry(root / L"models", m, log);
    for (const auto & [from, to] : { std::pair<const wchar_t *, const wchar_t *>{ L"gguf", L"musique" }, { L"adapters", L"lora" },
                                     { L"analysis", L"analyse" }, { L"whisper", L"voix" }, { L"YuE2-3B", L"entrainement\\YuE2-3B" },
                                     { L"YuE2-Vae", L"entrainement\\YuE2-Vae" } })
        move_entry(m / from, m / to, log);
    fs::path o = root / L"outils";
    move_entry(root / L"moteur", o / L"moteur", log);
    move_entry(root / L"moteur_cuda", o / L"moteur" / L"cuda", log);
    move_entry(root / L"whisper", o / L"whisper", log);
    move_entry(root / L"traduction", o / L"llama", log);        // llama.cpp processeur (traduction, IA de secours)
    move_entry(root / L"ia" / L"llama", o / L"llama-vulkan", log);  // llama.cpp carte graphique
    if (fs::is_directory(root / L"ia", ec) && fs::is_empty(root / L"ia", ec)) fs::remove(root / L"ia", ec);
    for (const wchar_t * f : { L"ffmpeg.exe", L"ffprobe.exe" }) move_entry(root / f, o / L"ffmpeg" / f, log);
    move_entry(root / L"aide", root / L"ressources" / L"aide", log);
    move_entry(root / L"langues", root / L"ressources" / L"langues", log);
}

// Fusionne un dossier dans un autre sans rien écraser (pochettes et vidéos d'un album renommé), avec journal.
void merge_folder(const fs::path & from, const fs::path & to) {
    std::ofstream log = audios_log();
    move_entry(from, to, log);
    remove_empty_parents(from.parent_path());
}

// 1.0.43 : l'ancienne bibliotheque\<identifiant>\ est rangée dans Audios\<Groupe>\<Album>\<NN. Titre>\. Le meta.json de
// chaque morceau est d'abord copié dans cache\sauvegarde_meta_1.0.43\ ; chaque déplacement est noté dans
// cache\migration.log ; ce qui ne peut pas être déplacé reste dans bibliotheque\.
static void migrate_library(const fs::path & root) {
    std::error_code ec;
    fs::path        old = root / L"bibliotheque";
    if (!fs::is_directory(old, ec)) return;
    fs::path backup = g_app_cache / L"sauvegarde_meta_1.0.43";
    audios_log() << "---- rangement 1.0.43 : bibliotheque -> Audios\n";
    std::vector<fs::path> dirs;
    for (const auto & e : fs::directory_iterator(old, ec))
        if (e.is_directory() && fs::exists(e.path() / L"meta.json")) dirs.push_back(e.path());
    for (const fs::path & d : dirs) {
        Song s;
        if (!load_song(d / L"meta.json", s)) continue;
        if (s.id.empty()) s.id = narrow(d.filename().wstring());
        fs::create_directories(backup, ec);
        fs::copy_file(d / L"meta.json", backup / (d.filename().wstring() + L".json"), fs::copy_options::skip_existing, ec);
        set_song_folder(s.id, d);
        place_song(s, true);
    }
    if (fs::is_empty(old, ec)) fs::remove(old, ec);
    else audios_log() << narrow(old.wstring()) << " : non vide, laisse en place\n";
}

// Renseigne les chemins à partir du dossier de l'application (root) et de celui du moteur (bin).
static void fill_paths(const fs::path & root, const fs::path & bin, Paths & out) {
    std::error_code ec;
    out.root       = root;
    out.synth      = bin / "yue-synth.exe";
    out.transcribe = bin / "yue-transcribe.exe";
    // Whisper : dossier à part (ses bibliothèques GGML diffèrent de celles du moteur).
    out.whisper = root / "outils" / "whisper" / "whisper-cli.exe";
    out.library = root / "Audios";  // 1.0.43 : Audios\<Groupe>\<Album>\<NN. Titre>
    fs::create_directories(out.library, ec);
    g_audios = out.library;
    out.data = root / "donnees";
    fs::create_directories(out.data, ec);
    migrate_data(root, out.data);
    out.config = root / "config";
    out.cache  = root / "cache";
    fs::create_directories(out.config, ec);
    fs::create_directories(out.cache, ec);
    migrate_cache(out.data, out.cache);
    g_app_root   = root;
    g_app_data   = out.data;
    g_app_config = out.config;
    g_app_cache  = out.cache;
    migrate_library(root);
    // Moteur CUDA facultatif, dans un dossier à part : ses DLL ggml diffèrent de celles du moteur Vulkan.
    for (const fs::path & c : { root / "outils" / "moteur" / "cuda", root / "yue2.cpp/build_cuda/Release" }) {
        if (fs::exists(c / "yue-synth.exe", ec) && fs::exists(c / "ggml-cuda.dll", ec)) {
            out.cuda_dir = c;
            break;
        }
    }
}

bool Paths::locate(const fs::path & exe_dir, Paths & out, bool shared_root) {
    std::error_code ec;
    // Livrable : moteur dans <exe>\outils\moteur (1.0.42). On ne remonte jamais dans les dossiers parents : un autre
    // Studio placé plus haut ne doit pas être utilisé. Les anciens dossiers sont d'abord rangés (migrate_layout).
    if (!shared_root) migrate_layout(exe_dir);
    for (const fs::path & bin : { exe_dir / "outils" / "moteur" }) {
        if (fs::exists(bin / "yue-synth.exe", ec)) {
            fill_paths(exe_dir, bin, out);
            return true;
        }
    }
    // Dossier de développement : le moteur compilé est dans yue2.cpp\build\Release, éventuellement plus haut
    // (outils rangés dans un sous-dossier, comme le Créateur de styles). La remontée ne sert qu'à trouver le moteur :
    // pour le Studio, models, donnees et bibliotheque restent ceux du dossier de l'exe (1.0.32).
    // 1.0.44 : un outil d'Admin_tools (shared_root) trouve aussi le moteur d'un Studio installé (outils\moteur).
    for (fs::path dir = exe_dir; !dir.empty(); dir = dir.parent_path()) {
        for (const fs::path & bin : { dir / "yue2.cpp/build/Release", dir / "yue2.cpp/build", dir,
                                      shared_root ? dir / "outils" / "moteur" : dir / "yue2.cpp/build/Release" }) {
            if (fs::exists(bin / "yue-synth.exe", ec)) {
                fill_paths(shared_root ? dir : exe_dir, bin, out);
                return true;
            }
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return false;
}

bool Paths::use_cuda_engine() {
    if (cuda_dir.empty()) return false;
    synth = cuda_dir / "yue-synth.exe";
    if (fs::exists(cuda_dir / "yue-transcribe.exe")) transcribe = cuda_dir / "yue-transcribe.exe";
    return true;
}

bool Paths::use_models(const fs::path & dir) {
    for (const fs::path & g : { dir / "musique" }) {  // 1.0.42 : modeles\musique
        if (!fs::exists(g / "YuE2-Vae-F32.gguf")) continue;
        fs::path    chosen;  // modèle par défaut : Q6_K, à défaut Q8_0, Q5_K_M, puis n'importe quel YuE2-3B-*.gguf
        for (const char * name : { "YuE2-3B-Q6_K.gguf", "YuE2-3B-Q8_0.gguf", "YuE2-3B-Q5_K_M.gguf" }) {
            if (fs::exists(g / name)) {
                chosen = g / name;
                break;
            }
        }
        if (chosen.empty()) {
            std::error_code ec;
            for (const auto & e : fs::directory_iterator(g, ec)) {
                std::string name = narrow(e.path().filename().wstring());
                if (name.rfind("YuE2-3B-", 0) == 0 && e.path().extension() == ".gguf") {
                    chosen = e.path();
                    break;
                }
            }
        }
        if (!chosen.empty()) {
            models_dir  = dir;
            gguf_dir    = g;
            model       = chosen;
            vae         = g / "YuE2-Vae-F32.gguf";
            transcriber = g / "SheetSage2-Q8_0.gguf";
            adapters    = dir / "lora";  // 1.0.42 : modeles\lora (LoRA et leurs fiches .json)
            return true;
        }
    }
    return false;
}

std::vector<std::string> Paths::list_models() const {
    std::vector<std::pair<uintmax_t, std::string>> found;
    std::error_code                                ec;
    for (const auto & e : fs::directory_iterator(gguf_dir, ec)) {
        std::string name = narrow(e.path().filename().wstring());
        if (name.rfind("YuE2-3B-", 0) == 0 && e.path().extension() == ".gguf") {
            found.push_back({ e.file_size(ec), name });
        }
    }
    std::sort(found.begin(), found.end(), [](const auto & a, const auto & b) { return a.first > b.first; });
    std::vector<std::string> names;
    for (auto & f : found) names.push_back(f.second);
    return names;
}

fs::path Paths::model_for(const std::string & name) const {
    if (!name.empty() && name.find_first_of("/\\") == std::string::npos) {
        fs::path p = gguf_dir / widen(name);
        if (fs::exists(p)) return p;
    }
    return model;
}

// ---------------------------------------------------------------------------- Studio

Studio::Studio(Paths paths, std::function<void()> notify) : paths_(std::move(paths)), notify_(std::move(notify)) {
    rescan();
    // Reprise après une fermeture : la file revient en pause (lancement manuel), une génération
    // coupée net devient « interrompue » et peut être relancée.
    bool synth = other_synth_running();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto & [id, s] : songs_) {
            if (s.status == "queued") {
                s.local = true;
                s.held  = true;
                s.stage = tr("En pause");
                save(s);
            } else if (s.status == "running" && !synth) {
                s.status   = "interrupted";
                s.stage    = tr("Interrompue");
                s.message  = tr("L'application s'est fermée pendant la génération.");
                s.progress = 0;
                save(s);
            }
        }
    }
    thread_ = std::thread([this] { worker(); });
}

Studio::~Studio() {
    shutdown();
    if (thread_.joinable()) {
        thread_.join();
    }
    if (health_thread_.joinable()) {
        health_thread_.join();
    }
}

void Studio::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        if (maintenance_ && process_) TerminateProcess((HANDLE) process_, 1);  // test de santé en cours
        if (!running_id_.empty()) {
            cancel_running_ = true;
            stop_kind_      = "interrupted";
            if (process_) TerminateProcess((HANDLE) process_, 1);
        }
    }
    cv_.notify_all();
}

bool Studio::can_transcribe() const {
    return fs::exists(paths_.transcribe) && fs::exists(paths_.transcriber);
}

std::vector<Song> Studio::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Song>           out;
    out.reserve(songs_.size());
    for (auto & [id, s] : songs_) {
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const Song & a, const Song & b) { return a.created > b.created; });
    return out;
}

void Studio::set_active_model(const std::string & name) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_model_ = name;
}

void Studio::set_fade_seconds(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    fade_default_ = std::clamp(seconds, 1.0, 30.0);
}

void Studio::set_active_solver(const std::string & name) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_solver_ = name;
}

std::string Studio::active_solver() {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_solver_;
}

std::string Studio::active_model() {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_model_;
}

std::string Studio::submit(Song song, bool held) {
    static std::mt19937 rng(std::random_device{}());
    char                stamp[32], suffix[8];
    time_t              t = time(nullptr);
    tm                  local{};
    localtime_s(&local, &t);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S-", &local);
    snprintf(suffix, sizeof(suffix), "%04x", (unsigned) (rng() & 0xFFFF));
    song.id       = std::string(stamp) + suffix;
    if (!song.instrumental && !song.lyrics.empty()) song.lyrics_lang = detect_lyrics_language(song.style, song.lyrics, &song.lyrics_lang_declared);
    song.status   = "queued";
    song.held     = held;
    song.stage    = held ? "En pause" : "En attente";
    song.progress = 0;
    song.created  = now_seconds();
    song.local    = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (song.model.empty()) song.model = active_model_;  // sinon : modèle imposé par l'appelant
        if (song.solver.empty()) song.solver = active_solver_;
        while (songs_.count(song.id)) {  // plusieurs versions soumises dans la même seconde
            snprintf(suffix, sizeof(suffix), "%04x", (unsigned) (rng() & 0xFFFF));
            song.id = std::string(stamp) + suffix;
        }
        songs_[song.id] = song;
        save(song);
    }
    cv_.notify_all();
    notify_();
    return song.id;
}

void Studio::cancel(const std::string & id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) {
            return;
        }
        Song & s = it->second;
        if (s.status == "queued") {
            s.status = "cancelled";
            s.stage  = tr("Annulée");
            save(s);
        } else if (s.status == "running" && id == running_id_) {
            cancel_running_ = true;
            stop_kind_      = "cancelled";
            if (process_) {
                TerminateProcess((HANDLE) process_, 1);
            }
        } else if (s.status == "running" && !s.local && !other_synth_running()) {
            s.status  = "error";
            s.stage   = tr("Erreur");
            s.message = tr("Interrompue (aucune génération en cours)");
            save(s);
        }
    }
    cv_.notify_all();
    notify_();
}

void Studio::cancel_all_local() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto & [id, s] : songs_) {
            if (s.local && s.active()) {
                ids.push_back(id);
            }
        }
    }
    for (auto & id : ids) {
        cancel(id);
    }
}

bool Studio::local_active() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !running_id_.empty();
}

void Studio::set_held(const std::string & id, bool held) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) return;
        Song & s = it->second;
        if (s.status == "queued") {
            s.held  = held;
            s.local = true;
            s.stage = held ? "En pause" : "En attente";
            save(s);
        } else if (s.status == "running" && held && id == running_id_) {
            cancel_running_ = true;  // arrêt : le morceau repart en file, en pause (reprise depuis le début)
            stop_kind_      = "paused";
            if (process_) TerminateProcess((HANDLE) process_, 1);
        }
    }
    cv_.notify_all();
    notify_();
}

void Studio::set_heard(const std::string & id, bool heard) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end() || it->second.heard == heard) return;
        it->second.heard = heard;
        save(it->second);
    }
    notify_();
}

void Studio::set_validated(const std::string & id, bool validated) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) return;
        it->second.validated = validated;
        if (validated) it->second.flagged = false;  // un morceau ne peut pas être validé et à problème à la fois
        save(it->second);
    }
    notify_();
}

void Studio::set_flagged(const std::string & id, bool flagged) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) return;
        it->second.flagged = flagged;
        if (flagged) it->second.validated = false;
        save(it->second);
    }
    notify_();
}

void Studio::hold_all() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto & [id, s] : songs_) {
            if (s.local && s.active()) ids.push_back(id);
        }
    }
    for (auto & id : ids) set_held(id, true);
}

void Studio::resume_all() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto & [id, s] : songs_) {
            if (s.status == "queued" && s.held) {
                s.held  = false;
                s.local = true;
                s.stage = tr("En attente");
                save(s);
            }
        }
    }
    cv_.notify_all();
    notify_();
}

void Studio::relaunch(const std::string & id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end() || it->second.active()) return;
        Song & s = it->second;
        s.status = "queued";
        s.held   = false;
        s.local  = true;
        s.stage  = tr("En attente");
        s.message.clear();
        s.progress = s.started = s.finished = s.eta = s.eta_at = s.audio_seconds = 0;
        s.truncated = s.has_used_seeds = false;
        s.created   = now_seconds();  // repart en fin de file
        save(s);
    }
    cv_.notify_all();
    notify_();
}

void Studio::edit_metadata(const std::string & id, const std::string & title, const std::string & artist,
                           const std::string & album, const std::string & track) {
    Song song;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) return;
        it->second.title  = title;
        it->second.artist = artist;
        it->second.album  = album;
        it->second.track  = track;
        save(it->second);
        song = it->second;
    }
    notify_();
    if (song.status != "done") return;
    // Réécriture des étiquettes des fichiers (copie sans réencodage), en arrière-plan.
    fs::path folder = song_folder(id);  // après le déplacement éventuel fait par save
    std::thread([folder, song] {
        for (const wchar_t * name : { L"audio.flac", L"audio.mp3" }) {
            fs::path in = folder / name, tmp = folder / (std::wstring(L"tmp_") + name);
            if (!fs::exists(in)) continue;
            std::wstring cmd = ffmpeg_exe() + L" -v error -y -i " + quote(in.wstring()) + L" -map 0 -c copy -metadata " +
                               quote(L"title=" + widen(song.title)) + L" -metadata " +
                               quote(L"artist=" + widen(song.artist.empty() ? "YuE2" : song.artist)) +
                               L" -metadata " + quote(L"album=" + widen(song.album)) + L" -metadata " +
                               quote(L"track=" + widen(song.track)) + L" " + quote(tmp.wstring());
            if (run_process(cmd, nullptr, nullptr) == 0 && fs::exists(tmp)) {
                std::error_code ec;
                fs::rename(tmp, in, ec);
                if (ec) fs::remove(tmp, ec);
            }
        }
    }).detach();
}

bool Studio::pick_next(std::string & id) {
    double best = 0;
    id.clear();
    if (gate_ || maintenance_) return false;  // jeu en cours ou test de santé : la carte graphique reste libre
    for (auto & [sid, s] : songs_) {
        if (s.local && s.waiting() && (id.empty() || s.created < best)) {
            id   = sid;
            best = s.created;
        }
    }
    return !id.empty();
}

void Studio::finish_stopped(const std::string & id, const fs::path & wav) {
    std::string kind;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        kind = stop_kind_;
        running_id_.clear();
    }
    std::error_code ec;
    fs::remove(wav, ec);
    update(id, [&](Song & s) {
        s.progress = 0;
        s.eta      = 0;
        if (kind == "paused") {
            s.status  = "queued";
            s.held    = true;
            s.stage   = tr("En pause");
            s.started = 0;
        } else if (kind == "gate") {
            s.status  = "queued";  // remis en attente : repart tout seul quand le jeu se ferme
            s.held    = false;
            s.stage   = tr("En attente (jeu en cours)");
            s.started = 0;
        } else if (kind == "interrupted") {
            s.status   = "interrupted";
            s.stage    = tr("Interrompue");
            s.message  = tr("Génération arrêtée par la fermeture de l'application.");
            s.finished = now_seconds();
        } else {
            s.status   = "cancelled";
            s.stage    = tr("Annulée");
            s.finished = now_seconds();
            s.local    = false;
        }
    });
}

// Envoie des dossiers à la corbeille de Windows (récupérables). Les chemins absents sont ignorés.
static bool send_to_recycle_bin(const std::vector<fs::path> & paths, std::string & error) {
    std::wstring list;
    for (const fs::path & p : paths) {
        std::error_code ec;
        if (!fs::exists(p, ec)) continue;
        if (!inside_app_dir(p)) {  // 1.0.32 : jamais hors du dossier du programme
            error = trf("Suppression refusée : « %s » est hors du dossier du programme.", narrow(p.wstring()).c_str());
            return false;
        }
        fs::path abs = fs::absolute(p, ec);
        abs.make_preferred();
        list += abs.wstring();
        list.push_back(L'\0');
    }
    if (list.empty()) return true;
    list.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc           = FO_DELETE;
    op.pFrom           = list.c_str();
    op.fFlags          = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    int r              = SHFileOperationW(&op);
    if (r != 0 || op.fAnyOperationsAborted) {
        error = trf("La corbeille de Windows a refusé la suppression (code %d).", r);
        return false;
    }
    return true;
}

bool Studio::remove(const std::string & id, std::string & error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) {
            return true;
        }
        if (it->second.active()) {
            error = tr("Annulez la génération avant de la supprimer.");
            return false;
        }
    }
    // dossier du morceau envoyé à la corbeille (récupérable) ; la fiche n'est retirée qu'en cas de succès
    fs::path folder = song_folder(id);
    if (!send_to_recycle_bin({ folder }, error)) {
        return false;
    }
    forget_song_folder(id);
    remove_empty_parents(folder.parent_path());  // album ou groupe vide (sans pochette ni vidéo)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        songs_.erase(id);
        mtimes_.erase(id);
    }
    notify_();
    return true;
}

// 1.0.46 : dossier entier (album) envoyé à la corbeille, avec ses morceaux, pochettes, vidéos et exports. Refusé si un
// morceau du dossier est en cours ou en attente, ou hors du dossier du programme (send_to_recycle_bin).
bool Studio::remove_folder(const fs::path & dir, std::string & error) {
    std::wstring prefix = dir.wstring() + L"\\";
    auto         inside = [&](const std::string & id) { return song_folder(id).wstring().rfind(prefix, 0) == 0; };
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto & [id, s] : songs_) {
            if (!inside(id)) continue;
            if (s.active()) {
                error = tr("Des morceaux de cet album sont en cours ou en attente de génération : annulez-les d'abord.");
                return false;
            }
            ids.push_back(id);
        }
    }
    if (!send_to_recycle_bin({ dir }, error)) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string & id : ids) {
            songs_.erase(id);
            mtimes_.erase(id);
            forget_song_folder(id);
        }
    }
    remove_empty_parents(dir.parent_path());  // groupe sans autre album
    notify_();
    return true;
}

int Studio::remove_all(std::string & error) {
    std::vector<std::string> ids;
    std::vector<fs::path>    folders;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto & [id, s] : songs_) {
            if (s.active()) {
                error = tr("Une génération est en cours ou en attente : annulez-la avant de tout supprimer.");
                return -1;
            }
            ids.push_back(id);
            folders.push_back(song_folder(id));
        }
    }
    if (ids.empty()) {
        return 0;
    }
    if (!send_to_recycle_bin(folders, error)) {
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string & id : ids) {
            songs_.erase(id);
            mtimes_.erase(id);
        }
    }
    notify_();
    return (int) ids.size();
}

const std::vector<SolverSpec> & solver_specs() {
    static const std::vector<SolverSpec> specs = {
        { "ab3_16", "AB3 16", "ab3", 16, 17, N_("le plus rapide, écart perceptible sur les détails") },
        { "ab3_20", "AB3 20", "ab3", 20, 21, N_("précision proche d'AB2 32, environ 35 % plus rapide") },
        { "ab3_24", "AB3 24", "ab3", 24, 25, N_("précision proche de MP 32, environ 25 % plus rapide qu'AB2 32") },
        { "euler_32", "Euler 32", "euler", 32, 32, N_("méthode simple : niveau sonore et spectre altérés") },
        { "ab2_32", "AB2 32", "ab2", 32, 33, N_("solveur d'origine rapide") },
        { "ab3_32", "AB3 32", "ab3", 32, 33, N_("meilleure précision à vitesse d'AB2 32") },
        { "mp32", "MP 32", "midpoint", 32, 64, N_("référence, le plus lent") },
    };
    return specs;
}

const SolverSpec & solver_spec(const std::string & key) {
    // vide ou « midpoint » : morceaux d'avant le choix du solveur, générés en midpoint 32 pas
    const std::string k = key.empty() || key == "midpoint" ? "mp32" : key == "ab2" ? "ab2_32" : key;
    for (const SolverSpec & s : solver_specs()) {
        if (k == s.key) return s;
    }
    for (const SolverSpec & s : solver_specs()) {
        if (std::string(s.key) == "ab2_32") return s;
    }
    return solver_specs().front();
}

// Coût relatif de la phase NAR selon le solveur, en part du coût de MP 32 (64 évaluations).
static double solver_factor(const std::string & solver) {
    return solver_spec(solver).evals / 64.0;
}

void Studio::refit_model() {
    EtaModel m;
    // Étapes de synthèse : moindres carrés de step = a·x + b·x² (x = trames / 1000).
    double s22 = 0, s23 = 0, s24 = 0, sy1 = 0, sy2 = 0, score_t = 0, score_n = 0, sem_t = 0, sem_n = 0;
    double fill[2] = { 0, 0 }, fill_n[2] = { 0, 0 };
    int    points = 0;
    for (auto & [id, s] : songs_) {
        if (s.status != "done") {
            continue;
        }
        if (s.frames > 0 && s.nar_step_s > 0) {
            // le modèle apprend le coût d'un pas « midpoint » : un pas ab2 coûte 33/64 (une évaluation au lieu de deux)
            double x = s.frames / 1000.0, y = s.nar_step_s * solver_spec(s.solver).steps / 32.0 / solver_factor(s.solver);
            s22 += x * x;
            s23 += x * x * x;
            s24 += x * x * x * x;
            sy1 += x * y;
            sy2 += x * x * y;
            points++;
        }
        if (s.score_tokens > 0 && s.score_s > 0) {
            score_n += s.score_tokens;
            score_t += s.score_s;
        }
        if (s.sem_tokens > 0 && s.sem_s > 0 && s.kind == "new") {
            sem_n += s.sem_tokens;
            sem_t += s.sem_s;
            if (s.duration > 0) {
                fill[s.instrumental] += std::min(1.0, s.sem_tokens / (s.duration * 25.0));
                fill_n[s.instrumental]++;
            }
        }
    }
    double det = s22 * s24 - s23 * s23;
    if (points >= 2 && det > 1e-9) {
        double a = (sy1 * s24 - sy2 * s23) / det, b = (s22 * sy2 - s23 * sy1) / det;
        if (a >= 0 && b > 0) {
            m.step_a = a;
            m.step_b = b;
        }
    } else if (points == 1 && s22 > 0) {
        // Un seul point : on recale la courbe par défaut dessus.
        double x = std::sqrt(s22), k = (sy1 / x) / m.nar_step(x * 1000);
        m.step_a *= k;
        m.step_b *= k;
    }
    if (score_t > 0) m.score_rate = score_n / score_t;
    if (sem_t > 0) m.sem_rate = sem_n / sem_t;
    if (fill_n[0] > 0) m.fill_song = fill[0] / fill_n[0];
    if (fill_n[1] > 0) m.fill_inst = fill[1] / fill_n[1];
    model_ = m;
}

void Studio::rescan() {
    std::vector<std::pair<fs::path, fs::file_time_type>> metas;
    std::error_code                                      ec;
    // 1.0.43 : morceaux rangés sous Audios jusqu'à 3 niveaux (groupe, album, morceau) ; dossiers « videos » ignorés.
    for (auto it = fs::recursive_directory_iterator(paths_.library, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;
        if (it->path().filename() == L"videos" || it.depth() > 2) {
            it.disable_recursion_pending();
            continue;
        }
        fs::path meta = it->path() / "meta.json";
        if (fs::exists(meta, ec)) {
            metas.emplace_back(meta, fs::last_write_time(meta, ec));
            it.disable_recursion_pending();  // un morceau ne contient pas d'autre morceau
        }
    }
    bool                        changed = false;
    std::map<std::string, bool> seen;
    for (auto & [meta, mtime] : metas) {
        // identifiant : celui de meta.json (dossier nommé par le titre), mémorisé par dossier pour les scans suivants
        std::string id;
        {
            std::lock_guard<std::mutex> lock(g_folders_m);
            for (const auto & [k, v] : g_folders)
                if (v == meta.parent_path()) {
                    id = k;
                    break;
                }
        }
        if (id.empty()) {
            Song probe;
            if (!load_song(meta, probe)) continue;
            id = probe.id.empty() ? narrow(meta.parent_path().filename().wstring()) : probe.id;
            set_song_folder(id, meta.parent_path());
        }
        seen[id]       = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto                        it = songs_.find(id);
            if (it != songs_.end() && it->second.local && it->second.active()) {
                continue;
            }
            auto known = mtimes_.find(id);
            if (known != mtimes_.end() && known->second == mtime) {
                continue;
            }
        }
        Song song;
        if (!load_song(meta, song)) {
            continue;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        songs_[id]  = song;
        mtimes_[id] = mtime;
        changed     = true;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = songs_.begin(); it != songs_.end();) {
            bool keep = seen.count(it->first) || (it->second.local && it->second.active());
            if (!keep) {
                mtimes_.erase(it->first);
                it      = songs_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed) {
            refit_model();
        }
    }
    if (changed) {
        notify_();
    }
}

void Studio::update(const std::string & id, const std::function<void(Song &)> & fn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end()) {
            return;
        }
        fn(it->second);
        save(it->second);
    }
    notify_();
}

void Studio::set_heard_vocals(const std::string & id, const std::string & lang) {
    update(id, [&](Song & s) {
        s.heard_vocals_lang = lang;
        s.lyrics_lang_check = 2;
    });
}

void Studio::set_language_check(const std::string & id, const std::string & code, int check) {
    update(id, [&](Song & s) {
        if (!code.empty()) {
            s.lyrics_lang          = code;
            s.lyrics_lang_declared = false;
        }
        s.lyrics_lang_check = check;
    });
}

void Studio::worker() {
    while (!stop_) {
        std::string id;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || pick_next(id); });
            if (stop_) {
                return;
            }
        }
        // Un seul modèle sur le GPU : attendre la fin d'une génération lancée ailleurs (version web).
        bool waiting = false;
        while (!stop_ && other_synth_running()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!songs_[id].waiting()) {
                    break;
                }
            }
            if (!waiting) {
                update(id, [](Song & s) { s.stage = tr("En attente du GPU (autre génération en cours)"); });
                waiting = true;
            }
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(2));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || !songs_[id].waiting()) {
                continue;
            }
        }
        try {
            generate(id);
            launch_long_part(id);  // 1.0.48
        } catch (const std::exception & e) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_id_.clear();
            }
            update(id, [&](Song & s) {
                s.status   = "error";
                s.stage    = tr("Erreur");
                s.message  = e.what();
                s.finished = now_seconds();
                s.local    = false;
            });
        }
    }
}

// 1.0.48 : partie 1 d'un morceau long terminée → la partie 2 est mise en file, à la suite de ses 25 dernières secondes.
void Studio::launch_long_part(const std::string & id) {
    Song p;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = songs_.find(id);
        if (it == songs_.end() || it->second.status != "done" || it->second.long_rest.empty()) return;
        p = it->second;
    }
    Song n;
    n.kind              = "extend";
    n.parent            = p.id;
    n.parent_title      = p.title;
    std::string title = p.title;
    for (const std::string suffix : { std::string(tr(" (partie 1)")), std::string(tr(" (partie 2)")) })
        if (title.size() > suffix.size() && title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0) title.resize(title.size() - suffix.size());
    // 1.0.70 : morceau en 3 parties : la partie 2 garde la suite des paroles, et la partie 3 reprend tout le morceau assemblé
    const size_t mark = p.long_rest.find(LONG_PART_MARK);
    std::string  part = p.long_rest.substr(0, mark);
    if (mark != std::string::npos) {
        n.long_rest = p.long_rest.substr(mark + strlen(LONG_PART_MARK));
        title      += tr(" (partie 2)");
    }
    n.title             = title;
    n.style             = p.style;
    n.lyrics            = part;
    n.instrumental      = p.instrumental;
    n.instrumental_lora = p.instrumental_lora;
    n.adapters          = p.adapters;
    n.negative_style    = p.negative_style;
    n.negative_scale    = p.negative_scale;
    n.fade_seconds      = p.fade_seconds;
    n.model             = p.model;
    n.solver            = p.solver;
    n.artist            = p.artist;
    n.album             = p.album;
    n.track             = p.track;
    n.long_join         = 25;
    n.auto_duration     = true;
    n.lm_seed = n.seed  = -1;
    double rest         = estimate_seconds(part, p.instrumental);
    n.duration          = p.instrumental ? std::min(360.0, n.long_join + rest) : 360;
    n.expected_seconds  = std::min(360.0, n.long_join + rest);
    submit(n);
}

// Suivi d'une génération : progression, mesures et estimation du temps restant.
struct RunState {
    EtaModel model;
    double   solver = 1;  // coût relatif de la phase NAR du solveur choisi (1 = MP 32)
    int      nar_steps = 32;
    double   duration = 180, expected_frames = 0;
    bool     has_score = true, has_semantic = true;
    double   phase_t0 = 0, last_step_t = 0, step_sum = 0;
    int      steps_done = 0;
    double   tail = 12;  // décodage, conversions et chargements
};

static double remaining_after_semantic(const RunState & r, double frames) {
    return 32 * r.model.nar_step(frames) * r.solver + r.tail;
}

void Studio::generate(const std::string & id) {
    Song song;
    Song parent;
    bool has_parent = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        song            = songs_[id];
        running_id_     = id;
        cancel_running_ = false;
        stop_kind_.clear();
        if (!song.parent.empty() && songs_.count(song.parent)) {
            parent     = songs_[song.parent];
            has_parent = true;
        }
    }
    fs::path folder = song_folder(id);
    fs::path wav    = folder / "audio.wav";
    auto     check_cancel = [&] {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancel_running_;
    };
    auto track = [&](HANDLE h) {
        std::lock_guard<std::mutex> lock(mutex_);
        process_ = h;
        if (h && cancel_running_) {
            TerminateProcess(h, 1);
        }
    };
    update(id, [](Song & s) {
        s.status   = "running";
        s.stage    = tr("Préparation");
        s.progress = 0.01;
        s.started  = now_seconds();
    });

    RunState run;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        run.model  = model_;
        run.solver    = solver_factor(song.solver);
        run.nar_steps = solver_spec(song.solver).steps;
    }
    run.duration = song.duration;

    // ---- Requête selon l'origine du morceau
    yyjson_mut_doc * doc     = nullptr;
    bool             adapter = false;
    if (song.kind == "rerender" || song.kind == "extend") {
        if (!has_parent) {
            throw std::runtime_error(tr("Le morceau d'origine n'est plus dans la bibliothèque."));
        }
        doc = read_replay(song_folder(song.parent) / "audio.json");
        if (!doc) {
            throw std::runtime_error(tr("Codes du morceau d'origine introuvables (audio.json)."));
        }
        yyjson_mut_val * o = yyjson_mut_doc_get_root(doc);
        adapter            = yyjson_mut_obj_get(o, "adapters") != nullptr;
        put_int(doc, o, "seed", song.seed);
        put_str(doc, o, "output_format", "wav24");
        put_int(doc, o, "peak_clip", 0);  // pas d'écrêtage volontaire (voir new_request)
        if (!song.solver.empty()) {
            const SolverSpec & sp = solver_spec(song.solver);
            put_str(doc, o, "solver", sp.engine);
            put_int(doc, o, "steps", sp.steps);
        }
        if (song.kind == "rerender") {
            // Même composition (codes et partition), nouvelle synthèse : on saute la partition et la composition.
            put_str(doc, o, "cot", parent.cot);
            run.has_score = run.has_semantic = false;
            const char * tokens = yyjson_mut_get_str(yyjson_mut_obj_get(o, "semantic_tokens"));
            run.expected_frames = tokens ? (double) (std::count(tokens, tokens + strlen(tokens), ',') + 1) : 0;
        } else {
            // Suite : les codes du morceau ouvrent l'invite, le modèle compose la suite jusqu'à la durée totale.
            yyjson_mut_val * tokens = yyjson_mut_obj_get(o, "semantic_tokens");
            std::string      codes  = tokens ? yyjson_mut_get_str(tokens) : "";
            yyjson_mut_obj_remove_key(o, "semantic_tokens");
            yyjson_mut_obj_remove_key(o, "abc");
            put_str(doc, o, "source_tokens", codes);
            // 1.0.48 : partie 2 d'un morceau long : seules les dernières secondes de la partie 1 ouvrent la suite
            // 1.0.70 : les codes d'une partie 2 ne couvrent que cette partie, pas le morceau assemblé
            double source_seconds = parent.audio_seconds;
            if (parent.long_join > 0) {
                double alone = media_duration(song_folder(parent.id) / "partie2_seule.wav");
                if (alone > 0) source_seconds = alone;
            }
            put_num(doc, o, "source_start", song.long_join > 0 ? std::max(0.0, source_seconds - song.long_join) : 0);
            put_num(doc, o, "source_end", -1);
            put_str(doc, o, "style", song.style);
            yyjson_mut_obj_remove_key(o, "negative_style");  // le rejeu du parent peut en contenir un
            yyjson_mut_obj_remove_key(o, "cfg_scale");
            add_negative(doc, o, song);
            put_str(doc, o, "lyrics", song.instrumental ? structure_only(song.lyrics) : song.lyrics);
            put_str(doc, o, "cot", "off");
            put_int(doc, o, "lm_seed", song.lm_seed);
            put_num(doc, o, "duration", song.duration);
            run.has_score       = false;
            run.expected_frames = song.duration * 25;
        }
    } else {
        doc = new_request(song, adapter);
        yyjson_mut_val * o = yyjson_mut_doc_get_root(doc);
        run.has_score       = song.cot != "off";
        run.expected_frames = song.expected_seconds > 0
                                  ? std::min(song.expected_seconds, song.duration) * 25
                                  : song.duration * 25 * (song.instrumental ? run.model.fill_inst : run.model.fill_song);
        if (song.kind == "cover") {
            // Reprise : la mélodie du fichier source est transcrite en partition, puis interprétée dans le nouveau style.
            update(id, [](Song & s) {
                s.stage    = tr("Transcription de la partition");
                s.progress = 0.03;
            });
            fs::path src = song.source_file;
            fs::path input = src;
            std::string ext = narrow(src.extension().wstring());
            for (auto & c : ext) c = (char) tolower((unsigned char) c);
            if (ext != ".mp3" && ext != ".wav") {
                input = folder / "source.wav";  // yue-transcribe ne lit que WAV et MP3
                std::wstring ff = ffmpeg_exe() + L" -v error -y -i " + quote(src.wstring()) + L" " + quote(input.wstring());
                if (run_process(ff, nullptr, track) != 0) {
                    throw std::runtime_error(trf("Impossible de lire le fichier source : %s", narrow(src.wstring()).c_str()));
                }
            }
            fs::path     abc_file = folder / "score.abc";
            std::wstring cmd = quote(paths_.transcribe.wstring()) + L" --no-fa --model " +
                               quote(paths_.transcriber.wstring()) + L" --audio " + quote(input.wstring()) +
                               L" --out " + quote(abc_file.wstring()) + (song.cot == "melody" ? L" --melody-only" : L"");
            std::string log, fatal;
            int code = run_process(cmd, [&](const std::string & l) {
                log += l + "\n";
                if (l.find("FATAL") != std::string::npos) fatal = l;
            }, track);
            write_file_atomic(folder / "transcription.log", log.data(), log.size());
            if (check_cancel()) {
                yyjson_mut_doc_free(doc);
                finish_stopped(id, wav);
                return;
            }
            std::string abc = read_file(abc_file);
            if (code != 0 || abc.empty()) {
                yyjson_mut_doc_free(doc);
                throw std::runtime_error(fatal.empty() ? std::string(tr("La transcription de la mélodie a échoué."))
                                                       : trf("La transcription de la mélodie a échoué : %s", fatal.c_str()));
            }
            put_str(doc, o, "abc", abc);
            run.has_score = false;  // partition fournie : pas d'étape d'écriture
        }
    }
    if (adapter) adapter = drop_missing_adapters(doc, paths_.adapters);  // LoRA supprimées : plus réclamées (1.0.29)
    std::string request = write_doc(doc);
    yyjson_mut_doc_free(doc);
    write_file_atomic(folder / "request.json", request.data(), request.size());

    // Estimation initiale
    auto initial_eta = [&] {
        double t = 6;  // chargement du modèle
        if (run.has_score) t += 1100 / run.model.score_rate;
        if (run.has_semantic) {
            double from = song.kind == "extend" && has_parent ? parent.audio_seconds * 25 : 0;
            t += std::max(0.0, run.expected_frames - from) / run.model.sem_rate;
        }
        return t + remaining_after_semantic(run, run.expected_frames);
    };
    double eta0 = initial_eta();
    update(id, [&](Song & s) {
        s.stage  = tr("Chargement du modèle");
        s.eta    = eta0;
        s.eta_at = now_seconds();
    });

    std::wstring cmd = quote(paths_.synth.wstring()) + L" " + engine_flags() + L" --model " +
                       quote(paths_.model_for(song.model).wstring()) + L" --vae " + quote(paths_.vae.wstring()) +
                       L" --request " + quote((folder / "request.json").wstring()) + L" --out " + quote(wav.wstring());
    if (adapter) {
        cmd += L" --adapters " + quote(paths_.adapters.wstring());
    }
    std::string log, fatal;
    Song        measured;  // mesures relevées dans le journal
    auto on_line = [&](const std::string & line) {
        log += line + "\n";
        if (line.find("FATAL") != std::string::npos) {
            fatal = line;
        }
        parse_timing_line(line, measured);
        if (line.rfind("[AR] Semantic song 0:", 0) == 0 && line.find("(truncated)") != std::string::npos) {
            measured.truncated = true;  // plafond atteint avant la fin naturelle
        }
        double now = now_seconds();
        int    a = 0, b = 0;
        double seconds = 0;
        long long lm = 0, sd = 0;
        char   stage[96];
        if (sscanf(line.c_str(), "[AR] Score %d/%d", &a, &b) == 2) {
            if (a == 0) run.phase_t0 = now;
            double rate = a > 50 && now > run.phase_t0 ? a / (now - run.phase_t0) : run.model.score_rate;
            double eta  = std::max(100, 1100 - a) / rate + run.expected_frames / run.model.sem_rate +
                         remaining_after_semantic(run, run.expected_frames);
            update(id, [&](Song & s) {
                s.stage    = tr("Écriture de la partition");
                s.progress = 0.02 + 0.10 * std::min(a / 1200.0, 1.0);
                s.eta      = eta;
                s.eta_at   = now;
            });
        } else if (sscanf(line.c_str(), "[AR] Semantic %d/%d", &a, &b) == 2) {
            if (a == 0) run.phase_t0 = now;
            double rate     = a > 100 && now > run.phase_t0 ? a / (now - run.phase_t0) : run.model.sem_rate;
            double expected = std::max(run.expected_frames, (double) a + 50);
            double eta      = (expected - a) / rate + remaining_after_semantic(run, expected);
            snprintf(stage, sizeof(stage), tr("Composition du morceau (%d s)"), a / 25);
            update(id, [&](Song & s) {
                s.stage    = stage;
                s.progress = (run.has_score ? 0.12 : 0.02) + 0.18 * std::min(a / std::max(expected, 1.0), 1.0);
                s.eta      = eta;
                s.eta_at   = now;
            });
        } else if (sscanf(line.c_str(), "[NAR] Step %d/%d", &a, &b) == 2 && b > 0) {
            if (run.last_step_t > 0) {
                run.step_sum += now - run.last_step_t;
                run.steps_done++;
            }
            run.last_step_t = now;
            double step = run.steps_done > 0 ? run.step_sum / run.steps_done : run.model.nar_step(run.expected_frames) * 32 * run.solver / run.nar_steps;
            snprintf(stage, sizeof(stage), tr("Synthèse audio (%d/%d)"), a, b);
            update(id, [&](Song & s) {
                s.stage    = stage;
                s.progress = 0.30 + 0.65 * a / b;
                s.eta      = (b - a) * step + run.tail;
                s.eta_at   = now;
            });
        } else if (sscanf(line.c_str(), "[NAR] Song 0: %d frames", &a) == 1) {
            run.expected_frames = a;  // durée réelle connue : on recale l'estimation
            run.last_step_t     = now;
            update(id, [&](Song & s) {
                s.eta    = remaining_after_semantic(run, a);
                s.eta_at = now;
            });
        } else if (line.rfind("[VAE] Tiled decode:", 0) == 0) {
            update(id, [&](Song & s) {
                s.stage    = tr("Décodage du son");
                s.progress = 0.96;
                s.eta      = 8;
                s.eta_at   = now;
            });
        } else if (sscanf(line.c_str(), "[Synth] Done: %lf s of audio, seeds %lld and %lld", &seconds, &lm, &sd) == 3) {
            update(id, [&](Song & s) {
                s.audio_seconds  = seconds;
                s.has_used_seeds = true;
                s.used_lm_seed   = lm;
                s.used_seed      = sd;
            });
        } else if (line.find("FATAL") != std::string::npos) {
            update(id, [&](Song & s) { s.message = line; });
        }
    };
    int code = run_process(cmd, on_line, track);
    write_file_atomic(folder / "generation.log", log.data(), log.size());

    if (check_cancel()) {
        finish_stopped(id, wav);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_id_.clear();
    }
    if (code != 0 || !fs::exists(wav)) {
        throw std::runtime_error(!fatal.empty() ? fatal : trf("yue-synth a échoué (code %d)", code));
    }

    if (measured.truncated) {
        // Plafond de durée atteint : fondu de sortie plutôt qu'une coupure nette. Durée du réglage général (courbe
        // linéaire) ou, pour un morceau « fin sur la structure », celle de sa dernière section (courbe en demi-sinus,
        // douce au début comme à la fin).
        double fade;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fade = song.fade_seconds > 0 ? song.fade_seconds : fade_default_;
        }
        update(id, [](Song & s) { s.stage = tr("Fondu de fin"); });
        double   seconds = media_duration(wav);
        fade             = std::min(fade, std::max(1.0, seconds / 2));
        fs::path faded   = folder / "audio_fondu.wav";
        char     filter[128];
        snprintf(filter, sizeof(filter), "afade=t=out:st=%.2f:d=%.2f%s", std::max(0.0, seconds - fade), fade,
                 song.fade_seconds > 0 ? ":curve=hsin" : "");
        std::wstring ff = ffmpeg_exe() + L" -v error -y -i " + quote(wav.wstring()) + L" -af " + widen(filter) +
                          L" -c:a pcm_s24le " + quote(faded.wstring());
        if (run_process(ff, nullptr, nullptr) == 0 && fs::exists(faded)) {
            std::error_code ec;
            fs::rename(faded, wav, ec);
        }
    }
    // 1.0.48 : partie 2 d'un morceau long. Elle commence par les long_join dernières secondes de la partie 1 : la partie 1
    // entière, puis la partie 2 à partir de (long_join − 3 s), avec un fondu enchaîné de 3 s au milieu de la reprise.
    // La partie 2 seule est gardée sous partie2_seule.wav.
    if (song.long_join > 0 && has_parent) {
        // 1.0.70 : le modèle conclut parfois juste après la reprise (34 s au lieu de 4 min) : la partie est refaite, 2 fois au plus
        const double added = media_duration(wav) - song.long_join;
        if (!song.instrumental && song.long_retry < 2 && added < 0.4 * (song.expected_seconds - song.long_join)) {
            update(id, [](Song & s) {
                s.long_retry++;
                s.lm_seed = s.seed = -1;
            });
            generate(id);
            return;
        }
        update(id, [](Song & s) { s.stage = tr("Assemblage des deux parties"); });
        fs::path first = song_folder(song.parent) / "audio.wav";
        if (!fs::exists(first)) first = song_folder(song.parent) / "audio.flac";
        fs::path alone = folder / "partie2_seule.wav", joined = folder / "audio_assemble.wav";
        std::error_code ec;
        fs::copy_file(wav, alone, fs::copy_options::overwrite_existing, ec);
        char filter[160];
        snprintf(filter, sizeof(filter), "[1:a]atrim=start=%.2f,asetpts=PTS-STARTPTS[b];[0:a][b]acrossfade=d=3:c1=tri:c2=tri",
                 std::max(0.0, song.long_join - 3.0));
        std::wstring ff = ffmpeg_exe() + L" -v error -y -i " + quote(first.wstring()) + L" -i " + quote(wav.wstring()) +
                          L" -filter_complex " + quote(widen(filter)) + L" -c:a pcm_s24le " + quote(joined.wstring());
        if (fs::exists(first) && run_process(ff, nullptr, nullptr) == 0 && fs::exists(joined)) {
            fs::rename(joined, wav, ec);
            double total = media_duration(wav);
            update(id, [&](Song & s) { s.audio_seconds = total; });
        } else {
            throw std::runtime_error(tr("Assemblage des deux parties impossible (partie 1 introuvable ou ffmpeg en erreur)."));
        }
    }
    update(id, [](Song & s) {
        s.stage    = tr("Conversion FLAC et MP3");
        s.progress = 0.99;
        s.eta      = 3;
        s.eta_at   = now_seconds();
    });
    std::wstring tags = L" -metadata " + quote(L"title=" + widen(song.title)) + L" -metadata " +
                        quote(L"artist=" + widen(song.artist.empty() ? "YuE2" : song.artist)) + L" -metadata " +
                        quote(L"genre=" + widen(song.style.substr(0, 200)));
    if (!song.album.empty()) tags += L" -metadata " + quote(L"album=" + widen(song.album));
    if (!song.track.empty()) tags += L" -metadata " + quote(L"track=" + widen(song.track));
    // Qualité maximale : FLAC 24 bits sans perte, MP3 320 kbit/s. Le WAV (archive, sortie brute du
    // moteur) garde son vrai pic à 0 dBFS ; FLAC et MP3 reçoivent 1 dB de marge ("true peak"
    // headroom) pour éviter qu'un ré-encodage ou qu'un convertisseur N/A en aval ne dépasse 0 dBFS
    // entre deux échantillons déjà au plafond et ne grésille.
    const wchar_t * targets[][3] = { { L"audio.flac", L"-c:a flac", L"-af volume=-1dB" },
                                     { L"audio.mp3", L"-c:a libmp3lame -b:a 320k", L"-af volume=-1dB" } };
    for (auto & t : targets) {
        std::wstring ff = ffmpeg_exe() + L" -v error -y -i " + quote(wav.wstring()) + L" " + t[2] + L" " + t[1] + tags +
                          L" " + quote((folder / t[0]).wstring());
        std::string err;
        if (run_process(ff, [&](const std::string & l) { err += l + " "; }, nullptr) != 0) {
            throw std::runtime_error(trf("Conversion ffmpeg impossible : %s", err.c_str()));
        }
    }
    update(id, [&](Song & s) {
        s.status       = "done";
        s.heard        = false;  // neuf : en attente d'écoute et de vérification
        s.stage        = tr("Terminée");
        s.progress     = 1.0;
        s.finished     = now_seconds();
        s.local        = false;
        s.eta          = 0;
        s.score_tokens = measured.score_tokens;
        s.score_s      = measured.score_s;
        s.sem_tokens   = measured.sem_tokens;
        s.sem_s        = measured.sem_s;
        s.frames       = measured.frames;
        s.nar_step_s   = measured.nar_step_s;
        s.truncated    = measured.truncated;
    });
    {  // 1.0.52 : fiches texte (paroles.txt, fiche.txt, fiche_album.txt, fiche_groupe.txt)
        Song              done;
        std::vector<Song> all;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            auto                        it = songs_.find(id);
            if (it != songs_.end()) done = it->second;
            for (const auto & kv : songs_) all.push_back(kv.second);
        }
        // morceau long assemblé : les fiches portent les paroles des deux parties
        if (!done.id.empty()) write_fiches(with_full_lyrics(done, all), song_folder(id));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    refit_model();
}

// ---------------------------------------------------------------------------- Porte de la file et test de santé

void Studio::set_gate(bool closed) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gate_ == closed) return;
        gate_ = closed;
        if (closed && !running_id_.empty() && !cancel_running_) {
            cancel_running_ = true;
            stop_kind_      = "gate";
            if (process_) TerminateProcess((HANDLE) process_, 1);
        }
        for (auto & [id, s] : songs_) {  // libellé des morceaux déjà en attente
            if (s.local && s.status == "queued" && !s.held) {
                s.stage = closed ? "En attente (jeu en cours)" : "En attente";
                save(s);
            }
        }
    }
    cv_.notify_all();
    notify_();
}

bool Studio::gate_closed() {
    std::lock_guard<std::mutex> lock(mutex_);
    return gate_;
}

HealthResult Studio::health() {
    std::lock_guard<std::mutex> lock(mutex_);
    return health_;
}

void Studio::start_health_check() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (maintenance_) return;
        if (!running_id_.empty() || other_synth_running()) {
            health_ = { 4, tr("Une génération est en cours : lancez le test quand la carte graphique est libre."), 0, now_seconds() };
            return;
        }
        maintenance_ = true;
        health_      = { 1, tr("Préparation du test…"), 0, now_seconds() };
    }
    if (health_thread_.joinable()) health_thread_.join();  // l'exécution précédente est terminée
    health_thread_ = std::thread([this] { run_health_check(); });
    notify_();
}

void Studio::run_health_check() {
    auto finish = [&](int state, const std::string & message, double error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            health_      = { state, message, error, now_seconds() };
            maintenance_ = false;
        }
        cv_.notify_all();
        notify_();
    };
    // Un morceau terminé fournit les codes de composition d'un extrait réel (la requête de rejeu).
    std::vector<Song> candidates;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto & [id, s] : songs_) {
            if (s.status == "done" && fs::exists(song_folder(id) / "audio.json")) candidates.push_back(s);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Song & a, const Song & b) { return a.created > b.created; });
    yyjson_mut_doc * doc     = nullptr;
    bool             adapter = false;
    for (const Song & s : candidates) {
        doc = read_replay(song_folder(s.id) / "audio.json");
        if (!doc) continue;
        yyjson_mut_val * o      = yyjson_mut_doc_get_root(doc);
        const char *     tokens = yyjson_mut_get_str(yyjson_mut_obj_get(o, "semantic_tokens"));
        if (tokens && std::count(tokens, tokens + strlen(tokens), ',') > 600) {
            std::string all(tokens), cut = all;
            size_t      pos = 0;
            for (int n = 0; n < 1200 && pos != std::string::npos; n++) pos = all.find(',', pos + 1);
            if (pos != std::string::npos) cut = all.substr(0, pos);  // les 1200 premières trames (48 s)
            put_str(doc, o, "semantic_tokens", cut);
            put_int(doc, o, "steps", 8);
            put_int(doc, o, "peak_clip", 0);
            put_str(doc, o, "output_format", "wav24");
            adapter = yyjson_mut_obj_get(o, "adapters") != nullptr;
            break;
        }
        yyjson_mut_doc_free(doc);
        doc = nullptr;
    }
    if (!doc) {
        finish(4, tr("Aucun morceau terminé exploitable : générez d'abord un morceau, puis relancez le test."), 0);
        return;
    }
    fs::path dir = paths_.cache / "sante";  // 1.0.41 : rangé dans cache
    fs::create_directories(dir);
    std::string request = write_doc(doc);
    yyjson_mut_doc_free(doc);
    write_file_atomic(dir / "request.json", request.data(), request.size());

    std::vector<std::vector<float>> latents;
    for (int run = 0; run < 2; run++) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            health_.message = trf("Rendu d'essai %d sur 2…", run + 1);
        }
        notify_();
        fs::path latent = dir / ("latent" + std::to_string(run) + ".vae");
        fs::remove(latent);
        std::wstring cmd = quote(paths_.synth.wstring()) + L" " + engine_flags() + L" --model " + quote(paths_.model.wstring()) +
                           L" --vae " + quote(paths_.vae.wstring()) + L" --request " + quote((dir / "request.json").wstring()) +
                           L" --latent " + quote(latent.wstring()) + L" --out " + quote((dir / "essai.wav").wstring());
        if (adapter) cmd += L" --adapters " + quote(paths_.adapters.wstring());
        std::string fatal;
        int code = run_process(cmd, [&](const std::string & l) {
            if (l.find("FATAL") != std::string::npos) fatal = l;
        }, [&](HANDLE h) {
            std::lock_guard<std::mutex> lock(mutex_);
            process_ = h;
            if (h && stop_) TerminateProcess(h, 1);
        });
        if (stop_) {
            finish(4, tr("Test interrompu par la fermeture de l'application."), 0);
            return;
        }
        std::string raw = read_file(latent);
        if (code != 0 || raw.size() < 4096) {
            finish(4, fatal.empty() ? std::string(tr("Le rendu d'essai a échoué.")) : trf("Le rendu d'essai a échoué : %s", fatal.c_str()), 0);
            return;
        }
        std::vector<float> v(raw.size() / sizeof(float));
        memcpy(v.data(), raw.data(), v.size() * sizeof(float));
        latents.push_back(std::move(v));
    }
    size_t n = std::min(latents[0].size(), latents[1].size());
    double diff = 0, ref = 0;
    size_t bad  = 0;
    for (size_t i = 0; i < n; i++) {
        double a = latents[0][i], b = latents[1][i];
        if (!std::isfinite(a) || !std::isfinite(b)) {
            bad++;
            continue;
        }
        diff += (a - b) * (a - b);
        ref += a * a;
    }
    double rel = ref > 0 ? std::sqrt(diff / ref) : 1.0;
    char   buf[256];
    if (bad > 0) {
        finish(3, trf("INSTABLE : le rendu contient des valeurs invalides (%d).", (int) bad), 1.0);
    } else if (rel < 0.15) {
        snprintf(buf, sizeof(buf), tr("Stable : deux rendus identiques concordent à %.1f %% près (seuil d'alerte : 15 %%)."), rel * 100);
        finish(2, buf, rel);
    } else {
        snprintf(buf, sizeof(buf),
                 tr("INSTABLE : deux rendus identiques diffèrent de %.0f %% (stable : moins de 5 %%). Le pilote graphique a peut-être changé : les générations risquent de grésiller."),
                 rel * 100);
        finish(3, buf, rel);
    }
}
