#include "setup.hpp"
#include "version.hpp"  // APP_NAME

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>
#include <fstream>
#include <thread>

#include "download.hpp"
#include "i18n.hpp"
#include "media.hpp"

namespace {

std::wstring wtr(const char * key) { return widen(tr(key)); }

struct ModelFile {
    const wchar_t *      sub;
    const wchar_t *      name;
    const wchar_t *      repo;
    unsigned long long   size;
};

// Moteur : modèle principal, VAE, transcription (reprises/covers) et LoRA instrumentale.
const ModelFile MODEL_FILES[] = {
    { L"musique", L"YuE2-3B-Q6_K.gguf", L"Serveurperso/YuE2-GGUF", 2943331072ull },  // 1.0.42 : modeles\musique, modeles\lora
    { L"musique", L"YuE2-Vae-F32.gguf", L"Serveurperso/YuE2-GGUF", 530497344ull },
    { L"musique", L"SheetSage2-Q8_0.gguf", L"Serveurperso/YuE2-GGUF", 105629856ull },
    { L"musique", L"MERT-v2-FullSong-Q8_0.gguf", L"Serveurperso/YuE2-GGUF", 902283712ull },
    { L"lora", L"ar_lora_inst_v3abc.bf16.safetensors", L"Mothersuperior/YuE2-instrumental-cot-full-loras", 139502088ull },
};

// 1.0.32 (règle de Herve) : chaque copie du Studio n'utilise que SON dossier models (<programme>\models, LoRA dans
// models\adapters), pour que l'utilisateur sache toujours où sont ses modèles. L'ancien fichier
// donnees\studio_modeles.txt (dossier choisi ailleurs, versions 1.0.30-1.0.31) n'est plus lu : il est renommé
// _ancien_studio_modeles.txt (jamais effacé) et le fait est noté dans donnees\temp\studio_yue2_demarrage.log.
void retire_old_setting(const fs::path & exe_dir) {
    std::error_code ec;
    for (const fs::path & dir : { app_data_dir(), exe_dir }) {
        if (dir.empty()) continue;
        fs::path old = dir / "studio_modeles.txt";
        if (!fs::exists(old, ec)) continue;
        std::string line;
        {
            std::ifstream in(old, std::ios::binary);
            std::getline(in, line);
        }
        fs::path renamed = dir / "_ancien_studio_modeles.txt";
        for (int n = 2; fs::exists(renamed, ec); n++) renamed = dir / ("_ancien_studio_modeles_" + std::to_string(n) + ".txt");
        fs::rename(old, renamed, ec);
        std::ofstream log(app_temp_dir() / "studio_yue2_demarrage.log", std::ios::binary | std::ios::app);
        log << "studio_modeles.txt ignore (dossier des modeles = " << narrow((exe_dir / "modeles").wstring())
            << ") ; ancien contenu : " << line << " ; renomme en _ancien_studio_modeles.txt\n";
    }
}

// ---- téléchargement (WinHTTP, reprise des fichiers .part)

struct Download {
    fs::path                 dir;
    std::atomic<unsigned long long> done{ 0 }, total{ 0 };
    std::atomic<int>         index{ 0 };
    std::atomic<bool>        cancel{ false }, finished{ false }, ok{ false };
    std::mutex               mutex;
    std::wstring             error;
};

bool fetch(Download & d, const ModelFile & f, const fs::path & dst) {
    std::wstring url = L"https://huggingface.co/" + std::wstring(f.repo) + L"/resolve/main/" + f.name;
    return http_fetch(url, dst, f.size, d.done, d.cancel, d.error);
}

void download_all(Download * d) {
    std::error_code ec;
    for (int i = 0; i < (int) std::size(MODEL_FILES) && !d->cancel; i++) {
        const ModelFile & f = MODEL_FILES[i];
        d->index = i;
        fs::path folder = d->dir / f.sub;
        fs::create_directories(folder, ec);
        fs::path dst = folder / f.name;
        if (fs::exists(dst) && fs::file_size(dst) == f.size) continue;
        bool ok = false;
        for (int attempt = 0; attempt < 4 && !ok && !d->cancel; attempt++) {  // coupures réseau : on reprend
            ok = fetch(*d, f, dst);
            if (!ok && !d->cancel) Sleep(1500);
        }
        if (!ok) {
            if (!d->cancel && d->error.empty()) d->error = widen(trf("Téléchargement impossible : %s", narrow(f.name).c_str()));
            d->finished = true;
            return;
        }
    }
    d->ok       = !d->cancel;
    d->finished = true;
}

Download * g_dl = nullptr;
HWND       g_label, g_bar, g_cancel;

std::wstring size_text(unsigned long long n) {
    wchar_t buf[64];
    (void) buf;
    return widen(trf("%.2f Go", (double) n / 1e9));
}

LRESULT CALLBACK dl_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER: {
            unsigned long long done = g_dl->done, total = g_dl->total;
            SendMessageW(g_bar, PBM_SETPOS, total ? (WPARAM) (done * 1000 / total) : 0, 0);
            int          i    = g_dl->index;
            std::wstring text = widen(trf("Téléchargement de %s  (%d/%d)\n%s sur %s",
                                          narrow(MODEL_FILES[std::min<int>(i, (int) std::size(MODEL_FILES) - 1)].name).c_str(), i + 1,
                                          (int) std::size(MODEL_FILES), narrow(size_text(done)).c_str(), narrow(size_text(total)).c_str()));
            SetWindowTextW(g_label, text.c_str());
            if (g_dl->finished) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        case WM_COMMAND:
            if ((HWND) lp == g_cancel) {
                g_dl->cancel = true;
                EnableWindow(g_cancel, FALSE);
            }
            return 0;
        case WM_CLOSE:
            if (!g_dl->finished) {
                g_dl->cancel = true;
                return 0;  // on attend la fin du fil
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Fenêtre de progression ; renvoie true si tout est téléchargé.
bool run_download(HINSTANCE inst, const fs::path & dir) {
    unsigned long long need = 0;
    for (const ModelFile & f : MODEL_FILES) {
        fs::path dst = dir / f.sub / f.name;
        if (!(fs::exists(dst) && fs::file_size(dst) == f.size)) need += f.size;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    ULARGE_INTEGER free_bytes{};
    if (GetDiskFreeSpaceExW(dir.c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < need + (200ull << 20)) {
        MessageBoxW(nullptr, widen(trf("Espace disque insuffisant dans %s : il faut environ %s.", narrow(dir.wstring()).c_str(),
                                       narrow(size_text(need)).c_str())).c_str(),
                    APP_NAME_W, MB_ICONERROR);
        return false;
    }
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    Download d;
    d.dir   = dir;
    d.total = 0;
    for (const ModelFile & f : MODEL_FILES) d.total += f.size;
    // Fichiers déjà présents comptés comme faits
    for (const ModelFile & f : MODEL_FILES) {
        fs::path dst = dir / f.sub / f.name;
        if (fs::exists(dst) && fs::file_size(dst) == f.size) d.done += f.size;
        else {
            fs::path part = dst;
            part += L".part";
            if (fs::exists(part)) d.done += fs::file_size(part);
        }
    }
    g_dl = &d;

    WNDCLASSEXW wc{ sizeof(wc), 0, dl_proc, 0, 0, inst, LoadIconW(inst, MAKEINTRESOURCEW(1)), LoadCursorW(nullptr, IDC_ARROW),
                    (HBRUSH) (COLOR_WINDOW + 1), nullptr, L"StudioYuE2Download", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, wtr("Studio Audio AI — téléchargement des modèles").c_str(),
                                WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 560, 190, nullptr, nullptr,
                                inst, nullptr);
    HFONT font = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    g_label    = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 14, 520, 40, hwnd, nullptr, inst, nullptr);
    g_bar      = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 16, 62, 520, 22, hwnd, nullptr, inst, nullptr);
    g_cancel   = CreateWindowExW(0, L"BUTTON", wtr("Annuler").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 436, 100, 100, 28, hwnd, nullptr, inst, nullptr);
    for (HWND h : { g_label, g_bar, g_cancel }) SendMessageW(h, WM_SETFONT, (WPARAM) font, TRUE);
    SendMessageW(g_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
    SetTimer(hwnd, 1, 250, nullptr);
    std::thread worker(download_all, &d);
    MSG         msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    d.cancel = true;
    worker.join();
    UnregisterClassW(wc.lpszClassName, inst);
    g_dl = nullptr;
    if (!d.ok && !d.cancel) {}
    if (!d.ok && !d.error.empty()) MessageBoxW(nullptr, d.error.c_str(), APP_NAME_W, MB_ICONERROR);
    return d.ok;
}

}  // namespace

bool ensure_models(void * instance, const fs::path & exe_dir, Paths & paths) {
    // 1.0.32 : un seul dossier, celui du programme (aucun autre dossier n'est jamais utilisé ni mémorisé).
    retire_old_setting(exe_dir);
    fs::path        models = exe_dir / "modeles";  // 1.0.42 : modeles\musique (YuE2), modeles\lora…
    std::error_code ec;
    fs::create_directories(models / "musique", ec);
    fs::create_directories(models / "lora", ec);
    for (;;) {
        if (paths.use_models(models)) return true;
        std::wstring text = widen(trf("Le modèle de musique YuE2-3B (un fichier YuE2-3B-*.gguf, par ex. Q6_K ou Q8_0, et YuE2-Vae-F32.gguf) est introuvable dans le dossier des modèles du programme :\n%s\n\n"
                                      "Oui : le télécharger depuis Hugging Face (environ 4,6 Go avec les modèles annexes) dans ce dossier.\n\n"
                                      "Non : ouvrir ce dossier pour y copier vous-même vos fichiers (modèles dans « musique », LoRA dans « lora »), puis répondre de nouveau.\n\n"
                                      "Annuler : quitter.", narrow(models.wstring()).c_str()));
        int r = MessageBoxW(nullptr, text.c_str(), wtr("Studio Audio AI — premier lancement").c_str(), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return false;
        if (r == IDYES) {
            run_download((HINSTANCE) instance, models);
            continue;
        }
        ShellExecuteW(nullptr, L"open", (models / "musique").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
