// 1.0.91 : voir atelier.hpp.
#include "atelier.hpp"

#include <wrl.h>
#include <wrl/client.h>

#include <vector>

#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
const wchar_t * kClass    = L"StudioAudioAIAtelierPartition";
const wchar_t * kHost     = L"atelier.studio";
std::wstring    g_audio_dir;  // 1.0.97 : dossier des sons rendus
HWND            g_wnd     = nullptr;
bool            g_ready   = false;  // page chargée : les messages peuvent partir
std::vector<std::string> g_pending;  // messages envoyés avant que la page soit prête
AtelierCallbacks g_cb;
ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller>  g_controller;
ComPtr<ICoreWebView2>            g_view;

std::wstring widen(const std::string & s) {
    if (s.empty()) return std::wstring();
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w((size_t) n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}
std::string narrow(const wchar_t * w) {
    if (!w || !*w) return std::string();
    int         n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t) (n > 0 ? n - 1 : 0), 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void fit() {
    if (!g_wnd || !g_controller) return;
    RECT r;
    GetClientRect(g_wnd, &r);
    g_controller->put_Bounds(r);
}

void send_now(const std::string & json) {
    if (g_view) g_view->PostWebMessageAsJson(widen(json).c_str());
}

void release_all() {
    if (g_controller) g_controller->Close();
    g_view.Reset();
    g_controller.Reset();
    g_env.Reset();
    g_ready = false;
    g_pending.clear();
}

LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: fit(); return 0;
        case WM_GETMINMAXINFO: {
            auto * mm = (MINMAXINFO *) lp;
            mm->ptMinTrackSize = { 640, 420 };
            return 0;
        }
        case WM_DESTROY:
            release_all();
            g_wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void create_webview(const std::wstring & pages_dir, const std::wstring & user_data_dir) {
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([pages_dir](HRESULT hr, ICoreWebView2Environment * env) -> HRESULT {
            if (FAILED(hr) || !env || !g_wnd) return S_OK;
            g_env = env;
            env->CreateCoreWebView2Controller(
                g_wnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([pages_dir](HRESULT hr2, ICoreWebView2Controller * c) -> HRESULT {
                           if (FAILED(hr2) || !c || !g_wnd) return S_OK;
                           g_controller = c;
                           c->get_CoreWebView2(&g_view);
                           fit();
                           ComPtr<ICoreWebView2Settings> st;
                           if (SUCCEEDED(g_view->get_Settings(&st))) {
                               st->put_AreDevToolsEnabled(FALSE);
                               st->put_IsStatusBarEnabled(FALSE);
                               st->put_IsZoomControlEnabled(TRUE);
                           }
                           // pages et sons servis depuis ressources\partition, sous une adresse locale
                           ComPtr<ICoreWebView2_3> v3;
                           if (SUCCEEDED(g_view.As(&v3)))
                           {
                               v3->SetVirtualHostNameToFolderMapping(kHost, pages_dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                               if (!g_audio_dir.empty()) v3->SetVirtualHostNameToFolderMapping(L"rendu.atelier.studio", g_audio_dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                           }
                           g_view->add_WebMessageReceived(
                               Callback<ICoreWebView2WebMessageReceivedEventHandler>([](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs * a) -> HRESULT {
                                   LPWSTR json = nullptr;
                                   if (SUCCEEDED(a->get_WebMessageAsJson(&json)) && json) {
                                       std::string s = narrow(json);
                                       CoTaskMemFree(json);
                                       if (s.find("\"type\":\"ready\"") != std::string::npos) {  // page prête : messages en attente
                                           g_ready = true;
                                           for (const std::string & m : g_pending) send_now(m);
                                           g_pending.clear();
                                           if (g_cb.on_ready) g_cb.on_ready(s);
                                       } else if (g_cb.on_message) {
                                           g_cb.on_message(s);
                                       }
                                   }
                                   return S_OK;
                               }).Get(),
                               nullptr);
                           g_view->Navigate((std::wstring(L"https://") + kHost + L"/atelier.html").c_str());
                           return S_OK;
                       }).Get());
            return S_OK;
        }).Get());
}
}  // namespace

std::wstring atelier_runtime_version() {
    LPWSTR v = nullptr;
    if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &v)) || !v) return std::wstring();
    std::wstring out = v;
    CoTaskMemFree(v);
    return out;
}

void atelier_set_audio_dir(const std::wstring & dir) { g_audio_dir = dir; }

bool atelier_is_open() { return g_wnd != nullptr; }

void atelier_post(const std::string & json) {
    if (g_ready) send_now(json);
    else g_pending.push_back(json);
}

bool atelier_open(HWND owner, const std::wstring & title, const std::wstring & pages_dir, const std::wstring & user_data_dir,
                  const std::string & first_message, AtelierCallbacks callbacks) {
    if (atelier_runtime_version().empty()) return false;
    g_cb = std::move(callbacks);
    if (g_wnd) {  // déjà ouverte : nouveau contenu, fenêtre devant
        atelier_post(first_message);
        SetWindowTextW(g_wnd, title.c_str());
        if (IsIconic(g_wnd)) ShowWindow(g_wnd, SW_RESTORE);
        SetForegroundWindow(g_wnd);
        return true;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
        wc.lpszClassName = kClass;
        wc.hIcon         = (HICON) SendMessageW(owner, WM_GETICON, ICON_BIG, 0);
        wc.hIconSm       = (HICON) SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        registered = true;
    }
    // 85 % de l'écran de la fenêtre principale, centrée
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT & wa = mi.rcWork;
    const int    w = (wa.right - wa.left) * 85 / 100, h = (wa.bottom - wa.top) * 85 / 100;
    g_wnd = CreateWindowExW(0, kClass, title.c_str(), WS_OVERLAPPEDWINDOW, wa.left + ((wa.right - wa.left) - w) / 2,
                            wa.top + ((wa.bottom - wa.top) - h) / 2, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_wnd) return false;
    ShowWindow(g_wnd, SW_SHOW);
    g_ready = false;
    g_pending.clear();
    g_pending.push_back(first_message);
    create_webview(pages_dir, user_data_dir);
    return true;
}

bool atelier_print_pdf(const std::wstring & path) {
    ComPtr<ICoreWebView2_7> v7;
    if (!g_view || FAILED(g_view.As(&v7))) return false;
    ComPtr<ICoreWebView2PrintSettings> settings;
    ComPtr<ICoreWebView2Environment6>  env6;
    if (g_env && SUCCEEDED(g_env.As(&env6)) && SUCCEEDED(env6->CreatePrintSettings(&settings))) {
        settings->put_PageWidth(8.27);   // A4 (pouces)
        settings->put_PageHeight(11.69);
        settings->put_ShouldPrintBackgrounds(FALSE);
        settings->put_ShouldPrintHeaderAndFooter(FALSE);
        settings->put_MarginTop(0.4);
        settings->put_MarginBottom(0.4);
        settings->put_MarginLeft(0.4);
        settings->put_MarginRight(0.4);
    }
    const std::wstring p = path;
    return SUCCEEDED(v7->PrintToPdf(p.c_str(), settings.Get(),
                                    Callback<ICoreWebView2PrintToPdfCompletedHandler>([p](HRESULT hr, BOOL ok) -> HRESULT {
                                        if (g_cb.on_pdf) g_cb.on_pdf(SUCCEEDED(hr) && ok, p);
                                        return S_OK;
                                    }).Get()));
}

void atelier_close() {
    if (g_wnd) DestroyWindow(g_wnd);
}
