// Journal de débogage : voir journal.hpp.
#include "journal.hpp"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>

#include "imgui.h"
#include "imgui_internal.h"

namespace journal {
namespace fs = std::filesystem;

namespace {
std::mutex               g_mutex;
FILE *                   g_file = nullptr;
fs::path                 g_path;
std::deque<std::string>  g_recent;  // dernières actions (avec l'heure)

std::string now_text() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[40];
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d.%03d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
             t.wMilliseconds);
    return b;
}

// Écrit une ligne (le verrou est déjà pris).
void put(const std::string & line) {
    if (!g_file) return;
    fputs(line.c_str(), g_file);
    fputc('\n', g_file);
    fflush(g_file);
}

void dump_recent_locked(const char * title) {
    put(std::string("    ") + title);
    for (auto & a : g_recent) put("      " + a);
}

LONG WINAPI crash_filter(EXCEPTION_POINTERS * ep) {
    char b[160];
    snprintf(b, sizeof(b), "PLANTAGE code 0x%08lX à l'adresse %p", ep ? ep->ExceptionRecord->ExceptionCode : 0,
             ep ? ep->ExceptionRecord->ExceptionAddress : nullptr);
    if (g_file) {  // pas de verrou : le plantage peut venir d'un fil qui le tient
        put(now_text() + " [ERREUR] [appli] " + b);
        dump_recent_locked("Dernières actions avant le plantage :");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void on_terminate() {
    error("appli", "std::terminate (exception C++ non rattrapée)");
    std::abort();
}

// Dernier élément survolé ou actif, repéré par le crochet d'ImGui (voir imgui_install).
ImGuiID     g_hot_id = 0;
int         g_hot_frame = -1;
std::string g_hot_label, g_hot_window;
}  // namespace

fs::path path() { return g_path; }

void init(const fs::path & dir, const std::string & version) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    g_path = dir / "journal.log";
    if (fs::exists(g_path, ec) && fs::file_size(g_path, ec) > 2u * 1024 * 1024) {
        fs::path old = dir / "journal.ancien.log";
        fs::remove(old, ec);
        fs::rename(g_path, old, ec);
    }
    g_file = _wfopen(g_path.c_str(), L"ab");
    if (!g_file) return;
    put("");
    put(now_text() + " [INFO] [appli] ===== Démarrage du Studio Audio AI " + version + " =====");
    SetUnhandledExceptionFilter(crash_filter);
    std::set_terminate(on_terminate);
}

void event(const std::string & category, const std::string & message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    put(now_text() + " [INFO] [" + category + "] " + message);
}

void error(const std::string & category, const std::string & message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    put(now_text() + " [ERREUR] [" + category + "] " + message);
    dump_recent_locked("Dernières actions avant cette erreur :");
}

void action(const std::string & message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string line = now_text() + " " + message;
    g_recent.push_back(line);
    if (g_recent.size() > 20) g_recent.pop_front();
    put(now_text() + " [ACTION] " + message);
}

}  // namespace journal

// ---------------------------------------------------------------------------------------------------------------------
// Crochets d'ImGui (IMGUI_ENABLE_TEST_ENGINE) : ImGui nous donne le texte de chaque bouton, case ou onglet ; on garde
// celui qui est sous la souris pour écrire « Clic : ... » dans le journal.
#ifdef IMGUI_ENABLE_TEST_ENGINE
void ImGuiTestEngineHook_ItemAdd(ImGuiContext *, ImGuiID, const ImRect &, const ImGuiLastItemData *) {}
void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char * ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID) { return nullptr; }
void ImGuiTestEngineHook_ItemInfo(ImGuiContext * ctx, ImGuiID id, const char * label, ImGuiItemStatusFlags) {
    if (!label || !*label) return;
    if (id != ctx->HoveredId && id != ctx->ActiveId) return;
    using namespace journal;
    g_hot_id    = id;
    g_hot_frame = ctx->FrameCount;
    g_hot_label = label;
    ImGuiWindow * w = ctx->HoveredWindow ? ctx->HoveredWindow->RootWindow : nullptr;
    g_hot_window    = w ? w->Name : "";
}
#endif

namespace journal {
void imgui_install() {
#ifdef IMGUI_ENABLE_TEST_ENGINE
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
#endif
}

void imgui_frame_end() {
#ifdef IMGUI_ENABLE_TEST_ENGINE
    ImGuiContext & g = *ImGui::GetCurrentContext();
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || g_hot_frame != g.FrameCount) return;
    std::string label = g_hot_label;
    size_t      p     = label.find("##");
    if (p != std::string::npos) label.erase(p);
    if (label.empty()) return;
    std::string win = g_hot_window;
    p               = win.find("##");
    if (p != std::string::npos) win.erase(p);
    action("Clic : « " + label + " »" + (win.empty() ? "" : " (fenêtre « " + win + " »)"));
    g_hot_frame = -1;
#endif
}
}  // namespace journal
