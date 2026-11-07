// 1.1.005 : voir helpview.hpp.
#include "helpview.hpp"

#include <wrl.h>
#include <wrl/client.h>

#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
const wchar_t * kHost = L"aide.studio";
HWND            g_parent   = nullptr;
bool            g_creating = false;  // création asynchrone en cours
bool            g_visible  = true;
RECT            g_rect{};
std::wstring    g_url;               // adresse à charger dès que la vue est prête
ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller>  g_controller;
ComPtr<ICoreWebView2>            g_view;
int             g_generation = 0;    // une création abandonnée (page quittée entre-temps) ne doit rien garder
}  // namespace

bool helpview_show(HWND parent, const RECT & rect, const std::wstring & pages_dir, const std::wstring & user_data_dir, const std::wstring & url) {
    g_rect = rect;
    if (g_controller) {
        g_controller->put_Bounds(rect);
        g_controller->put_IsVisible(g_visible ? TRUE : FALSE);
        return true;
    }
    if (g_creating) return true;
    LPWSTR ver = nullptr;
    if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver)) || !ver) return false;
    CoTaskMemFree(ver);
    g_parent   = parent;
    g_creating = true;
    g_url      = url;
    const int gen = ++g_generation;
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([pages_dir, gen](HRESULT hr, ICoreWebView2Environment * env) -> HRESULT {
            if (gen != g_generation) return S_OK;
            if (FAILED(hr) || !env) { g_creating = false; return S_OK; }
            g_env = env;
            env->CreateCoreWebView2Controller(
                g_parent, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([pages_dir, gen](HRESULT hr2, ICoreWebView2Controller * c) -> HRESULT {
                              if (gen != g_generation) { if (c) c->Close(); return S_OK; }
                              g_creating = false;
                              if (FAILED(hr2) || !c) return S_OK;
                              g_controller = c;
                              c->get_CoreWebView2(&g_view);
                              c->put_Bounds(g_rect);
                              c->put_IsVisible(g_visible ? TRUE : FALSE);
                              ComPtr<ICoreWebView2Settings> st;
                              if (SUCCEEDED(g_view->get_Settings(&st))) {
                                  st->put_AreDevToolsEnabled(FALSE);
                                  st->put_IsStatusBarEnabled(FALSE);
                                  st->put_IsZoomControlEnabled(TRUE);
                              }
                              ComPtr<ICoreWebView2_3> v3;
                              if (SUCCEEDED(g_view.As(&v3))) v3->SetVirtualHostNameToFolderMapping(kHost, pages_dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                              if (!g_url.empty()) g_view->Navigate(g_url.c_str());
                              return S_OK;
                          }).Get());
            return S_OK;
        }).Get());
    return true;
}

void helpview_navigate(const std::wstring & url) {
    g_url = url;
    if (g_view) g_view->Navigate(url.c_str());
}

void helpview_set_visible(bool visible) {
    g_visible = visible;
    if (g_controller) g_controller->put_IsVisible(visible ? TRUE : FALSE);
}

void helpview_back() {
    if (g_view) g_view->GoBack();
}

void helpview_forward() {
    if (g_view) g_view->GoForward();
}

void helpview_release() {
    ++g_generation;  // une création en cours sera abandonnée à son arrivée
    g_creating = false;
    if (g_controller) g_controller->Close();
    g_view.Reset();
    g_controller.Reset();
    g_env.Reset();
}
