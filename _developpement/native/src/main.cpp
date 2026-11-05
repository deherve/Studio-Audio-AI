// Studio Audio AI (anciennement Studio YuE2), version native : interface Win32 + DirectX 11 + Dear ImGui, sans navigateur ni serveur web.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <dxgi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// GDI+ (dessin des pastilles) s'appuie sur min/max, masqués par NOMINMAX.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "imgui.h"
#include "imgui_internal.h"  // InputEventsQueue, état des champs de texte
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "misc/cpp/imgui_stdlib.h"
#include "player.hpp"
#include "studio.hpp"
#include "analysis.hpp"
#include "suno.hpp"
#include "fiches.hpp"
#include "media.hpp"
#include "setup.hpp"
#include "gpumem.hpp"
#include "gpuinfo.hpp"
#include "lyrics_web.hpp"
#include "update_check.hpp"
#include "sysinfo.hpp"
#include "translate.hpp"
#include "ai_writer.hpp"
#include "version.hpp"
#include "download.hpp"
#include "lora_catalog.hpp"
#include "i18n.hpp"
#include "yyjson.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const UINT WM_APP_NOTIFY = WM_APP + 1;
static const UINT WM_APP_TRAY   = WM_APP + 2;
static UINT       g_taskbar_created = 0;  // explorateur relancé : l'icône de notification doit être recréée

enum TrayCommand { TRAY_NONE, TRAY_OPEN, TRAY_RESUME_ALL, TRAY_HOLD_ALL, TRAY_QUIT, TRAY_READD };
static int g_tray_cmd = TRAY_NONE;
static std::vector<std::wstring> g_dropped;  // fichiers glissés sur la fenêtre (packs .yue2pack), traités à l'image suivante

// ---------------------------------------------------------------------------- Direct3D

static ID3D11Device *           g_device      = nullptr;
static ID3D11DeviceContext *    g_context     = nullptr;
static IDXGISwapChain *         g_swapchain   = nullptr;
static ID3D11RenderTargetView * g_rtv         = nullptr;
static bool                     g_occluded    = false;
static UINT                     g_resize_w    = 0, g_resize_h = 0;
static Studio *                 g_studio      = nullptr;
static bool                     g_want_close  = false;
static bool                     g_force_close = false;
static LUID                     g_adapter_luid{};
static unsigned long long       g_adapter_vram = 0;

static void create_render_target() {
    ID3D11Texture2D * back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

static void cleanup_render_target() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

static bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[]      = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL       level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swapchain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapchain, &g_device, &level, &g_context);
    }
    if (hr != S_OK) {
        return false;
    }
    create_render_target();
    // Carte utilisée par l'application : sert à retrouver ses compteurs de mémoire vidéo
    IDXGIDevice * dxgi = nullptr;
    if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
        IDXGIAdapter * adapter = nullptr;
        if (SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC desc{};
            adapter->GetDesc(&desc);
            g_adapter_luid = desc.AdapterLuid;
            g_adapter_vram = desc.DedicatedVideoMemory;
            adapter->Release();
        }
        dxgi->Release();
    }
    return true;
}

static void cleanup_device() {
    cleanup_render_target();
    if (g_swapchain) g_swapchain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

// Texte traduit en UTF-16 (menus et boîtes de dialogue Windows).
static std::wstring wtr(const char * key) { return widen(tr(key)); }
// Filtre de boîte de dialogue : « Description\0motifs\0Tous les fichiers\0*.*\0\0 » (c_str() ajoute le dernier zéro).
static std::wstring file_filter(const std::wstring & label, const std::wstring & patterns) {
    std::wstring f = label;
    f.push_back(L'\0');
    f += patterns;
    f.push_back(L'\0');
    f += wtr("Tous les fichiers");
    f.push_back(L'\0');
    f += L"*.*";
    f.push_back(L'\0');
    return f;
}

static void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, TRAY_OPEN, wtr("Ouvrir Studio Audio AI").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TRAY_RESUME_ALL, wtr("Tout lancer").c_str());
    AppendMenuW(menu, MF_STRING, TRAY_HOLD_ALL, wtr("Tout mettre en pause (libérer la carte graphique)").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TRAY_QUIT, wtr("Quitter").c_str());
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);  // sinon le menu ne se ferme pas en cliquant ailleurs
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (cmd) g_tray_cmd = cmd;
}

static float g_dpi = 1.f;  // échelle d'affichage (DPI du moniteur)

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }
    if (msg == g_taskbar_created && g_taskbar_created) {
        g_tray_cmd = TRAY_READD;
        return 0;
    }
    switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                g_resize_w = LOWORD(lparam);
                g_resize_h = HIWORD(lparam);
            }
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO *) lparam)->ptMinTrackSize = { (LONG) (1180 * g_dpi), (LONG) (620 * g_dpi) };  // en dessous, les langues à textes longs se chevauchent
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_CLOSE:
            if (!g_force_close && g_studio && g_studio->local_active()) {
                g_want_close = true;  // confirmation affichée par l'interface
                return 0;
            }
            break;
        case WM_DROPFILES: {
            HDROP drop = (HDROP) wparam;
            UINT  n    = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; i++) {
                wchar_t file[MAX_PATH];
                if (DragQueryFileW(drop, i, file, MAX_PATH)) g_dropped.push_back(file);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_APP_NOTIFY:
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lparam) == WM_LBUTTONDBLCLK || LOWORD(lparam) == WM_LBUTTONUP) {
                g_tray_cmd = TRAY_OPEN;
            } else if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
                show_tray_menu(hwnd);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------- Thème

struct Palette {
    ImVec4 bg, panel, card, text, muted, line, accent, accent_ink, accent_soft, danger, ok, chip, warn;
    // Couleurs des blocs du formulaire et des catégories de balises
    ImVec4 blue, violet, teal, amber, pink, orange, green;
};
static Palette P;

static ImVec4 hex(unsigned rgb, float a = 1.f) {
    return ImVec4(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}

static ImVec4 alpha(const ImVec4 & c, float a) {
    return ImVec4(c.x, c.y, c.z, a);
}

// Mélange d'une couleur dans le fond : teinte légère des blocs.
static ImVec4 tint(const ImVec4 & base, const ImVec4 & color, float amount) {
    return ImVec4(base.x + (color.x - base.x) * amount, base.y + (color.y - base.y) * amount,
                  base.z + (color.z - base.z) * amount, 1.f);
}

static bool windows_light_theme() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

// Thèmes graphiques : 0 = Minuit violet (par défaut ; suit le mode clair/sombre de Windows), puis des thèmes fixes.
static int  g_theme        = 0;
static bool g_system_light = false;
static const char * THEME_NAMES[] = { N_("Minuit violet (par défaut)"), N_("Studio gris"), N_("Papier encre"), N_("Menthe clair") };
static const char * THEME_HINTS[] = { N_("sombre violet (ou clair selon Windows)"), N_("gris neutre et dense, ambre"), N_("clair chaud, bleu encre, coins très arrondis"),
                                      N_("clair frais, turquoise") };
static const int    THEME_COUNT   = 4;

// Un thème est dark ou light ; vrai si la fenêtre doit prendre la barre de titre sombre.
static bool theme_is_dark(int theme, bool system_light) {
    return theme == 1 || (theme == 0 && !system_light);
}

static void apply_theme(bool light, float dpi) {
    int theme = g_theme;
    if (theme == 1) {  // Studio gris : sombre, neutre, ambre
        P = { hex(0x1a1b1e), hex(0x232529), hex(0x2b2d32), hex(0xe4e6ea), hex(0x8e939c), hex(0x383b42),
              hex(0xffb000), hex(0x241a00), hex(0x3a3320), hex(0xff6b5e), hex(0x7ddf64), hex(0x30333a), hex(0xffb000),
              hex(0x60a5fa), hex(0xa78bfa), hex(0x2dd4bf), hex(0xfbbf24), hex(0xf472b6), hex(0xfb923c), hex(0x34d399) };
    } else if (theme == 2) {  // Papier encre : clair chaud, bleu encre
        P = { hex(0xf3f1ee), hex(0xffffff), hex(0xfbfaf8), hex(0x1d1c1a), hex(0x6b6862), hex(0xe0dcd4),
              hex(0x1d4ed8), hex(0xffffff), hex(0xe3eafc), hex(0xc2372f), hex(0x15803d), hex(0xefece6), hex(0xb45309),
              hex(0x2563eb), hex(0x6d5ce7), hex(0x0d9488), hex(0xd97706), hex(0xdb2777), hex(0xea580c), hex(0x059669) };
    } else if (theme == 3) {  // Menthe clair : clair frais, turquoise
        P = { hex(0xeef6f3), hex(0xffffff), hex(0xf6fbf9), hex(0x14231e), hex(0x5a7068), hex(0xd5e6df),
              hex(0x0d9488), hex(0xffffff), hex(0xd6f1ec), hex(0xc2372f), hex(0x059669), hex(0xe5f1ed), hex(0xb45309),
              hex(0x2563eb), hex(0x6d5ce7), hex(0x0891b2), hex(0xd97706), hex(0xdb2777), hex(0xea580c), hex(0x15803d) };
    } else if (light) {
        P = { hex(0xf3f1ee), hex(0xffffff), hex(0xfbfaf8), hex(0x1d1c1a), hex(0x6b6862), hex(0xe3e0da),
              hex(0x5b4bdb), hex(0xffffff), hex(0xece9fd), hex(0xc2372f), hex(0x1f8a4c), hex(0xf0eee9), hex(0xb45309),
              hex(0x2563eb), hex(0x6d5ce7), hex(0x0d9488), hex(0xd97706), hex(0xdb2777), hex(0xea580c), hex(0x059669) };
    } else {
        P = { hex(0x121215), hex(0x1b1b20), hex(0x222229), hex(0xecebe8), hex(0x9b9891), hex(0x2e2e36),
              hex(0x8f83ff), hex(0x12111a), hex(0x2a2745), hex(0xff6b5e), hex(0x4cc983), hex(0x27272e), hex(0xfbbf24),
              hex(0x60a5fa), hex(0xa78bfa), hex(0x2dd4bf), hex(0xfbbf24), hex(0xf472b6), hex(0xfb923c), hex(0x34d399) };
    }
    // Forme : arrondis et densité selon le thème
    float child_r = 12, frame_r = 8, popup_r = 10, tab_r = 7, pad_x = 10, pad_y = 6, item_y = 7;
    if (theme == 1) { child_r = 4; frame_r = 3; popup_r = 4; tab_r = 3; pad_x = 8; pad_y = 4; item_y = 5; }
    if (theme == 2) { child_r = 14; frame_r = 9; popup_r = 12; tab_r = 8; }
    ImGuiStyle & s = ImGui::GetStyle();
    s                    = ImGuiStyle();
    s.WindowRounding     = 0;
    s.ChildRounding      = child_r;
    s.FrameRounding      = frame_r;
    s.PopupRounding      = popup_r;
    s.GrabRounding       = frame_r;
    s.ScrollbarRounding  = frame_r;
    s.TabRounding        = tab_r;
    s.WindowPadding      = ImVec2(16, 14);
    s.FramePadding       = ImVec2(pad_x, pad_y);
    s.ItemSpacing        = ImVec2(8, item_y);
    s.ScrollbarSize      = 12;
    s.ChildBorderSize    = 1;
    s.FrameBorderSize    = 1;
    s.WindowBorderSize   = 0;
    s.PopupBorderSize    = 1;
    s.ScaleAllSizes(dpi);

    ImVec4 * c                       = s.Colors;
    c[ImGuiCol_WindowBg]             = P.bg;
    c[ImGuiCol_ChildBg]              = P.panel;
    c[ImGuiCol_PopupBg]              = P.panel;
    c[ImGuiCol_Border]               = P.line;
    c[ImGuiCol_Text]                 = P.text;
    c[ImGuiCol_TextDisabled]         = P.muted;
    c[ImGuiCol_FrameBg]              = P.bg;
    c[ImGuiCol_FrameBgHovered]       = P.bg;
    c[ImGuiCol_FrameBgActive]        = P.bg;
    c[ImGuiCol_Button]               = P.chip;
    c[ImGuiCol_ButtonHovered]        = P.accent_soft;
    c[ImGuiCol_ButtonActive]         = P.accent_soft;
    c[ImGuiCol_Header]               = P.chip;
    c[ImGuiCol_HeaderHovered]        = P.accent_soft;
    c[ImGuiCol_HeaderActive]         = P.accent_soft;
    c[ImGuiCol_Tab]                  = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered]           = P.accent_soft;
    c[ImGuiCol_TabSelected]          = P.accent_soft;
    c[ImGuiCol_TabSelectedOverline]  = P.accent;
    c[ImGuiCol_TabDimmed]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected]    = P.accent_soft;
    c[ImGuiCol_CheckMark]            = P.accent;
    c[ImGuiCol_SliderGrab]           = P.accent;
    c[ImGuiCol_SliderGrabActive]     = P.accent;
    c[ImGuiCol_PlotHistogram]        = P.accent;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = P.line;
    c[ImGuiCol_ScrollbarGrabHovered] = P.muted;
    c[ImGuiCol_ScrollbarGrabActive]  = P.muted;
    c[ImGuiCol_TextSelectedBg]       = alpha(P.accent, 0.35f);
    c[ImGuiCol_NavCursor]            = P.accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_Separator]            = P.line;
}

// ---------------------------------------------------------------------------- Dégradé des jauges et barres de progression
// Toutes les jauges et barres de progression vont du vert (vide) au rouge vif (plein), en passant par le jaune
// puis l'orange : la couleur dépend de la position le long de la barre, pas de la valeur affichée.
static const ImVec4 GAUGE_STOPS[] = {
    ImVec4(34 / 255.f, 197 / 255.f, 94 / 255.f, 1),   // vert
    ImVec4(234 / 255.f, 210 / 255.f, 8 / 255.f, 1),   // jaune
    ImVec4(249 / 255.f, 115 / 255.f, 22 / 255.f, 1),  // orange
    ImVec4(255 / 255.f, 0 / 255.f, 0 / 255.f, 1),     // rouge vif
};
// Couleur à la position t (0 = début de la barre, 1 = fin).
static ImVec4 gauge_color(float t) {
    const int n = (int) (sizeof(GAUGE_STOPS) / sizeof(GAUGE_STOPS[0])) - 1;  // nombre d'intervalles
    t           = std::clamp(t, 0.f, 1.f) * n;
    int   i     = std::min((int) t, n - 1);
    float k     = t - i;
    const ImVec4 &a = GAUGE_STOPS[i], &b = GAUGE_STOPS[i + 1];
    return ImVec4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, 1);
}

// Remplace ImGui::ProgressBar : même cadre, mais partie remplie en dégradé vert → rouge vif.
// overlay : texte affiché au centre (nullptr = pourcentage, "" = aucun texte).
// Liste de mots séparés par des virgules, remise en forme pour un champ multiligne (1.0.34 : ImGui 1.91 ne coupe pas
// les lignes) : « mot, mot, » tant que la ligne tient dans width, sinon retour à la ligne après la virgule. Un retour
// à la ligne tapé compte comme une virgule ; les blancs en tête d'un mot sont retirés, les éléments vides aussi (sauf
// le dernier, en cours de frappe). cursor (facultatif) : position du curseur, recalculée pour rester au même endroit.
// 1.0.51 : retour à la ligne d'un texte libre (description de pochette) pour un champ multiligne : les retours à la ligne
// redeviennent des espaces, puis l'espace avant le mot qui dépasse width devient un retour à la ligne. La longueur ne
// change pas : le curseur reste au même endroit.
static void soft_wrap(std::string & s, float width) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    size_t line = 0, space = std::string::npos;
    auto   too_long = [&](size_t end) { return ImGui::CalcTextSize(s.data() + line, s.data() + end).x > width; };
    for (size_t i = 0; i <= s.size(); i++) {
        if (i < s.size() && s[i] != ' ') continue;
        if (too_long(i) && space != std::string::npos && space > line) {
            s[space] = '\n';
            line     = space + 1;
        }
        space = i;
    }
}

static std::string wrap_word_list(const std::string & text, float width, int * cursor) {
    struct Item {
        std::string s;
        bool        sep = false;  // suivi d'une virgule (ou d'un retour à la ligne)
    };
    std::vector<Item> items(1);
    bool              leading = true;     // blancs de tête de l'élément en cours
    int               cur_item = -1, cur_off = 0;
    const int         cpos = cursor ? *cursor : -1;
    for (int i = 0; i <= (int) text.size(); i++) {
        if (i == cpos) {
            cur_item = (int) items.size() - 1;
            cur_off  = (int) items.back().s.size();
        }
        if (i == (int) text.size()) break;
        char c = text[i];
        if (c == ',' || c == '\n' || c == ';') {
            items.back().sep = true;
            items.push_back({});
            leading = true;
        } else if (leading && (c == ' ' || c == '\t' || c == '\r')) {
            continue;
        } else {
            items.back().s += c;
            leading = false;
        }
    }
    std::string out, line;
    int         new_cursor = -1;
    for (int k = 0; k < (int) items.size(); k++) {
        const Item & it   = items[k];
        bool         last = k == (int) items.size() - 1;
        if (it.s.empty() && !last) {  // élément vide (« ,, ») : retiré ; le curseur passe au suivant
            if (cur_item == k) cur_item = k + 1, cur_off = 0;
            continue;
        }
        std::string piece = it.s + (it.sep ? "," : "");
        if (!out.empty()) {
            std::string trial = line + " " + piece;
            if (!line.empty() && ImGui::CalcTextSize(trial.c_str()).x > width) {
                out += "\n";
                line.clear();
            } else {
                out += " ";
                line += " ";
            }
        }
        if (cur_item == k) new_cursor = (int) out.size() + std::min(cur_off, (int) it.s.size());
        out += piece;
        line += piece;
    }
    if (cursor) *cursor = new_cursor >= 0 ? new_cursor : (int) out.size();
    return out;
}

static void progress_bar(float fraction, const ImVec2 & size, const char * overlay = nullptr) {
    fraction = std::clamp(fraction, 0.f, 1.f);
    // ImGui dessine le cadre seul (remplissage transparent et sans texte) ; on peint le dégradé par-dessus.
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0, 0, 0, 0));
    ImGui::ProgressBar(fraction, size, "");
    ImGui::PopStyleColor();
    ImVec2       mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    float        bw = ImGui::GetStyle().FrameBorderSize;
    mn.x += bw; mn.y += bw; mx.x -= bw; mx.y -= bw;
    float        full = mx.x - mn.x, end = mn.x + full * fraction;
    ImDrawList * dl   = ImGui::GetWindowDrawList();
    // Le dégradé est découpé en tronçons (vert→jaune, jaune→orange, orange→rouge), coupés à la fin du remplissage.
    const int n = (int) (sizeof(GAUGE_STOPS) / sizeof(GAUGE_STOPS[0])) - 1;
    for (int i = 0; i < n; i++) {
        float x0 = mn.x + full * i / n, x1 = mn.x + full * (i + 1) / n;
        if (x0 >= end) break;
        float  cut = std::min(x1, end);
        ImU32  c0  = ImGui::GetColorU32(GAUGE_STOPS[i]);
        ImU32  c1  = ImGui::GetColorU32(gauge_color((cut - mn.x) / full));
        dl->AddRectFilledMultiColor(ImVec2(x0, mn.y), ImVec2(cut, mx.y), c0, c1, c1, c0);
    }
    // Texte centré, blanc avec une ombre noire pour rester lisible sur toutes les couleurs.
    char        pct[16];
    const char *text = overlay;
    if (!text) { snprintf(pct, sizeof(pct), "%.0f%%", fraction * 100.f); text = pct; }
    if (*text) {
        ImVec2 ts = ImGui::CalcTextSize(text);
        ImVec2 tp(std::floor((mn.x + mx.x - ts.x) / 2), std::floor((mn.y + mx.y - ts.y) / 2));
        dl->PushClipRect(mn, mx, true);
        dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), text);
        dl->AddText(tp, IM_COL32(255, 255, 255, 255), text);
        dl->PopClipRect();
    }
}

// ---------------------------------------------------------------------------- Données du formulaire

// Balises de style en anglais (langue d'entraînement du modèle), rangées par onglet coloré.
struct ChipGroup {
    const char *              name;
    int                       color;  // indice de couleur : 0 genre, 1 ambiance, 2 voix, 3 instruments, 4 tempo/son
    std::vector<const char *> items;
};
static const ChipGroup STYLE_GROUPS[] = {
    { N_("Genres"), 0, { "pop", "pop rock", "rock", "hard rock", "alternative rock", "indie rock", "grunge", "post-grunge",
                     "punk", "punk rock", "pop punk", "post-punk", "new wave", "cold wave", "dark wave", "gothic rock",
                     "synth-pop", "shoegaze", "noise pop", "britpop", "industrial", "metal", "heavy metal", "nu metal",
                     "progressive rock", "psychedelic rock", "garage rock", "folk rock", "indie folk", "folk", "celtic",
                     "acoustic", "ballad", "country", "blues", "blues rock", "jazz", "soul", "funk", "disco", "R&B",
                     "hip hop", "trap", "reggae", "electronic", "synthwave", "EDM", "house", "techno", "trance",
                     "drum and bass", "ambient", "trip hop", "lo-fi", "cinematic", "orchestral", "French rock",
                     "French chanson",
                     "classic rock", "experimental", "soundtrack", "classical", "opera", "new age", "world", "rap",
                     "dance", "dance pop", "electro pop", "K-pop", "J-rock", "latin", "latin pop", "reggaeton",
                     "salsa", "bossa nova", "samba", "bolero", "dancehall", "ska", "rockabilly", "glam rock",
                     "noise rock", "post-rock", "country rock", "americana", "bluegrass", "gospel",
                     "singer-songwriter", "indie", "indie pop", "emo", "screamo", "hardcore", "post-hardcore",
                     "metalcore", "death metal", "melodic death metal", "thrash metal", "black metal",
                     "power metal", "symphonic metal", "downtempo", "chillout", "lounge", "easy listening",
                     "big band", "dubstep", "eurodance", "grime", "glitch", "8-bit" } },
    { N_("Ambiance"), 1, { "energetic", "melancholic", "uplifting", "dark", "cold", "dreamy", "nostalgic", "aggressive",
                       "romantic", "epic", "anthemic", "calm", "intimate", "mysterious", "happy", "sad", "haunting",
                       "intense", "explosive", "chaotic", "groovy", "hypnotic", "emotional", "rebellious",
                       "hopeful", "inspiring", "optimistic", "cheerful", "joyful", "festive", "playful", "relaxed",
                       "chill", "peaceful", "soothing", "gentle", "soft", "powerful", "passionate", "heartfelt",
                       "sentimental", "soulful", "introspective", "reflective", "pensive", "meditative",
                       "atmospheric", "dramatic", "brutal", "furious", "angry", "tough", "confident", "bold",
                       "seductive", "sensual", "elegant", "spiritual", "dissonant", "longing", "lively", "vibrant",
                       "exciting", "danceable", "futuristic", "gloomy", "motivational", "melodic", "easygoing",
                       "empowering" } },
    { N_("Voix"), 2, { "male vocal", "female vocal", "duet", "deep male vocal", "baritone vocal", "breathy female vocal",
                   "ethereal vocal", "raspy vocal", "falsetto", "whispered vocal", "powerful vocal", "screamed vocal",
                   "rapped vocal", "choir", "backing vocals", "vocal harmonies", "spoken word",
                   "tenor vocal", "soprano vocal", "mezzo-soprano vocal", "alto vocal", "bass vocal",
                   "child vocal", "husky vocal", "gravelly vocal", "gritty vocal", "smooth vocal", "warm vocal",
                   "soft vocal", "bright vocal", "airy vocal", "clear vocal", "rich vocal", "sweet vocal",
                   "soulful vocal", "sultry vocal", "nasal vocal", "hoarse vocal", "shouty vocal", "rough vocal",
                   "harsh vocal", "velvet vocal", "polished vocal", "youthful vocal", "mellow vocal",
                   "crisp vocal", "full-bodied vocal", "strong vocal", "vibrant vocal", "reverb vocal",
                   "vocal fry", "beatboxing" } },
    { N_("Instruments"), 3, { "electric guitar", "overdrive guitar", "distorted guitar", "clean guitar", "chorus guitar",
                          "jangly guitar", "acoustic guitar", "fingerpicking guitar", "twelve-string guitar",
                          "guitar solo", "feedback", "bass guitar", "distorted bass", "fretless bass", "synth bass",
                          "piano", "Rhodes", "organ", "keyboards", "analog synth", "synth pads", "arpeggiator", "strings",
                          "violin", "cello", "brass", "saxophone", "harmonica", "accordion", "bagpipes", "drums",
                          "double bass drum", "drum machine", "808", "gated reverb drums", "percussion",
                          "guitar", "bass", "synthesizer", "synth", "synth lead", "electric piano", "harpsichord",
                          "flute", "trumpet", "trombone", "oboe", "horns", "harp", "banjo", "ukulele", "sitar", "oud",
                          "qanun", "tabla", "congas", "bongos", "tambourine", "maracas", "glockenspiel", "marimba",
                          "vibraphone", "steel guitar", "pedal steel guitar", "dobro", "nylon string guitar",
                          "steel pan", "koto", "shakuhachi", "ney", "erhu", "turntables", "fiddle", "dulcimer", "zither",
                          "toms", "vocal samples", "808 bass", "acoustic drums", "synth drums", "funky bass",
                          "alto saxophone", "soprano saxophone", "cavaquinho", "charango", "pandeira", "dholak",
                          "bandura" } },
    { N_("Son"), 4, { "very slow", "slow", "mid-tempo", "upbeat", "fast", "very fast", "70s", "80s", "90s", "2000s", "vintage",
                      "reverb", "heavy reverb", "loud-quiet dynamics", "acoustic bridge", "lo-fi production", "live recording",
                      "minimal", "wall of sound", "polished production" } },
};

static ImVec4 category_color(int index) {
    const ImVec4 * colors[] = { &P.violet, &P.pink, &P.orange, &P.green, &P.blue };
    return *colors[std::clamp(index, 0, 4)];
}

// Balise qui parle de la voix (chant, timbre, chœur, langue chantée) : sans paroles, elle pousse le modèle à inventer du texte.
static bool is_voice_tag(const std::string & tag) {
    auto low = [](std::string x) {
        for (auto & c : x) c = (char) tolower((unsigned char) c);
        return x;
    };
    std::string t = low(tag);
    if (t.rfind("no ", 0) == 0 || t.rfind("without ", 0) == 0) return false;  // 1.0.37 : « no vocals » est gardé
    for (const ChipGroup & g : STYLE_GROUPS) {
        if (g.color != 2) continue;
        for (const char * item : g.items) if (t == low(item)) return true;
    }
    for (const char * w : { "vocal", "voice", "choir", "falsetto", "a cappella", "acapella", "spoken", "beatbox" }) {
        if (t.find(w) != std::string::npos) return true;
    }
    return false;
}

static const char * SONG_TAGS[] = { "[Intro]", "[Verse]", "[Pre-Chorus]", "[Chorus]", "[Bridge]", "[Interlude]", "[Outro]" };
// En instrumental, la LoRA attend des balises en minuscules, une par ligne, sans texte.
static const char * INST_TAGS[] = { "[intro]", "[verse]", "[pre-chorus]", "[chorus]", "[bridge]", "[outro]" };
static const char * COT_LABELS[] = { N_("Complet (mélodie + accords)"), N_("Mélodie seule"), N_("Sans partition (plus rapide)") };
static const char * COT_VALUES[] = { "full", "melody", "off" };
static const int    COT_FASTEST  = 2;

// Structures courantes (formes pop, AABA, strophique, build/drop…), en balises comprises par le modèle.
struct Structure {
    const char *              name;
    std::vector<const char *> sections;
};
static const Structure STRUCTURES[] = {
    { N_("Couplet / refrain (ABAB)"), { "intro", "verse", "chorus", "verse", "chorus", "outro" } },
    { N_("Pop avec pont (ABABCB)"), { "intro", "verse", "chorus", "verse", "chorus", "bridge", "chorus", "outro" } },
    { N_("Pop avec pré-refrain (ABCABCDC)"),
      { "intro", "verse", "pre-chorus", "chorus", "verse", "pre-chorus", "chorus", "bridge", "chorus", "outro" } },
    { N_("Refrain d'ouverture"), { "intro", "chorus", "verse", "chorus", "verse", "chorus", "outro" } },
    { N_("Double refrain final"), { "intro", "verse", "chorus", "verse", "chorus", "bridge", "chorus", "chorus", "outro" } },
    { N_("Ballade"), { "intro", "verse", "verse", "chorus", "verse", "chorus", "bridge", "chorus", "outro" } },
    { N_("Classique AABA (32 mesures)"), { "intro", "verse", "verse", "bridge", "verse", "outro" } },
    { N_("Strophique (AAA)"), { "intro", "verse", "verse", "verse", "outro" } },
    { N_("Rock avec solo"), { "intro", "verse", "chorus", "verse", "chorus", "interlude", "bridge", "chorus", "outro" } },
    { N_("Électro montée / drop"), { "intro", "verse", "pre-chorus", "chorus", "interlude", "pre-chorus", "chorus", "outro" } },
    { N_("Court (≈ 1 min)"), { "intro", "verse", "chorus", "outro" } },
};

static std::string structure_text(const Structure & st, bool instrumental) {
    std::string out;
    for (const char * name : st.sections) {
        std::string tag = name;
        if (instrumental) {
            if (tag == "interlude") tag = "bridge";  // la LoRA instrumentale ne connaît pas [interlude]
            out += "[" + tag + "]\n";
        } else {
            bool upper = true;
            for (auto & c : tag) {
                c     = upper ? (char) toupper((unsigned char) c) : c;
                upper = c == '-';
            }
            bool needs_text = tag != "Intro" && tag != "Outro" && tag != "Interlude";
            out += "[" + tag + "]\n" + (needs_text ? "\n" : "") + "\n";
        }
    }
    return out;
}

// Sections d'une structure de paroles sous forme de liste « Intro,Verse,Chorus » (balises [..] seules).
static std::string structure_tags(const std::string & lyrics) {
    std::string out;
    size_t      pos = 0;
    while (pos <= lyrics.size()) {
        size_t      end  = lyrics.find('\n', pos);
        std::string line = lyrics.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        size_t      a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
        if (a != std::string::npos && line[a] == '[' && line[b] == ']' && b > a + 1) {
            std::string tag = line.substr(a + 1, b - a - 1);
            std::replace(tag.begin(), tag.end(), ',', ' ');
            out += (out.empty() ? "" : ",") + tag;
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

// Liste de sections affichée « Intro > Verse > Chorus » (bulles d'aide, fenêtres).
static std::string arrow_text(const std::string & tags) {
    std::string out;
    for (char c : tags) out += c == ',' ? std::string(" > ") : std::string(1, c);
    return out;
}

// Paroles vierges à partir d'une liste de sections (comme structure_text pour les structures types).
static std::string skeleton_text(const std::string & tags, bool instrumental) {
    std::string out;
    size_t      pos = 0;
    while (pos < tags.size()) {
        size_t      end = tags.find(',', pos);
        std::string tag = tags.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos             = end == std::string::npos ? tags.size() : end + 1;
        if (tag.empty()) continue;
        std::string low = tag;
        for (auto & c : low) c = (char) tolower((unsigned char) c);
        if (instrumental) {
            out += "[" + tag + "]\n";
        } else {
            bool needs_text = low.rfind("intro", 0) != 0 && low.rfind("outro", 0) != 0 && low.rfind("interlude", 0) != 0;
            out += "[" + tag + "]\n" + (needs_text ? "\n" : "") + "\n";
        }
    }
    return out;
}

// LoRA choisie pour le prochain morceau : fichier de models/adapters, force, case cochée.
struct LoraUse {
    std::string file;
    float       scale   = 1.f;
    bool        enabled = true;
};

struct Form {
    std::vector<LoraUse> loras;
    std::string title, style, artist, album;
    std::string negative_style;          // balises négatives (expérimental) : style à éviter
    float       negative_scale = 1.5f;   // force du guidage négatif
    std::string lyrics_song = "[Intro]\n\n[Verse]\n\n\n[Chorus]\n\n\n[Outro]\n";
    std::string lyrics_inst = "[intro]\n[verse]\n[chorus]\n[verse]\n[chorus]\n[outro]\n";
    bool        instrumental  = false;
    bool        auto_duration = true;  // le modèle conclut seul ; plafond calculé
    bool        long_track    = false; // 1.0.48 : morceau long en 2 parties enchaînées (prototype)
    bool        fade_structure = false;  // fin en fondu sur la dernière section de la structure
    int         engine = 0, cot = COT_FASTEST, duration = 180, versions = 1, language = 0;
    int64_t     seed = -1;
    fs::path    cover;  // fichier audio source d'une reprise
    double      cover_seconds = 0;
    int         cursor = -1;
    bool        dirty  = false;
    bool        pending_structure = false;  // structure à appliquer après confirmation
    std::string pending_text, pending_name;
};

static void set_mode(Form & f, bool instrumental) {
    f.instrumental = instrumental;
    f.cot          = f.cover.empty() ? COT_FASTEST : (instrumental ? 0 : 1);
    f.cursor       = -1;
    f.dirty        = true;
}

static std::string read_text(const fs::path & path) {
    std::FILE * file = _wfopen(path.c_str(), L"rb");
    std::string text;
    if (file) {
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), file)) > 0;) text.append(buf, n);
        fclose(file);
    }
    return text;
}

static void write_text(const fs::path & path, const std::string & text) {
    std::FILE * file = _wfopen(path.c_str(), L"wb");
    if (file) {
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
    }
}

static std::string write_json(yyjson_mut_doc * doc) {
    size_t      len  = 0;
    char *      json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);
    std::string out(json ? json : "", len);
    free(json);
    yyjson_mut_doc_free(doc);
    return out;
}

static void load_draft(Form & f, const fs::path & path) {
    std::string  text = read_text(path);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return;
    yyjson_val * r = yyjson_doc_get_root(doc);
    auto str = [&](const char * k, std::string & out) {
        yyjson_val * v = yyjson_obj_get(r, k);
        if (yyjson_is_str(v)) out = yyjson_get_str(v);
    };
    // Le nom et le style ne sont jamais pré-remplis : seuls les paroles et le mode sont restaurés.
    str("lyrics_song", f.lyrics_song);
    str("lyrics_inst", f.lyrics_inst);
    if (yyjson_get_bool(yyjson_obj_get(r, "instrumental"))) set_mode(f, true);
    {
        size_t       i2, m2;
        yyjson_val * item;
        yyjson_arr_foreach(yyjson_obj_get(r, "loras"), i2, m2, item) {
            yyjson_val * name = yyjson_obj_get(item, "file");
            yyjson_val * sc   = yyjson_obj_get(item, "scale");
            if (!yyjson_is_str(name)) continue;
            LoraUse u;
            u.file    = yyjson_get_str(name);
            u.scale   = yyjson_is_num(sc) ? (float) yyjson_get_num(sc) : 1.f;
            u.enabled = yyjson_get_bool(yyjson_obj_get(item, "enabled"));
            f.loras.push_back(u);
        }
    }
    f.dirty = false;
    yyjson_doc_free(doc);
}

static void save_draft(Form & f, const fs::path & path) {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_strcpy(doc, o, "lyrics_song", f.lyrics_song.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "lyrics_inst", f.lyrics_inst.c_str());
    yyjson_mut_obj_add_bool(doc, o, "instrumental", f.instrumental);
    yyjson_mut_val * arr = yyjson_mut_obj_add_arr(doc, o, "loras");
    for (const LoraUse & u : f.loras) {
        yyjson_mut_val * item = yyjson_mut_arr_add_obj(doc, arr);
        yyjson_mut_obj_add_strcpy(doc, item, "file", u.file.c_str());
        yyjson_mut_obj_add_real(doc, item, "scale", u.scale);
        yyjson_mut_obj_add_bool(doc, item, "enabled", u.enabled);
    }
    write_text(path, write_json(doc));
    f.dirty = false;
}

// Réglages conservés entre les sessions.
// Préréglage : un style, et facultativement des LoRA (avec leur force) et la structure des paroles (liste de
// sections, séparées par des virgules : « Intro,Verse,Chorus… ») pour une chanson et/ou un instrumental.
struct Preset {
    std::string          name, style;
    std::vector<LoraUse> loras;
    std::string          structure_song, structure_inst;
    // Style à éviter (balises négatives) : has_negative = le préréglage en mémorise un (même vide) ;
    // faux pour les préréglages enregistrés avant la version 1.0.10, qui ne touchent pas à ce champ.
    bool                 has_negative = false;
    std::string          negative_style;
    float                negative_scale = 1.5f;
    std::string          pack;  // pack d'origine (fichier .yue2pack importé) ; vide = préréglage créé ici
};
// Structure personnelle (1.0.18) : liste de sections « Intro,Verse,Chorus… » (comme structure_tags), valable pour une
// chanson comme pour un instrumental. pack : nom du pack d'origine ; vide = structure enregistrée ici.
struct UserStructure {
    std::string name, sections, pack;
};
// Pack installé (fichier .yue2pack importé avec un nom de pack) : informations affichées dans Réglages › Génération.
struct PackInfo {
    std::string name, author, version, description;
};
// Balises rapides proposées sous « Style à éviter » au premier lancement (et par « Rétablir la liste par défaut »).
static std::vector<std::string> default_negative_tags() {
    return { "vocoder", "autotune", "robotic voice", "synthesizer", "electronic drums", "distorted guitar", "choir", "spoken word" };
}

struct Settings {
    bool                notifications = true;  // pastilles dans la zone de notification
    int                 language      = 0;
    std::string         ui_language;           // langue de l'application (code d'un fichier de « langues ») ; vide = langue du système
    int                 volume        = 80;
    int                 sort          = 0;      // ordre de la bibliothèque
    bool                autoplay      = false;  // lecture continue
    bool                game_pause    = true;   // suspendre la file pendant un jeu
    std::string         game_list     = "cs2.exe, VRising.exe, VRisingClient.exe";
    std::string         gpu_driver;             // version du pilote lors du dernier test de santé
    std::string         video_dir;              // dossier de destination des vidéos
    std::string         model;                  // modèle par défaut (fichier GGUF) ; vide = Q8_0
    std::string         solver;                 // solveur par défaut de la phase NAR : « ab2 » ou « midpoint » ; vide = ab2
    std::string         gpu_compat  = "auto";   // compatibilité de la carte : « auto », « stable » (FP32), « fast » (FP16)
    std::string         gpu_backend = "auto";   // moteur de calcul : « auto », « vulkan », « cuda » (pris en compte au démarrage)
    int                 fade_seconds = 5;       // fondu de fin des morceaux coupés au plafond de durée (secondes)
    std::vector<std::string> collapsed;     // blocs du formulaire repliés
    int                 theme      = 0;         // thème graphique (voir THEME_NAMES)
    int                 mark_style = 1;         // marques validé / à problème : 0 pouces, 1 coche et croix (défaut 1.0.31, livrable de Herve), 2 commutateur
    int                 card_mode  = 0;         // cartes de la bibliothèque : 0 détaillée, 1 compacte, 2 pochette
    int                 style_mode = 0;         // bloc Style : 0 classique, 1 pastilles, 2 constructeur
    bool                analysis_auto = true;   // analyser le fichier choisi pour une reprise dès sa sélection
    bool                lyrics_web    = true;   // reprise : chercher les paroles sur Internet (LRCLIB) pour corriger Whisper
    std::string         update_url;             // adresse de recherche de mise à jour ; vide = UPDATE_MANIFEST_URL (version.hpp)
    // Balises rapides du champ « Style à éviter » : modifiables dans Réglages › Génération ; chaque terme
    // tapé dans le champ y est ajouté automatiquement.
    std::vector<std::string> negative_tags = default_negative_tags();
    bool                translate_web    = true;   // traduction des paroles : essayer Internet d'abord
    std::string         translate_target = "en";   // dernière langue choisie pour « Traduire »
    bool                tray_gauges      = true;   // application réduite : jauges et compteurs près de l'horloge
    int                 gauge_style      = 1;      // compteurs de l'en-tête : 0 Sport, 1 Compact
    int                 gpu_limit        = 100;    // limite d'utilisation de la carte en % (100 = aucune)
    std::map<std::string, std::string> covers;  // clé d'album -> chemin de la pochette
    std::vector<Preset> presets;
    std::vector<UserStructure> structures;  // structures personnelles et structures des packs
    std::vector<PackInfo>      packs;       // packs importés
    // Composer avec l'IA (1.0.26) : moteur, modèles, clés (chiffrées par Windows) et dernier formulaire.
    std::string                        ai_engine = "local";  // « local » (llama.cpp intégré), « server » ou identifiant d'un service en ligne
    std::string                        ai_last_service;      // dernier service en ligne choisi
    std::string                        ai_local_model;       // fichier GGUF de modeles\ia utilisé par l'IA locale
    // Atelier clip (1.0.54) : textes affichés au début et à la fin des clips (« Rock Ballads Fr », « Abonnez-vous »…) et type de rendu
    std::string                        clip_text_start, clip_text_end, clip_style_id = "auto", clip_image_model = "klein", clip_image_count = "0", clip_image_mode = "rec";
    std::string                        clip_short_seconds = "30";  // 1.0.75 : durée des Shorts (15, 30, 45, 60 ou 90 s)
    bool                               ai_gpu = true;        // IA locale sur la carte graphique (version Vulkan de llama.cpp)
    std::string                        ai_server_url, ai_server_name, ai_server_model;  // serveur local connecté
    std::string                        ai_custom_url;        // service « compatible OpenAI » personnalisé
    std::map<std::string, std::string> ai_keys;              // service → clé d'API chiffrée (DPAPI, base64)
    std::map<std::string, std::string> ai_models;            // service → modèle choisi
    int                                ai_language = -1;     // indice dans LANGUAGES (-1 : français au premier affichage)
    std::string                        ai_influence, ai_theme, ai_mood, ai_title, ai_extra;
    int                                ai_length       = 1;  // 0 courte, 1 normale, 2 longue, 3 très longue (1.0.70 : 3 parties)
    bool                               ai_instrumental = false;
    bool                               ai_profile      = true;  // compléter le style avec le profil d'influence connu
    // Anti-répétition et album (1.0.33)
    std::vector<std::string>           ai_avoid        = ai_default_avoid_words();  // mots à éviter (Réglages › IA)
    bool                               ai_auto_avoid   = true;   // éviter aussi les mots fréquents des derniers morceaux
    std::vector<std::string>           ai_recent;                // titre + paroles des derniers morceaux écrits par l'IA (15 au plus)
    bool                               ai_album_mode   = false;  // onglet : un morceau ou un album
    int                                ai_count        = 10;     // album : nombre de morceaux (1 à 50)
    std::string                        ai_album_title, ai_band;  // album : imposés (vides : l'IA choisit)
    bool                               ai_mixed        = false;  // album : chansons et instrumentaux mélangés
    int                                ai_instrumentals = 0;     // album mélangé : nombre d'instrumentaux (0 = aucun)
    std::string                        ai_negative;              // 1.0.34 : styles à éviter de l'onglet (balises à virgules)
    int                                ai_max_instrumentals = 2; // 1.0.35 : album mélangé, instrumentaux au plus (Réglages › IA, 0 à 50)
    // Pochettes générées (1.0.36) : service d'images, modèle par service, cases de l'onglet, description par album
    std::string                        ai_image_engine = "local";  // « local », « openai », « xai », « together », « custom »
    std::string                        ai_image_local  = "klein";  // 1.0.50 : modèle local « klein », « zimage » ou « moss »
    std::map<std::string, std::string> ai_image_models;            // service → modèle choisi (vide = celui proposé)
    bool                               ai_covers = false, ai_cover_youtube = false, ai_cover_tiktok = false;
    std::map<std::string, std::string> cover_prompts;              // clé d'album → description de l'image (anglais)
    std::map<std::string, std::string> album_sounds;               // 1.0.47 : clé d'album → son commun (balises du plan de l'IA)
};

// ---- Lecture et écriture JSON des préréglages, structures et packs (réglages et fichiers .yue2pack)

static std::string json_str(yyjson_val * obj, const char * key) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string();
}

static bool preset_from_json(yyjson_val * item, Preset & p) {
    yyjson_val * name  = yyjson_obj_get(item, "name");
    yyjson_val * style = yyjson_obj_get(item, "style");
    if (!yyjson_is_str(name) || !yyjson_is_str(style)) return false;
    p.name           = yyjson_get_str(name);
    p.style          = yyjson_get_str(style);
    p.structure_song = json_str(item, "structure_song");
    p.structure_inst = json_str(item, "structure_inst");
    p.pack           = json_str(item, "pack");
    yyjson_val * v2  = yyjson_obj_get(item, "negative_style");
    if (yyjson_is_str(v2)) {
        p.has_negative   = true;
        p.negative_style = yyjson_get_str(v2);
        v2               = yyjson_obj_get(item, "negative_scale");
        if (yyjson_is_num(v2)) p.negative_scale = (float) yyjson_get_num(v2);
    }
    size_t       i3, m3;
    yyjson_val * lo;
    yyjson_arr_foreach(yyjson_obj_get(item, "loras"), i3, m3, lo) {
        yyjson_val * file = yyjson_obj_get(lo, "file");
        yyjson_val * sc   = yyjson_obj_get(lo, "scale");
        if (!yyjson_is_str(file)) continue;
        p.loras.push_back({ yyjson_get_str(file), yyjson_is_num(sc) ? (float) yyjson_get_num(sc) : 1.f, true });
    }
    return true;
}

// with_pack : faux dans un fichier .yue2pack (le nom du pack est écrit une seule fois, en tête du fichier).
static void preset_to_json(yyjson_mut_doc * doc, yyjson_mut_val * arr, const Preset & p, bool with_pack) {
    yyjson_mut_val * item = yyjson_mut_arr_add_obj(doc, arr);
    yyjson_mut_obj_add_strcpy(doc, item, "name", p.name.c_str());
    yyjson_mut_obj_add_strcpy(doc, item, "style", p.style.c_str());
    if (!p.structure_song.empty()) yyjson_mut_obj_add_strcpy(doc, item, "structure_song", p.structure_song.c_str());
    if (!p.structure_inst.empty()) yyjson_mut_obj_add_strcpy(doc, item, "structure_inst", p.structure_inst.c_str());
    if (p.has_negative) {
        yyjson_mut_obj_add_strcpy(doc, item, "negative_style", p.negative_style.c_str());
        yyjson_mut_obj_add_real(doc, item, "negative_scale", p.negative_scale);
    }
    if (!p.loras.empty()) {
        yyjson_mut_val * la = yyjson_mut_obj_add_arr(doc, item, "loras");
        for (const LoraUse & u : p.loras) {
            yyjson_mut_val * lo = yyjson_mut_arr_add_obj(doc, la);
            yyjson_mut_obj_add_strcpy(doc, lo, "file", u.file.c_str());
            yyjson_mut_obj_add_real(doc, lo, "scale", u.scale);
        }
    }
    if (with_pack && !p.pack.empty()) yyjson_mut_obj_add_strcpy(doc, item, "pack", p.pack.c_str());
}

static bool structure_from_json(yyjson_val * item, UserStructure & st) {
    st.name     = json_str(item, "name");
    st.sections = json_str(item, "sections");
    st.pack     = json_str(item, "pack");
    return !st.name.empty() && !st.sections.empty();
}

static void structure_to_json(yyjson_mut_doc * doc, yyjson_mut_val * arr, const UserStructure & st, bool with_pack) {
    yyjson_mut_val * item = yyjson_mut_arr_add_obj(doc, arr);
    yyjson_mut_obj_add_strcpy(doc, item, "name", st.name.c_str());
    yyjson_mut_obj_add_strcpy(doc, item, "sections", st.sections.c_str());
    if (with_pack && !st.pack.empty()) yyjson_mut_obj_add_strcpy(doc, item, "pack", st.pack.c_str());
}

static PackInfo pack_info_from_json(yyjson_val * o) {
    return { json_str(o, "name"), json_str(o, "author"), json_str(o, "version"), json_str(o, "description") };
}

static void pack_info_to_json(yyjson_mut_doc * doc, yyjson_mut_val * o, const PackInfo & info) {
    yyjson_mut_obj_add_strcpy(doc, o, "name", info.name.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "author", info.author.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "version", info.version.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "description", info.description.c_str());
}

// Lit les clés présentes dans un objet JSON de réglages (les absentes gardent leur valeur) : sert pour chacun des
// fichiers de config\ (1.0.41) comme pour l'ancien donnees\studio_reglages.json qui les contenait toutes.
static void parse_settings(Settings & s, yyjson_val * r) {
    yyjson_val * v = yyjson_obj_get(r, "notifications");
    if (yyjson_is_bool(v)) s.notifications = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "language");
    if (yyjson_is_int(v)) s.language = std::clamp((int) yyjson_get_int(v), 0, (int) LANGUAGES.size() - 1);
    v = yyjson_obj_get(r, "ui_language");
    if (yyjson_is_str(v)) s.ui_language = yyjson_get_str(v);
    v = yyjson_obj_get(r, "volume");
    if (yyjson_is_int(v)) s.volume = std::clamp((int) yyjson_get_int(v), 0, 100);
    v = yyjson_obj_get(r, "sort");
    if (yyjson_is_int(v)) s.sort = std::clamp((int) yyjson_get_int(v), 0, 5);
    v = yyjson_obj_get(r, "autoplay");
    if (yyjson_is_bool(v)) s.autoplay = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "game_pause");
    if (yyjson_is_bool(v)) s.game_pause = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "game_list");
    if (yyjson_is_str(v)) s.game_list = yyjson_get_str(v);
    v = yyjson_obj_get(r, "gpu_driver");
    if (yyjson_is_str(v)) s.gpu_driver = yyjson_get_str(v);
    v = yyjson_obj_get(r, "video_dir");
    if (yyjson_is_str(v)) s.video_dir = yyjson_get_str(v);
    v = yyjson_obj_get(r, "model");
    if (yyjson_is_str(v)) s.model = yyjson_get_str(v);
    v = yyjson_obj_get(r, "solver");
    if (yyjson_is_str(v)) s.solver = yyjson_get_str(v);
    v = yyjson_obj_get(r, "gpu_compat");
    if (yyjson_is_str(v)) s.gpu_compat = yyjson_get_str(v);
    v = yyjson_obj_get(r, "gpu_backend");
    if (yyjson_is_str(v)) s.gpu_backend = yyjson_get_str(v);
    v = yyjson_obj_get(r, "fade_seconds");
    if (yyjson_is_int(v)) s.fade_seconds = std::clamp((int) yyjson_get_int(v), 1, 30);
    v = yyjson_obj_get(r, "mark_style");
    if (yyjson_is_int(v)) s.mark_style = std::clamp((int) yyjson_get_int(v), 0, 2);
    {
        size_t       i4, m4;
        yyjson_val * c4;
        yyjson_arr_foreach(yyjson_obj_get(r, "collapsed"), i4, m4, c4) {
            if (yyjson_is_str(c4)) s.collapsed.push_back(yyjson_get_str(c4));
        }
    }
    v = yyjson_obj_get(r, "theme");
    if (yyjson_is_int(v)) s.theme = std::clamp((int) yyjson_get_int(v), 0, THEME_COUNT - 1);
    v = yyjson_obj_get(r, "card_mode");
    if (yyjson_is_int(v)) s.card_mode = std::clamp((int) yyjson_get_int(v), 0, 3);
    v = yyjson_obj_get(r, "style_mode");
    if (yyjson_is_int(v)) s.style_mode = std::clamp((int) yyjson_get_int(v), 0, 2);
    v = yyjson_obj_get(r, "analysis_auto");
    if (yyjson_is_bool(v)) s.analysis_auto = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "update_url");
    if (yyjson_is_str(v)) s.update_url = yyjson_get_str(v);
    v = yyjson_obj_get(r, "lyrics_web");
    if (yyjson_is_bool(v)) s.lyrics_web = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "translate_web");
    if (yyjson_is_bool(v)) s.translate_web = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "translate_target");
    if (yyjson_is_str(v)) s.translate_target = yyjson_get_str(v);
    v = yyjson_obj_get(r, "tray_gauges");
    if (yyjson_is_bool(v)) s.tray_gauges = yyjson_get_bool(v);
    v = yyjson_obj_get(r, "gauge_style");
    if (yyjson_is_int(v)) s.gauge_style = std::clamp((int) yyjson_get_int(v), 0, 1);
    v = yyjson_obj_get(r, "gpu_limit");
    if (yyjson_is_int(v)) s.gpu_limit = std::clamp((int) yyjson_get_int(v), 30, 100);
    v = yyjson_obj_get(r, "negative_tags");
    if (yyjson_is_arr(v)) {  // liste enregistrée (même vide : l'utilisateur a pu tout supprimer)
        s.negative_tags.clear();
        size_t       i5, m5;
        yyjson_val * t5;
        yyjson_arr_foreach(v, i5, m5, t5) {
            if (yyjson_is_str(t5)) s.negative_tags.push_back(yyjson_get_str(t5));
        }
    }
    {
        size_t       i2, m2;
        yyjson_val * key;
        yyjson_val * val;
        yyjson_obj_foreach(yyjson_obj_get(r, "covers"), i2, m2, key, val) {
            if (yyjson_is_str(key) && yyjson_is_str(val)) s.covers[yyjson_get_str(key)] = yyjson_get_str(val);
        }
    }
    size_t       idx, max;
    yyjson_val * item;
    yyjson_arr_foreach(yyjson_obj_get(r, "presets"), idx, max, item) {
        Preset p;
        if (preset_from_json(item, p)) s.presets.push_back(std::move(p));
    }
    yyjson_arr_foreach(yyjson_obj_get(r, "structures"), idx, max, item) {
        UserStructure st;
        if (structure_from_json(item, st)) s.structures.push_back(std::move(st));
    }
    yyjson_arr_foreach(yyjson_obj_get(r, "packs"), idx, max, item) {
        PackInfo info = pack_info_from_json(item);
        if (!info.name.empty()) s.packs.push_back(std::move(info));
    }
    // Composer avec l'IA (1.0.26)
    const std::pair<const char *, std::string *> ai_texts[] = {
        { "clip_text_start", &s.clip_text_start }, { "clip_text_end", &s.clip_text_end }, { "clip_style", &s.clip_style_id },
        { "clip_image_model", &s.clip_image_model }, { "clip_image_count", &s.clip_image_count }, { "clip_image_mode", &s.clip_image_mode },
        { "clip_short_seconds", &s.clip_short_seconds },
        { "ai_engine", &s.ai_engine },           { "ai_last_service", &s.ai_last_service }, { "ai_local_model", &s.ai_local_model },
        { "ai_server_url", &s.ai_server_url },   { "ai_server_name", &s.ai_server_name },   { "ai_server_model", &s.ai_server_model },
        { "ai_custom_url", &s.ai_custom_url },   { "ai_influence", &s.ai_influence },       { "ai_theme", &s.ai_theme },
        { "ai_mood", &s.ai_mood },               { "ai_title", &s.ai_title },               { "ai_extra", &s.ai_extra } };
    for (const auto & [key, dst] : ai_texts) {
        v = yyjson_obj_get(r, key);
        if (yyjson_is_str(v)) *dst = yyjson_get_str(v);
    }
    const std::pair<const char *, bool *> ai_flags[] = { { "ai_gpu", &s.ai_gpu }, { "ai_instrumental", &s.ai_instrumental }, { "ai_profile", &s.ai_profile } };
    for (const auto & [key, dst] : ai_flags) {
        v = yyjson_obj_get(r, key);
        if (yyjson_is_bool(v)) *dst = yyjson_get_bool(v);
    }
    v = yyjson_obj_get(r, "ai_language");
    if (yyjson_is_int(v)) s.ai_language = std::clamp((int) yyjson_get_int(v), -1, (int) LANGUAGES.size() - 1);
    v = yyjson_obj_get(r, "ai_length");
    if (yyjson_is_int(v)) s.ai_length = std::clamp((int) yyjson_get_int(v), 0, 3);
    // 1.0.33 : anti-répétition et album
    const std::pair<const char *, std::vector<std::string> *> ai_lists[] = { { "ai_avoid", &s.ai_avoid }, { "ai_recent", &s.ai_recent } };
    for (const auto & [key, dst] : ai_lists) {
        v = yyjson_obj_get(r, key);
        if (!yyjson_is_arr(v)) continue;  // absent : liste par défaut (mots à éviter) ou vide (derniers morceaux)
        dst->clear();
        size_t       i7, m7;
        yyjson_val * t7;
        yyjson_arr_foreach(v, i7, m7, t7) {
            if (yyjson_is_str(t7)) dst->push_back(yyjson_get_str(t7));
        }
    }
    const std::pair<const char *, bool *> ai_flags2[] = { { "ai_auto_avoid", &s.ai_auto_avoid }, { "ai_album_mode", &s.ai_album_mode }, { "ai_mixed", &s.ai_mixed } };
    for (const auto & [key, dst] : ai_flags2) {
        v = yyjson_obj_get(r, key);
        if (yyjson_is_bool(v)) *dst = yyjson_get_bool(v);
    }
    v = yyjson_obj_get(r, "ai_count");
    if (yyjson_is_int(v)) s.ai_count = std::clamp((int) yyjson_get_int(v), 1, 50);
    v = yyjson_obj_get(r, "ai_instrumentals");
    if (yyjson_is_int(v)) s.ai_instrumentals = std::clamp((int) yyjson_get_int(v), 0, 50);
    // 1.0.36 : pochettes générées
    v = yyjson_obj_get(r, "ai_image_local");  // 1.0.50
    if (yyjson_is_str(v)) s.ai_image_local = yyjson_get_str(v);
    v = yyjson_obj_get(r, "ai_image_engine");
    if (yyjson_is_str(v)) s.ai_image_engine = yyjson_get_str(v);
    for (const auto & [key, dst] : { std::pair<const char *, bool *>{ "ai_covers", &s.ai_covers }, { "ai_cover_youtube", &s.ai_cover_youtube },
                                     { "ai_cover_tiktok", &s.ai_cover_tiktok } }) {
        v = yyjson_obj_get(r, key);
        if (yyjson_is_bool(v)) *dst = yyjson_get_bool(v);
    }
    for (const auto & [key, dst] : { std::pair<const char *, std::map<std::string, std::string> *>{ "ai_image_models", &s.ai_image_models },
                                     { "cover_prompts", &s.cover_prompts }, { "album_sounds", &s.album_sounds } }) {
        size_t       i8, m8;
        yyjson_val * k8;
        yyjson_val * v8;
        yyjson_obj_foreach(yyjson_obj_get(r, key), i8, m8, k8, v8) {
            if (yyjson_is_str(k8) && yyjson_is_str(v8)) (*dst)[yyjson_get_str(k8)] = yyjson_get_str(v8);
        }
    }
    v = yyjson_obj_get(r, "ai_max_instrumentals");
    if (yyjson_is_int(v)) s.ai_max_instrumentals = std::clamp((int) yyjson_get_int(v), 0, 50);
    for (const auto & [key, dst] : { std::pair<const char *, std::string *>{ "ai_album_title", &s.ai_album_title }, { "ai_band", &s.ai_band },
                                     { "ai_negative", &s.ai_negative } }) {
        v = yyjson_obj_get(r, key);
        if (yyjson_is_str(v)) *dst = yyjson_get_str(v);
    }
    const std::pair<const char *, std::map<std::string, std::string> *> ai_maps[] = { { "ai_keys", &s.ai_keys }, { "ai_models", &s.ai_models } };
    for (const auto & [key, dst] : ai_maps) {
        size_t       i6, m6;
        yyjson_val * k6;
        yyjson_val * v6;
        yyjson_obj_foreach(yyjson_obj_get(r, key), i6, m6, k6, v6) {
            if (yyjson_is_str(k6) && yyjson_is_str(v6)) (*dst)[yyjson_get_str(k6)] = yyjson_get_str(v6);
        }
    }
}

// 1.0.41 : réglages rangés dans config\ en plusieurs fichiers (reglages.json reçoit toutes les clés non listées ici).
// cles_api.json : clés chiffrées par Windows (DPAPI) pour ce compte sur ce PC, illisibles sur un autre PC.
static const std::pair<const char *, std::vector<const char *>> SETTINGS_FILES[] = {
    { "styles.json", { "presets", "structures", "packs" } },
    { "mots_a_eviter.json", { "ai_avoid", "ai_auto_avoid", "ai_recent" } },
    { "cles_api.json", { "ai_keys" } },
};

static bool parse_settings_file(Settings & s, const fs::path & file) {
    std::string  text = read_text(file);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return false;
    parse_settings(s, yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return true;
}

static std::string trim(const std::string & s);  // définie plus bas (aides UI)

// path = config\reglages.json
static void load_settings(Settings & s, const fs::path & path) {
    parse_settings_file(s, path);
    for (const auto & [name, keys] : SETTINGS_FILES) parse_settings_file(s, path.parent_path() / name);
}

static void save_settings(const Settings & s, const fs::path & path) {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_bool(doc, o, "notifications", s.notifications);
    yyjson_mut_obj_add_int(doc, o, "language", s.language);
    yyjson_mut_obj_add_strcpy(doc, o, "ui_language", s.ui_language.c_str());
    yyjson_mut_obj_add_int(doc, o, "volume", s.volume);
    yyjson_mut_obj_add_int(doc, o, "sort", s.sort);
    yyjson_mut_obj_add_bool(doc, o, "autoplay", s.autoplay);
    yyjson_mut_obj_add_bool(doc, o, "game_pause", s.game_pause);
    yyjson_mut_obj_add_strcpy(doc, o, "game_list", s.game_list.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "gpu_driver", s.gpu_driver.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "video_dir", s.video_dir.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "model", s.model.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "solver", s.solver.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "gpu_compat", s.gpu_compat.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "gpu_backend", s.gpu_backend.c_str());
    yyjson_mut_obj_add_int(doc, o, "fade_seconds", s.fade_seconds);
    yyjson_mut_obj_add_int(doc, o, "mark_style", s.mark_style);
    {
        yyjson_mut_val * ca = yyjson_mut_obj_add_arr(doc, o, "collapsed");
        for (const std::string & id : s.collapsed) yyjson_mut_arr_add_strcpy(doc, ca, id.c_str());
    }
    yyjson_mut_obj_add_int(doc, o, "theme", s.theme);
    yyjson_mut_obj_add_int(doc, o, "card_mode", s.card_mode);
    yyjson_mut_obj_add_int(doc, o, "style_mode", s.style_mode);
    yyjson_mut_obj_add_bool(doc, o, "analysis_auto", s.analysis_auto);
    yyjson_mut_obj_add_bool(doc, o, "lyrics_web", s.lyrics_web);
    yyjson_mut_obj_add_bool(doc, o, "translate_web", s.translate_web);
    yyjson_mut_obj_add_strcpy(doc, o, "translate_target", s.translate_target.c_str());
    yyjson_mut_obj_add_bool(doc, o, "tray_gauges", s.tray_gauges);
    yyjson_mut_obj_add_int(doc, o, "gauge_style", s.gauge_style);
    yyjson_mut_obj_add_int(doc, o, "gpu_limit", s.gpu_limit);
    {
        yyjson_mut_val * na = yyjson_mut_obj_add_arr(doc, o, "negative_tags");
        for (const std::string & t : s.negative_tags) yyjson_mut_arr_add_strcpy(doc, na, t.c_str());
    }
    yyjson_mut_obj_add_strcpy(doc, o, "update_url", s.update_url.c_str());
    {
        yyjson_mut_val * covers = yyjson_mut_obj_add_obj(doc, o, "covers");
        for (auto & [k, v] : s.covers) {
            yyjson_mut_obj_add(covers, yyjson_mut_strcpy(doc, k.c_str()), yyjson_mut_strcpy(doc, v.c_str()));
        }
    }
    yyjson_mut_val * arr = yyjson_mut_obj_add_arr(doc, o, "presets");
    for (const Preset & p : s.presets) preset_to_json(doc, arr, p, true);
    yyjson_mut_val * sta = yyjson_mut_obj_add_arr(doc, o, "structures");
    for (const UserStructure & st : s.structures) structure_to_json(doc, sta, st, true);
    yyjson_mut_val * pka = yyjson_mut_obj_add_arr(doc, o, "packs");
    for (const PackInfo & info : s.packs) pack_info_to_json(doc, yyjson_mut_arr_add_obj(doc, pka), info);
    // Composer avec l'IA (1.0.26)
    const std::pair<const char *, const std::string *> ai_texts[] = {
        { "clip_text_start", &s.clip_text_start }, { "clip_text_end", &s.clip_text_end }, { "clip_style", &s.clip_style_id },
        { "clip_image_model", &s.clip_image_model }, { "clip_image_count", &s.clip_image_count }, { "clip_image_mode", &s.clip_image_mode },
        { "clip_short_seconds", &s.clip_short_seconds },
        { "ai_engine", &s.ai_engine },           { "ai_last_service", &s.ai_last_service }, { "ai_local_model", &s.ai_local_model },
        { "ai_server_url", &s.ai_server_url },   { "ai_server_name", &s.ai_server_name },   { "ai_server_model", &s.ai_server_model },
        { "ai_custom_url", &s.ai_custom_url },   { "ai_influence", &s.ai_influence },       { "ai_theme", &s.ai_theme },
        { "ai_mood", &s.ai_mood },               { "ai_title", &s.ai_title },               { "ai_extra", &s.ai_extra } };
    for (const auto & [key, val] : ai_texts) yyjson_mut_obj_add_strcpy(doc, o, key, val->c_str());
    yyjson_mut_obj_add_bool(doc, o, "ai_gpu", s.ai_gpu);
    yyjson_mut_obj_add_bool(doc, o, "ai_instrumental", s.ai_instrumental);
    yyjson_mut_obj_add_bool(doc, o, "ai_profile", s.ai_profile);
    yyjson_mut_obj_add_int(doc, o, "ai_language", s.ai_language);
    yyjson_mut_obj_add_int(doc, o, "ai_length", s.ai_length);
    // 1.0.33 : anti-répétition et album
    for (const auto & [key, list] : { std::pair<const char *, const std::vector<std::string> *>{ "ai_avoid", &s.ai_avoid }, { "ai_recent", &s.ai_recent } }) {
        yyjson_mut_val * a = yyjson_mut_obj_add_arr(doc, o, key);
        for (const std::string & t : *list) yyjson_mut_arr_add_strcpy(doc, a, t.c_str());
    }
    yyjson_mut_obj_add_bool(doc, o, "ai_auto_avoid", s.ai_auto_avoid);
    yyjson_mut_obj_add_bool(doc, o, "ai_album_mode", s.ai_album_mode);
    yyjson_mut_obj_add_bool(doc, o, "ai_mixed", s.ai_mixed);
    yyjson_mut_obj_add_int(doc, o, "ai_count", s.ai_count);
    yyjson_mut_obj_add_int(doc, o, "ai_instrumentals", s.ai_instrumentals);
    yyjson_mut_obj_add_int(doc, o, "ai_max_instrumentals", s.ai_max_instrumentals);
    yyjson_mut_obj_add_strcpy(doc, o, "ai_image_engine", s.ai_image_engine.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "ai_image_local", s.ai_image_local.c_str());
    yyjson_mut_obj_add_bool(doc, o, "ai_covers", s.ai_covers);
    yyjson_mut_obj_add_bool(doc, o, "ai_cover_youtube", s.ai_cover_youtube);
    yyjson_mut_obj_add_bool(doc, o, "ai_cover_tiktok", s.ai_cover_tiktok);
    for (const auto & [key, map] : { std::pair<const char *, const std::map<std::string, std::string> *>{ "ai_image_models", &s.ai_image_models },
                                     { "cover_prompts", &s.cover_prompts }, { "album_sounds", &s.album_sounds } }) {
        yyjson_mut_val * obj = yyjson_mut_obj_add_obj(doc, o, key);
        for (auto & [k, val] : *map) yyjson_mut_obj_add(obj, yyjson_mut_strcpy(doc, k.c_str()), yyjson_mut_strcpy(doc, val.c_str()));
    }
    yyjson_mut_obj_add_strcpy(doc, o, "ai_album_title", s.ai_album_title.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "ai_band", s.ai_band.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "ai_negative", s.ai_negative.c_str());
    const std::pair<const char *, const std::map<std::string, std::string> *> ai_maps[] = { { "ai_keys", &s.ai_keys }, { "ai_models", &s.ai_models } };
    for (const auto & [key, map] : ai_maps) {
        yyjson_mut_val * obj = yyjson_mut_obj_add_obj(doc, o, key);
        for (auto & [k, val] : *map) yyjson_mut_obj_add(obj, yyjson_mut_strcpy(doc, k.c_str()), yyjson_mut_strcpy(doc, val.c_str()));
    }
    // 1.0.41 : les clés de SETTINGS_FILES partent dans leur propre fichier, le reste dans reglages.json
    for (const auto & [name, keys] : SETTINGS_FILES) {
        yyjson_mut_val * part = yyjson_mut_obj(doc);
        for (const char * key : keys) {
            yyjson_mut_val * val = yyjson_mut_obj_remove_key(o, key);
            if (val) yyjson_mut_obj_add(part, yyjson_mut_str(doc, key), val);
        }
        size_t len  = 0;
        char * json = yyjson_mut_val_write(part, YYJSON_WRITE_PRETTY, &len);
        if (json) write_text(path.parent_path() / name, std::string(json, len));
        free(json);
    }
    write_text(path, write_json(doc));
}

// 1.0.41 : premier lancement avec config\ : l'ancien donnees\studio_reglages.json (toutes les clés)
// est lu puis réécrit en fichiers séparés ; il est gardé sous donnees\_ancien_studio_reglages.json (jamais effacé).
// Le dernier album de l'IA suit dans config\.
static void migrate_settings(const fs::path & data, const fs::path & config) {
    std::error_code ec;
    fs::path        old = data / "studio_reglages.json", now = config / "reglages.json";
    if (fs::exists(old, ec) && !fs::exists(now, ec)) {
        Settings s;
        if (parse_settings_file(s, old)) {
            save_settings(s, now);
            if (fs::exists(now, ec)) {
                fs::path kept = data / "_ancien_studio_reglages.json";
                for (int n = 2; fs::exists(kept, ec); n++) kept = data / ("_ancien_studio_reglages_" + std::to_string(n) + ".json");
                fs::rename(old, kept, ec);
            }
        }
    }
    if (fs::exists(data / "dernier_album.json", ec) && !fs::exists(config / "dernier_album.json", ec))
        fs::rename(data / "dernier_album.json", config / "dernier_album.json", ec);
}

// ---------------------------------------------------------------------------- Fichiers .yue2pack (1.0.18)
// Un seul format pour sauvegarder, partager et distribuer des styles (préréglages) et des structures :
//   { "format": "yue2pack", "version": 1,
//     "pack": { "name": "Rock 80s", "author": "…", "version": "1.0", "description": "…" },
//     "styles": [ { comme un préréglage de studio_reglages.json } ],
//     "structures": [ { "name": "…", "sections": "Intro,Verse,Chorus,…" } ] }
// Un nom de pack vide = simple sauvegarde : à l'import, les éléments rejoignent les styles et structures personnels.
struct PackFile {
    PackInfo                   info;
    std::vector<Preset>        styles;
    std::vector<UserStructure> structures;
};

static std::string trim(const std::string & s);  // définie plus bas (aides UI)

static bool read_pack(const fs::path & path, PackFile & out, std::string & error) {
    std::string  text = read_text(path);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) {
        error = tr("Fichier illisible ou abîmé.");
        return false;
    }
    yyjson_val * r = yyjson_doc_get_root(doc);
    if (json_str(r, "format") != "yue2pack") {
        yyjson_doc_free(doc);
        error = tr("Ce fichier n'est pas un pack de Studio Audio AI.");
        return false;
    }
    yyjson_val * ver = yyjson_obj_get(r, "version");
    if (yyjson_is_int(ver) && yyjson_get_int(ver) > 1) {
        yyjson_doc_free(doc);
        error = tr("Pack créé par une version plus récente du Studio : mettez le Studio à jour.");
        return false;
    }
    out.info = pack_info_from_json(yyjson_obj_get(r, "pack"));
    out.info.name = trim(out.info.name);
    size_t       idx, max;
    yyjson_val * item;
    yyjson_arr_foreach(yyjson_obj_get(r, "styles"), idx, max, item) {
        Preset p;
        if (preset_from_json(item, p)) {
            p.pack = out.info.name;
            out.styles.push_back(std::move(p));
        }
    }
    yyjson_arr_foreach(yyjson_obj_get(r, "structures"), idx, max, item) {
        UserStructure st;
        if (structure_from_json(item, st)) {
            st.pack = out.info.name;
            out.structures.push_back(std::move(st));
        }
    }
    yyjson_doc_free(doc);
    if (out.styles.empty() && out.structures.empty()) {
        error = tr("Ce pack ne contient aucun style ni aucune structure.");
        return false;
    }
    return true;
}

static bool write_pack(const fs::path & path, const PackFile & pack) {
    yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * o   = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_str(doc, o, "format", "yue2pack");
    yyjson_mut_obj_add_int(doc, o, "version", 1);
    pack_info_to_json(doc, yyjson_mut_obj_add_obj(doc, o, "pack"), pack.info);
    yyjson_mut_val * sa = yyjson_mut_obj_add_arr(doc, o, "styles");
    for (const Preset & p : pack.styles) preset_to_json(doc, sa, p, false);
    yyjson_mut_val * ta = yyjson_mut_obj_add_arr(doc, o, "structures");
    for (const UserStructure & st : pack.structures) structure_to_json(doc, ta, st, false);
    write_text(path, write_json(doc));
    std::error_code ec;
    return fs::exists(path, ec) && fs::file_size(path, ec) > 0;
}

// ---------------------------------------------------------------------------- Aides UI

static ImFont * g_bold   = nullptr;
static ImFont * g_header = nullptr;
static void build_fonts(bool runtime);  // définie avant wWinMain


static std::string fmt_time(double seconds) {
    int  s = (int) std::lround(std::max(0.0, seconds));
    char buf[32];
    if (s >= 60) snprintf(buf, sizeof(buf), tr("%d min %02d s"), s / 60, s % 60);
    else snprintf(buf, sizeof(buf), "%d s", s);
    return buf;
}

static std::string fmt_clock(int ms) {
    int  s = std::max(0, ms / 1000);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

static std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::string lower(std::string s) {
    for (auto & c : s) c = (char) tolower((unsigned char) c);
    return s;
}

static void muted_wrapped(const std::string & text) {
    ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// Texte coloré qui revient à la ligne dans la largeur disponible (1.0.37 : indications jaunes qui dépassaient des cartes).
static void colored_wrapped(const ImVec4 & color, const std::string & text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

static void label(const char * text, const char * hint = nullptr) {
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    if (hint) {
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", hint);
    }
}

// Petite étiquette arrondie colorée (statut, origine du morceau…).
static void pill(const char * text, const ImVec4 & color) {
    ImVec2       size = ImGui::CalcTextSize(text);
    ImVec2       pad(7 * g_dpi, 1 * g_dpi);
    ImVec2       p  = ImGui::GetCursorScreenPos();
    float        h  = ImGui::GetTextLineHeight() + pad.y * 2;
    ImDrawList * dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(size.x + pad.x * 2, h), ImGui::GetColorU32(alpha(color, 0.18f)), h / 2);
    dl->AddText(p + pad, ImGui::GetColorU32(color), text);
    ImGui::Dummy(ImVec2(size.x + pad.x * 2, h));
}

static float pill_width(const char * text) {
    return ImGui::CalcTextSize(text).x + 14 * g_dpi;
}

// Repère des morceaux d'album (1.0.37). 1.0.46 : palette de 24 couleurs bien séparées (teintes espacées de 105°, trois
// réglages de saturation et de luminosité en alternance) ; chaque album présent reçoit une case libre de la palette
// (g_album_slots, attribuée par ancienneté de l'album dans refresh_album_dirs) : deux albums ne partagent une couleur
// qu'au-delà de 24 albums.
static constexpr int                ALBUM_COLORS = 24;
static std::map<std::string, int>   g_album_slots;  // clé d'album -> case de la palette

static ImVec4 album_palette(int i) {
    static const float sv[3][2] = { { 0.62f, 0.96f }, { 0.88f, 0.82f }, { 0.42f, 1.00f } };
    ImVec4             c(0, 0, 0, 1);
    ImGui::ColorConvertHSVtoRGB((float) ((i * 105) % 360) / 360.f, sv[i % 3][0], sv[i % 3][1], c.x, c.y, c.z);
    return c;
}

static ImVec4 album_color(const std::string & key) {
    auto it = g_album_slots.find(key);
    return album_palette(it != g_album_slots.end() ? it->second : (int) (std::hash<std::string>{}(key) % ALBUM_COLORS));
}

// Pouce dessiné en vecteur (aucune police d'emoji nécessaire) : levé et vert pour « validé », baissé
// et rouge pour « à problème ». Éteint (gris estompé) tant qu'il n'est pas actif. Renvoie vrai au clic.
static bool thumb_button(const char * id, bool active, float size, bool down, const char * tip_on,
                         const char * tip_off) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool   clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool   hovered = ImGui::IsItemHovered();
    ImVec4 on      = down ? P.danger : P.ok;
    ImVec4 color   = active ? on : (hovered ? alpha(on, 0.9f) : P.muted);
    float  alpha_f = active ? 1.0f : (hovered ? 0.9f : 0.45f);
    ImU32  fill    = ImGui::GetColorU32(alpha(color, alpha_f));
    ImDrawList * dl = ImGui::GetWindowDrawList();
    float  u = size / 24.f;  // le dessin est conçu dans une grille 24 x 24
    // Le pouce baissé est le même dessin retourné verticalement.
    auto   pt = [&](float x, float y) { return ImVec2(p.x + x * u, p.y + (down ? 24.f - y : y) * u); };
    auto   rect = [&](float x0, float y0, float x1, float y1, float r) {
        ImVec2 a = pt(x0, y0), b = pt(x1, y1);
        dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
                          fill, r * u);
    };
    if (active || hovered) {
        dl->AddCircleFilled(pt(12, 12), 12 * u, ImGui::GetColorU32(alpha(color, active ? 0.16f : 0.10f)));
    }
    rect(3.5f, 11, 8, 21.5f, 1.5f);   // manchette
    rect(9.5f, 10, 20.5f, 21.5f, 3.f);  // main
    rect(10.f, 3.f, 15.f, 13.f, 2.5f);  // pouce
    if (hovered) ImGui::SetTooltip("%s", active ? tip_on : tip_off);
    return clicked;
}

// Styles des marques « validé » / « à problème » (Réglages) : 0 pouces, 1 coche et croix, 2 commutateur.
static const char * MARK_STYLES[] = { N_("Pouces (actuel)"), N_("Coche et croix"), N_("Commutateur À écouter / Validé / Problème") };

// Coche ou croix dessinée dans un cercle : pleine et colorée une fois active. Renvoie vrai au clic.
static bool circle_mark(const char * id, bool active, bool cross, float size, const char * tip_on, const char * tip_off) {
    ImVec2 p       = ImGui::GetCursorScreenPos();
    bool   clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool   hovered = ImGui::IsItemHovered();
    ImVec4 color   = cross ? P.danger : P.ok;
    ImDrawList * dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size / 2, p.y + size / 2);
    float  r = size / 2 - 1 * g_dpi;
    if (active) dl->AddCircleFilled(c, r, ImGui::GetColorU32(alpha(color, 0.92f)));
    else if (hovered) dl->AddCircleFilled(c, r, ImGui::GetColorU32(alpha(color, 0.16f)));
    dl->AddCircle(c, r, ImGui::GetColorU32(active ? color : (hovered ? alpha(color, 0.9f) : P.line)), 0, 1.5f * g_dpi);
    ImU32 glyph = ImGui::GetColorU32(active ? P.accent_ink : (hovered ? color : P.muted));
    float th = std::max(1.6f * g_dpi, size * 0.09f);
    auto  at = [&](float x, float y) { return ImVec2(p.x + x * size, p.y + y * size); };
    if (cross) {
        dl->AddLine(at(0.33f, 0.33f), at(0.67f, 0.67f), glyph, th);
        dl->AddLine(at(0.67f, 0.33f), at(0.33f, 0.67f), glyph, th);
    } else {
        dl->AddLine(at(0.28f, 0.52f), at(0.43f, 0.67f), glyph, th);
        dl->AddLine(at(0.43f, 0.67f), at(0.73f, 0.34f), glyph, th);
    }
    if (hovered) ImGui::SetTooltip("%s", active ? tip_on : tip_off);
    return clicked;
}

// Zone cliquable du titre d'une carte (replier / déplier) : curseur main, infobulle ; renvoie vrai au clic.
static bool title_zone(ImVec2 a, ImVec2 b, const std::string & tip) {
    if (!ImGui::IsMouseHoveringRect(a, b) || !ImGui::IsWindowHovered()) return false;
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s", tip.c_str());
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

static float marks_width(int style, float size) {
    if (style != 2) return size * 2 + 4 * g_dpi;
    float pad = 9 * g_dpi, w = 0;
    for (const char * label : { tr("À écouter"), tr("Validé"), tr("Problème") }) w += ImGui::CalcTextSize(label).x + pad * 2;
    return w;
}

// Dessine les marques d'un morceau terminé au curseur. Renvoie : 0 rien, 1 « validé », 2 « à problème »,
// 3 « à écouter » (retire les deux marques).
static int draw_marks(int style, bool validated, bool flagged, float size) {
    int result = 0;
    if (style == 1) {
        if (circle_mark("##valide", validated, false, size, tr("Morceau validé — cliquer pour retirer la validation"), tr("Cliquer pour valider ce morceau"))) result = 1;
        ImGui::SameLine(0, 4 * g_dpi);
        if (circle_mark("##probleme", flagged, true, size, tr("Morceau à problème — cliquer pour retirer le signalement"), tr("Cliquer pour signaler un problème"))) result = 2;
    } else if (style == 2) {
        const char * labels[] = { tr("À écouter"), tr("Validé"), tr("Problème") };
        const char * tips[]   = { tr("Aucune marque : morceau à écouter"), tr("Morceau validé"), tr("Morceau à problème (à analyser)") };
        const ImVec4 colors[] = { P.accent, P.ok, P.danger };
        int          cur      = flagged ? 2 : validated ? 1 : 0;
        float        pad = 9 * g_dpi, h = size * 0.92f, r = ImGui::GetStyle().FrameRounding * 0.8f;
        float        widths[3], total = 0;
        for (int i = 0; i < 3; i++) {
            widths[i] = ImGui::CalcTextSize(labels[i]).x + pad * 2;
            total += widths[i];
        }
        ImVec2       p0 = ImGui::GetCursorScreenPos();
        ImDrawList * dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p0 + ImVec2(total, h), ImGui::GetColorU32(P.chip), r);
        float x = 0;
        for (int i = 0; i < 3; i++) {
            ImVec2 a = p0 + ImVec2(x, 0), b = a + ImVec2(widths[i], h);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##seg", ImVec2(widths[i], h)) && i != cur) result = i == 0 ? 3 : i;
            ImGui::PopID();
            bool hovered = ImGui::IsItemHovered();
            ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft : i == 2 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
            if (i == cur) dl->AddRectFilled(a, b, ImGui::GetColorU32(alpha(colors[i], 0.30f)), r, corners);
            else if (hovered) dl->AddRectFilled(a, b, ImGui::GetColorU32(alpha(colors[i], 0.12f)), r, corners);
            ImVec2 ts = ImGui::CalcTextSize(labels[i]);
            dl->AddText(ImVec2(a.x + (widths[i] - ts.x) / 2, a.y + (h - ts.y) / 2), ImGui::GetColorU32(i == cur ? colors[i] : P.muted), labels[i]);
            if (hovered) ImGui::SetTooltip("%s", tips[i]);
            x += widths[i];
        }
        dl->AddRect(p0, p0 + ImVec2(total, h), ImGui::GetColorU32(P.line), r);
        ImGui::SetCursorScreenPos(p0);
        ImGui::Dummy(ImVec2(total, h));
    } else {
        if (thumb_button("##valide", validated, size, false, tr("Morceau validé — cliquer pour retirer la validation"), tr("Cliquer pour valider ce morceau"))) result = 1;
        ImGui::SameLine(0, 4 * g_dpi);
        if (thumb_button("##probleme", flagged, size, true, tr("Morceau à problème (à analyser) — cliquer pour retirer le signalement"),
                         tr("Cliquer pour signaler un problème sur ce morceau"))) result = 2;
    }
    return result;
}

static bool accent_button(const char * text, const ImVec2 & size = ImVec2(0, 0), const ImVec4 * color = nullptr) {
    ImVec4 c = color ? *color : P.accent;
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(c, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(c, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text, P.accent_ink);
    bool clicked = ImGui::Button(text, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

static bool soft_button(const char * text, const ImVec4 & color, bool compact = true) {
    ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool clicked = compact ? ImGui::SmallButton(text) : ImGui::Button(text);
    ImGui::PopStyleColor(4);
    return clicked;
}

static bool danger_button(const char * text) {
    return soft_button(text, P.danger);
}

// Blocs du formulaire repliables : l'état (identifiant des blocs repliés) est conservé entre les sessions.
static std::set<std::string> g_collapsed;
static bool                  g_collapsed_dirty = false;

// Bloc du formulaire : fond teinté, bordure et titre à la couleur du bloc. Un clic sur la ligne de titre le replie ou
// le déplie ; replié, il affiche un résumé (summary) à la place de l'indication. Renvoie vrai si le bloc est déplié.
static bool begin_section(const char * id, const char * title, const ImVec4 & color, const char * hint = nullptr,
                          const std::string & summary = std::string()) {
    bool collapsed = g_collapsed.count(id) > 0;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.panel, color, 0.045f));
    ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.35f));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  h = ImGui::GetTextLineHeight();
    float  w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    if (ImGui::InvisibleButton("##en_tete", ImVec2(w, h + 2 * g_dpi))) {
        if (collapsed) g_collapsed.erase(id);
        else g_collapsed.insert(id);
        g_collapsed_dirty = true;
    }
    bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip(collapsed ? tr("Cliquer pour déplier ce bloc") : tr("Cliquer pour replier ce bloc"));
    }
    ImGui::PopID();
    ImGui::GetWindowDrawList()->AddRectFilled(p + ImVec2(0, 2 * g_dpi), p + ImVec2(4 * g_dpi, h - 1 * g_dpi),
                                              ImGui::GetColorU32(color), 2 * g_dpi);
    ImGui::SetCursorScreenPos(p + ImVec2(11 * g_dpi, 0));
    ImGui::PushFont(g_bold);
    ImGui::TextColored(hovered ? alpha(color, 0.8f) : color, "%s %s", collapsed ? "▸" : "▾", title);
    ImGui::PopFont();
    if (collapsed && !summary.empty()) {
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::TextColored(P.muted, "%s", summary.c_str());
    } else if (!collapsed && hint) {
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", hint);
    }
    return !collapsed;
}

static void end_section() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
}

// Menu contextuel du clic droit pour les champs de texte (ImGui n'en fournit pas).
struct TextMenu {
    ImGuiID id = 0;
    int     a = 0, b = 0;  // sélection (octets UTF-8)
};
static TextMenu g_text_menu;

// Menu du champ « graine » (clic droit) : coller une graine copiée dans la bibliothèque, la copier, ou revenir au tirage aléatoire.
static bool seed_context_menu(int64_t & seed) {
    bool changed = false;
    if (ImGui::BeginPopupContextItem("##seed_menu")) {
        const char * clip     = ImGui::GetClipboardText();
        std::string  digits;
        for (const char * c = clip ? clip : ""; *c; c++) {
            if ((*c >= '0' && *c <= '9') || (*c == '-' && digits.empty())) digits += *c;
            else if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '\n') break;
        }
        bool valid = !digits.empty() && digits != "-";
        if (ImGui::MenuItem(tr("Coller"), "Ctrl+V", false, valid)) {
            seed    = strtoll(digits.c_str(), nullptr, 10);
            changed = true;
            ImGui::ClearActiveID();
        }
        if (ImGui::MenuItem(tr("Copier"), "Ctrl+C", false, seed >= 0)) ImGui::SetClipboardText(std::to_string(seed).c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Aléatoire (-1)"))) {
            seed    = -1;
            changed = true;
            ImGui::ClearActiveID();
        }
        ImGui::EndPopup();
    }
    return changed;
}

static bool text_context_menu(std::string & value) {
    ImGuiID id      = ImGui::GetItemID();
    bool    changed = false;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        int                   a  = (int) value.size(), b = a;
        ImGuiInputTextState * st = ImGui::GetInputTextState(id);
        if (st && ImGui::GetActiveID() == id) {
            a = std::clamp(st->GetSelectionStart(), 0, (int) value.size());
            b = std::clamp(st->GetSelectionEnd(), 0, (int) value.size());
            if (a > b) std::swap(a, b);
            if (a == b) a = b = std::clamp(st->GetCursorPos(), 0, (int) value.size());
        }
        g_text_menu = { id, a, b };
        ImGui::OpenPopup("##menu_texte");
    }
    if (g_text_menu.id == id && ImGui::BeginPopup("##menu_texte")) {
        int          a = std::min(g_text_menu.a, (int) value.size()), b = std::min(g_text_menu.b, (int) value.size());
        const char * clip     = ImGui::GetClipboardText();
        bool         has_clip = clip && *clip;
        auto         edit     = [&](const std::string & insert) {
            ImGui::ClearActiveID();  // le champ relira la chaîne modifiée
            value.replace(a, b - a, insert);
            changed = true;
        };
        if (ImGui::MenuItem(tr("Couper"), "Ctrl+X", false, a < b)) {
            ImGui::SetClipboardText(value.substr(a, b - a).c_str());
            edit("");
        }
        if (ImGui::MenuItem(tr("Copier"), "Ctrl+C", false, a < b)) ImGui::SetClipboardText(value.substr(a, b - a).c_str());
        if (ImGui::MenuItem(tr("Coller"), "Ctrl+V", false, has_clip)) edit(clip);
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Copier tout"), nullptr, false, !value.empty())) ImGui::SetClipboardText(value.c_str());
        if (ImGui::MenuItem(tr("Effacer tout"), nullptr, false, !value.empty())) {
            a = 0;
            b = (int) value.size();
            edit("");
        }
        ImGui::EndPopup();
    }
    return changed;
}

// 1.0.46 (demande de Herve) : menu clic droit Couper / Copier / Coller / Copier tout / Effacer tout sur tous les champs
// texte. Mêmes appels que ImGui (std::string), suivis du menu. Avec ImGuiInputTextFlags_EnterReturnsTrue, le résultat
// reste « Entrée appuyée » (un collage par le menu ne valide pas le champ) ; champ en lecture seule : pas de menu.
namespace tx {
static bool finish(bool r, std::string * s, ImGuiInputTextFlags flags) {
    if (flags & ImGuiInputTextFlags_ReadOnly) return r;
    bool m = text_context_menu(*s);
    return (flags & ImGuiInputTextFlags_EnterReturnsTrue) ? r : (r || m);
}
static bool InputText(const char * label, std::string * s, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback cb = nullptr,
                      void * ud = nullptr) {
    return finish(ImGui::InputText(label, s, flags, cb, ud), s, flags);
}
static bool InputTextWithHint(const char * label, const char * hint, std::string * s, ImGuiInputTextFlags flags = 0,
                              ImGuiInputTextCallback cb = nullptr, void * ud = nullptr) {
    return finish(ImGui::InputTextWithHint(label, hint, s, flags, cb, ud), s, flags);
}
static bool InputTextMultiline(const char * label, std::string * s, const ImVec2 & size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0,
                               ImGuiInputTextCallback cb = nullptr, void * ud = nullptr) {
    return finish(ImGui::InputTextMultiline(label, s, size, flags, cb, ud), s, flags);
}
}  // namespace tx

// 1.0.48 : paroles d'un morceau long coupées au début de la section la plus proche de 1/parts des lignes
// (jamais avant la 2e section). Seconde partie vide si les paroles ont moins de deux sections.
static std::pair<std::string, std::string> split_long_lyrics(const std::string & lyrics, size_t parts = 2) {
    std::vector<std::string> lines;
    {
        std::istringstream in(lyrics);
        for (std::string l; std::getline(in, l);) lines.push_back(l);
    }
    std::vector<size_t> starts;  // lignes « [Section] »
    size_t              total = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = trim(lines[i]);
        if (!t.empty() && t.front() == '[') starts.push_back(i);
        total += !t.empty();
    }
    if (starts.size() < 2) return { lyrics, std::string() };
    size_t best = starts[1], seen = 0, best_gap = SIZE_MAX;
    for (size_t i = 0, k = 0; i < lines.size(); i++) {
        if (k < starts.size() && starts[k] == i) {
            size_t gap = seen * parts > total ? seen * parts - total : total - seen * parts;
            // 1.0.49 : coupure au début d'un couplet, d'un interlude ou d'un pont plutôt qu'avant un refrain
            std::string head = lower(trim(lines[i]));
            bool        good = head.find("chorus") == std::string::npos && head.find("refrain") == std::string::npos;
            if (k >= 1 && good && gap < best_gap) {
                best_gap = gap;
                best     = i;
            }
            k++;
        }
        seen += !trim(lines[i]).empty();
    }
    std::string a, b;
    for (size_t i = 0; i < lines.size(); i++) (i < best ? a : b) += lines[i] + "\n";
    return { trim(a), trim(b) };
}

// Bouton « × » qui vide un champ.
static bool clear_button(const char * id, std::string & value) {
    ImGui::PushID(id);
    ImGui::BeginDisabled(value.empty());
    bool clicked = ImGui::Button("×", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tr("Vider ce champ"));
    ImGui::PopID();
    if (clicked) {
        ImGui::ClearActiveID();
        value.clear();
    }
    return clicked;
}

// Champ d'une ligne avec menu contextuel et bouton « × » ; width < 0 = largeur disponible.
static bool input_line(const char * id, const char * hint, std::string & value, float width = -1) {
    float w = (width < 0 ? ImGui::GetContentRegionAvail().x : width) - ImGui::GetFrameHeight() -
              ImGui::GetStyle().ItemSpacing.x * 0.5f;
    ImGui::SetNextItemWidth(w);
    bool changed = ImGui::InputTextWithHint(id, hint, &value);
    changed |= text_context_menu(value);
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    changed |= clear_button(id, value);
    return changed;
}

static std::vector<std::string> split_tags(const std::string & style) {
    return split_style(style);
}

static bool is_bpm_tag(const std::string & tag) {
    return tag.size() > 4 && lower(tag).compare(tag.size() - 3, 3, "bpm") == 0;
}

// Balises d'une catégorie, à sa couleur : présentes en surbrillance, un clic ajoute ou retire.
// Un seul tempo à la fois : choisir un BPM remplace le précédent.
static bool style_chips(const std::vector<const char *> & items, std::string & style, const ImVec4 & color) {
    std::vector<std::string> tags  = split_tags(style);
    ImGuiStyle &             st    = ImGui::GetStyle();
    float                    avail = ImGui::GetContentRegionAvail().x, x = 0;
    bool                     changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    for (size_t i = 0; i < items.size(); i++) {
        auto it = std::find_if(tags.begin(), tags.end(), [&](const std::string & t) { return lower(t) == lower(items[i]); });
        bool        on   = it != tags.end();
        std::string text = (on ? "✓ " : "") + std::string(items[i]);
        float       w    = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
        if (i > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
            ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
            x += st.ItemSpacing.x * 0.6f + w;
        } else {
            x = w;
        }
        ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, on ? 0.55f : 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, on ? 0.65f : 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? P.accent_ink : color);
        ImGui::PushID((int) i);
        if (ImGui::SmallButton(text.c_str())) {
            if (on) {
                tags.erase(it);
            } else {
                if (is_bpm_tag(items[i])) {
                    tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
                }
                tags.push_back(items[i]);
            }
            changed = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar(2);
    if (changed) style = join_style(tags);
    return changed;
}

// Rangée de balises de section ; renvoie l'indice cliqué ou -1.
static int tag_row(const char * const * items, int count, const ImVec4 & color) {
    ImGuiStyle & st      = ImGui::GetStyle();
    float        avail   = ImGui::GetContentRegionAvail().x, x = 0;
    int          clicked = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    for (int i = 0; i < count; i++) {
        float w = ImGui::CalcTextSize(items[i]).x + st.FramePadding.x * 2;
        if (i > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
            ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
            x += st.ItemSpacing.x * 0.6f + w;
        } else {
            x = w;
        }
        ImGui::PushID(i);
        if (soft_button(items[i], color)) clicked = i;
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    return clicked;
}

static int lyrics_callback(ImGuiInputTextCallbackData * data) {
    ((Form *) data->UserData)->cursor = data->CursorPos;
    return 0;
}

static std::wstring save_dialog(HWND owner, const Song & song, const char * ext) {
    std::wstring name           = widen(safe_filename(song.title) + "." + ext);
    wchar_t      file[MAX_PATH] = {};
    wcsncpy_s(file, name.c_str(), _TRUNCATE);
    std::wstring wext   = widen(ext);
    std::wstring filter = widen(std::string(ext) == "flac" ? "FLAC" : std::string(ext) == "wav" ? "WAV" : "MP3");
    filter += L" (*." + wext + L")";
    filter.push_back(L'\0');
    filter += L"*." + wext;
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = wext.c_str();
    std::wstring title = wtr("Exporter le morceau");
    ofn.lpstrTitle  = title.c_str();
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? std::wstring(file) : std::wstring();
}

// ---------------------------------------------------------------------------- Pastilles (zone de notification)

// Icône à côté de l'horloge (1.0.40 : une seule fonction) : pastille violette = nombre de morceaux dans la file
// (en cours + en attente), icône du programme si la file est vide. Clic : ouvrir ; clic droit : menu.
// Les morceaux terminés ont leur propre icône (pastille rouge, voir tray_gauges_update).
static HICON           g_icon = nullptr;
static NOTIFYICONDATAW g_tray{};
static HICON           g_tray_badge = nullptr;
static std::string     g_tray_key;

static HICON render_badge(int count, COLORREF color) {
    using namespace Gdiplus;
    const int    size = 32;
    Bitmap       bitmap(size, size, PixelFormat32bppARGB);
    Graphics     g(&bitmap);
    g.Clear(Color(0, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    SolidBrush   disk(Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    g.FillEllipse(&disk, 0, 0, size - 1, size - 1);
    std::wstring text = count > 99 ? L"99+" : std::to_wstring(count);
    FontFamily   family(L"Segoe UI");
    Font         font(&family, text.size() > 2 ? 12.f : text.size() > 1 ? 15.f : 20.f, FontStyleBold, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    SolidBrush white(Color(255, 255, 255, 255));
    g.DrawString(text.c_str(), -1, &font, RectF(0, 0, (REAL) size, (REAL) size), &format, &white);
    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}

static void tray_gauges_remove();

static void tray_remove() {
    tray_gauges_remove();
    if (g_tray.cbSize) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        g_tray.cbSize = 0;
    }
    if (g_tray_badge) {
        DestroyIcon(g_tray_badge);
        g_tray_badge = nullptr;
    }
    g_tray_key.clear();
}

// 1.0.46 (demande de Herve) : l'icône principale alterne toutes les 2 s entre la file (pastille violette, ou l'icône du
// programme si la file est vide) et les morceaux terminés pas encore écoutés (pastille rouge), s'il y en a.
// La pastille rouge séparée des versions 1.0.40-1.0.45 est retirée (TRAY_SEPARATE_COUNT pour la remettre).
static constexpr bool TRAY_SEPARATE_COUNT = false;

static void tray_update(HWND hwnd, bool enabled, int active, int unheard, const std::string & tooltip) {
    if (!enabled) {
        tray_remove();
        return;
    }
    if (!g_tray.cbSize) {
        g_tray.cbSize           = sizeof(g_tray);
        g_tray.hWnd             = hwnd;
        g_tray.uID              = 1;
        g_tray.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        g_tray.uCallbackMessage = WM_APP_TRAY;
        g_tray.hIcon            = g_icon;
        wcscpy_s(g_tray.szTip, APP_NAME_W);
        Shell_NotifyIconW(NIM_ADD, &g_tray);
        g_tray_key.clear();
    }
    bool        red = unheard > 0 && (GetTickCount64() / 2000) % 2 == 1;
    std::string key = (red ? "r" + std::to_string(unheard) : active > 0 ? "v" + std::to_string(active) : "-") + "|" + tooltip;
    if (key == g_tray_key) return;
    g_tray_key  = key;
    HICON badge = red ? render_badge(unheard, RGB(220, 38, 38)) : active > 0 ? render_badge(active, RGB(124, 92, 255)) : nullptr;
    g_tray.uFlags = NIF_ICON | NIF_TIP;
    g_tray.hIcon  = badge ? badge : g_icon;
    std::wstring tip = widen(tooltip);
    wcsncpy_s(g_tray.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    if (g_tray_badge) DestroyIcon(g_tray_badge);
    g_tray_badge = badge;
}

// ---------------------------------------------------------------------------- Jauges près de l'horloge (application réduite)

// Deux icônes supplémentaires, seulement quand la fenêtre est réduite :
//  - jauges : trois barres verticales, CPU, GPU et mémoire GPU, en dégradé vert (bas) → rouge vif (haut) ;
//  - terminés (1.0.40) : nombre de morceaux terminés non vus, chiffre blanc sur rond rouge ; masquée s'il n'y en a aucun.
// Le survol affiche les valeurs exactes.
static NOTIFYICONDATAW g_tray_gauge{}, g_tray_count{};
static HICON           g_gauge_icon = nullptr, g_count_icon = nullptr;
static std::string     g_gauge_key;

static HICON render_gauges(double cpu, double gpu, double vram) {
    using namespace Gdiplus;
    const int size = 32;
    Bitmap    bitmap(size, size, PixelFormat32bppARGB);
    Graphics  g(&bitmap);
    g.Clear(Color(0, 0, 0, 0));
    SolidBrush back(Color(235, 24, 24, 32));  // fond sombre : lisible sur une barre des tâches claire ou sombre
    g.FillRectangle(&back, 0, 0, size, size);
    const double values[3] = { cpu, gpu, vram };  // de gauche à droite : CPU, GPU, mémoire GPU
    const int    bar_w = 8, gap = 2, left = 3, top = 2, bottom = size - 2;
    // Dégradé vertical commun aux trois barres, comme les jauges de la fenêtre : vert en bas, jaune, orange, rouge vif en haut.
    LinearGradientBrush grad(Point(0, bottom), Point(0, top - 1), Color(255, 34, 197, 94), Color(255, 255, 0, 0));
    Color colors[4]    = { Color(255, 34, 197, 94), Color(255, 234, 210, 8), Color(255, 249, 115, 22), Color(255, 255, 0, 0) };
    REAL  positions[4] = { 0.f, 1.f / 3, 2.f / 3, 1.f };
    grad.SetInterpolationColors(colors, positions, 4);
    for (int i = 0; i < 3; i++) {
        int        x = left + i * (bar_w + gap);
        SolidBrush track(Color(255, 60, 60, 72));
        g.FillRectangle(&track, x, top, bar_w, bottom - top);
        double v = std::clamp(values[i] < 0 ? 0.0 : values[i], 0.0, 100.0);
        int    h = (int) std::lround((bottom - top) * v / 100.0);
        if (h > 0) g.FillRectangle(&grad, x, bottom - h, bar_w, h);
    }
    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}

static void tray_gauges_remove() {
    for (NOTIFYICONDATAW * d : { &g_tray_gauge, &g_tray_count }) {
        if (d->cbSize) {
            Shell_NotifyIconW(NIM_DELETE, d);
            d->cbSize = 0;
        }
    }
    if (g_gauge_icon) DestroyIcon(g_gauge_icon);
    if (g_count_icon) DestroyIcon(g_count_icon);
    g_gauge_icon = g_count_icon = nullptr;
    g_gauge_key.clear();
}

static void tray_gauges_update(HWND hwnd, bool show, const GpuMem & m, int finished, const std::string & tip_gauges,
                               const std::string & tip_counts) {
    if (!show) {
        tray_gauges_remove();
        return;
    }
    double vram = m.ok && m.total ? 100.0 * (double) m.used / (double) m.total : -1;
    // Arrondi à 5 % : l'icône n'est redessinée que si quelque chose change vraiment.
    auto   step = [](double v) { return v < 0 ? -1 : (int) std::lround(v / 5) * 5; };
    std::string key = std::to_string(step(m.cpu)) + "|" + std::to_string(step(m.gpu)) + "|" + std::to_string(step(vram)) + "|" +
                      std::to_string(finished) + "|" + tip_gauges + tip_counts;
    if (key == g_gauge_key && g_tray_gauge.cbSize) return;
    g_gauge_key      = key;
    HICON gauge_icon = render_gauges(m.cpu, m.gpu, vram);
    HICON count_icon = TRAY_SEPARATE_COUNT && finished > 0 ? render_badge(finished, RGB(220, 38, 38)) : nullptr;
    if (!count_icon && g_tray_count.cbSize) {  // aucun morceau terminé : pastille rouge retirée
        Shell_NotifyIconW(NIM_DELETE, &g_tray_count);
        g_tray_count.cbSize = 0;
    }
    struct Item { NOTIFYICONDATAW * d; UINT id; HICON icon; const std::string * tip; };
    for (const Item & it : { Item{ &g_tray_gauge, 2, gauge_icon, &tip_gauges }, Item{ &g_tray_count, 3, count_icon, &tip_counts } }) {
        if (!it.icon) continue;
        bool add = !it.d->cbSize;
        if (add) {
            it.d->cbSize           = sizeof(NOTIFYICONDATAW);
            it.d->hWnd             = hwnd;
            it.d->uID              = it.id;
            it.d->uCallbackMessage = WM_APP_TRAY;  // clic : comme l'icône principale
        }
        it.d->uFlags = NIF_ICON | NIF_TIP | (add ? NIF_MESSAGE : 0);
        it.d->hIcon  = it.icon;
        std::wstring tip = widen(*it.tip);
        wcsncpy_s(it.d->szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, it.d);
    }
    if (g_gauge_icon) DestroyIcon(g_gauge_icon);
    if (g_count_icon) DestroyIcon(g_count_icon);
    g_gauge_icon = gauge_icon;
    g_count_icon = count_icon;
}

// ---------------------------------------------------------------------------- Application

// ---------------------------------------------------------------------------- Jeux, pilote, miniatures

// Jeu en cours : plein écran exclusif / présentation (détecté par Windows) ou l'un des programmes listés.
static bool detect_game(const std::string & list, std::string & which) {
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE)) {
        which = tr("application en plein écran");
        return true;
    }
    std::vector<std::string> names;
    for (auto & n : split_style(list)) names.push_back(lower(n));
    if (names.empty()) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{ sizeof(entry) };
    bool            found = false;
    for (BOOL ok = Process32FirstW(snap, &entry); ok && !found; ok = Process32NextW(snap, &entry)) {
        std::string exe = lower(narrow(entry.szExeFile));
        for (auto & n : names) {
            if (exe == n || exe == n + ".exe") {
                found = true;
                which = exe;
            }
        }
    }
    CloseHandle(snap);
    return found;
}

// Version d'AMD Software : Adrenalin Edition installée (« 25.10.45.05 »), lue dans le registre ; vide si pas de pilote AMD.
static std::string amd_adrenalin_version() {
    wchar_t buf[128] = {};
    DWORD   size     = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\AMD\\CN", L"CNVersion", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) != ERROR_SUCCESS) {
        return "";
    }
    return narrow(buf);
}

// Résultat de la recherche d'un pilote plus récent (lancée au démarrage, dans un thread).
struct DriverUpdate {
    std::atomic<int> state{ 0 };  // 0 pas de pilote AMD / pas encore testé, 1 recherche, 2 à jour, 3 mise à jour disponible, 4 hors ligne
    std::string      installed, latest;  // « 25.10 », « 26.8.1 » : lus seulement quand state est 2 ou 3
};

// Dernier AMD Software : Adrenalin Edition stable, d'après les notes de version publiées sur GitHub (miroir non officiel des
// notes AMD : AMD ne propose pas de service de consultation de sa dernière version). On compare année et mois.
static void check_driver_update(std::shared_ptr<DriverUpdate> up, std::string installed_cn) {
    int a = 0, b = 0;
    if (sscanf(installed_cn.c_str(), "%d.%d", &a, &b) != 2 || a <= 0) return;
    up->state = 1;
    std::string body;
    if (!http_get_text(L"https://api.github.com/repos/nunodxxd/AMD-Software-Adrenalin/releases?per_page=20", body)) {
        up->state = 4;
        return;
    }
    yyjson_doc * doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        up->state = 4;
        return;
    }
    int         la = 0, lb = 0, lc = 0;
    std::string latest;
    size_t      i, n;
    yyjson_val * rel;
    yyjson_arr_foreach(yyjson_doc_get_root(doc), i, n, rel) {
        yyjson_val * tag = yyjson_obj_get(rel, "tag_name");
        yyjson_val * pre = yyjson_obj_get(rel, "prerelease");
        yyjson_val * drf = yyjson_obj_get(rel, "draft");
        if (!yyjson_is_str(tag) || (yyjson_is_bool(pre) && yyjson_get_bool(pre)) || (yyjson_is_bool(drf) && yyjson_get_bool(drf))) continue;
        int x = 0, y = 0, z = 0;
        char tail = 0;
        if (sscanf(yyjson_get_str(tag), "%d.%d.%d%c", &x, &y, &z, &tail) == 3) {  // « 26.8.1 » : sans suffixe « -beta »
            if (x * 100 + y > la * 100 + lb || latest.empty()) {
                la = x; lb = y; lc = z;
                latest = yyjson_get_str(tag);
            }
        }
    }
    yyjson_doc_free(doc);
    (void) lc;
    if (latest.empty()) {
        up->state = 4;
        return;
    }
    char inst[32];
    snprintf(inst, sizeof(inst), "%d.%d", a, b);
    up->installed = inst;
    up->latest    = latest;
    up->state     = (la * 100 + lb > a * 100 + b) ? 3 : 2;
}

// Modèles principaux proposés dans les Réglages (fichiers de Serveurperso/YuE2-GGUF).
static const char * MODEL_FILES_KNOWN[] = { "YuE2-3B-Q8_0.gguf", "YuE2-3B-Q6_K.gguf", "YuE2-3B-Q5_K_M.gguf" };
static const char * MODEL_BASE_URL      = "https://huggingface.co/Serveurperso/YuE2-GGUF/resolve/main/";

// Adresse « dernière version » d'un fichier de LoRA du catalogue : la révision figée (/resolve/<sha>/) devient « main ».
static std::string latest_url(const std::string & pinned) {
    size_t a = pinned.find("/resolve/");
    if (a == std::string::npos) return pinned;
    a += 9;
    size_t b = pinned.find('/', a);
    if (b == std::string::npos) return pinned;
    return pinned.substr(0, a) + "main" + pinned.substr(b);
}

// Tailles des fichiers à jour sur Hugging Face, relevées au démarrage (clé : nom du fichier local).
struct RemoteSizes {
    std::mutex                                 mutex;
    std::map<std::string, unsigned long long>  size;
    std::atomic<int>                           state{ 0 };  // 0 pas lancé, 1 en cours, 2 terminé
    unsigned long long get(const std::string & file) {
        std::lock_guard<std::mutex> lock(mutex);
        auto                        it = size.find(file);
        return it == size.end() ? 0 : it->second;
    }
};

// Analyse d'un fichier audio en tâche de fond (étiquetage + partition) et suggestions de balises.
struct AnalysisJob {
    std::atomic<int>   state{ 0 };  // 0 en cours, 1 terminée, 2 erreur
    std::atomic<float> progress{ 0 };
    std::mutex         mutex;
    std::string        stage;
    std::atomic<bool>  cancel{ false };
    fs::path           file;
    bool               with_score = false;  // partition transcrite (tempo, tonalité, structure)
    bool               use_structure = true;
    bool               with_lyrics = false;  // paroles transcrites (Whisper)
    bool               use_lyrics  = true;
    bool               search_web  = false;  // chercher les paroles sur Internet (réglage « lyrics_web »)
    AudioAnalysis      result;
    SongIdentity       identity;             // groupe et titre devinés du fichier
    WebLyrics          web;                  // paroles trouvées sur Internet (LRCLIB)
    std::string        web_text;             // paroles Internet au format YuE2 (vide si non retenues)
    std::vector<Suggestion> sugg;
    std::string        get_stage() {
        std::lock_guard<std::mutex> lock(mutex);
        return stage;
    }
};

static void check_remote_sizes(std::shared_ptr<RemoteSizes> rs, std::vector<std::pair<std::string, std::string>> items) {
    rs->state = 1;
    for (const auto & [file, url] : items) {
        unsigned long long n = http_remote_size(widen(url));
        if (n > 0) {
            std::lock_guard<std::mutex> lock(rs->mutex);
            rs->size[file] = n;
        }
    }
    rs->state = 2;
}

static std::string gpu_driver_version() {
    std::string out;
    IDXGIDevice * dev = nullptr;
    if (g_device && SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&dev)))) {
        IDXGIAdapter * ad = nullptr;
        if (SUCCEEDED(dev->GetAdapter(&ad))) {
            LARGE_INTEGER v{};
            if (SUCCEEDED(ad->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v))) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%u.%u.%u.%u", HIWORD(v.HighPart), LOWORD(v.HighPart), HIWORD(v.LowPart),
                         LOWORD(v.LowPart));
                out = buf;
            }
            ad->Release();
        }
        dev->Release();
    }
    return out;
}

// Miniatures des pochettes (chargées avec GDI+, mises en cache par chemin et date).
struct Thumb {
    ID3D11ShaderResourceView * srv = nullptr;
    ImVec2                     size{ 0, 0 };
};
static std::map<std::wstring, Thumb> g_thumbs;

static bool thumb_texture(const fs::path & file, ImTextureID & tex, ImVec2 & size, int max_px) {
    std::error_code ec;
    auto            stamp = fs::last_write_time(file, ec).time_since_epoch().count();
    std::wstring    key   = file.wstring() + L"|" + std::to_wstring(stamp) + L"|" + std::to_wstring(max_px);
    auto            it    = g_thumbs.find(key);
    if (it == g_thumbs.end()) {
        Thumb t;
        Gdiplus::Bitmap src(file.c_str());
        if (src.GetLastStatus() == Gdiplus::Ok && src.GetWidth() > 0 && src.GetHeight() > 0) {
            float k  = std::min(1.f, (float) max_px / (float) std::max(src.GetWidth(), src.GetHeight()));
            int   nw = std::max(1, (int) std::lround(src.GetWidth() * k)), nh = std::max(1, (int) std::lround(src.GetHeight() * k));
            Gdiplus::Bitmap dst(nw, nh, PixelFormat32bppARGB);
            {
                Gdiplus::Graphics g(&dst);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(&src, 0, 0, nw, nh);
            }
            Gdiplus::BitmapData data;
            Gdiplus::Rect       rect(0, 0, nw, nh);
            if (dst.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width            = nw;
                desc.Height           = nh;
                desc.MipLevels        = 1;
                desc.ArraySize        = 1;
                desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage            = D3D11_USAGE_DEFAULT;
                desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{ data.Scan0, (UINT) data.Stride, 0 };
                ID3D11Texture2D *      texture = nullptr;
                if (SUCCEEDED(g_device->CreateTexture2D(&desc, &init, &texture))) {
                    g_device->CreateShaderResourceView(texture, nullptr, &t.srv);
                    texture->Release();
                    t.size = ImVec2((float) nw, (float) nh);
                }
                dst.UnlockBits(&data);
            }
        }
        it = g_thumbs.emplace(key, t).first;
    }
    if (!it->second.srv) return false;
    tex  = (ImTextureID) (intptr_t) it->second.srv;
    size = it->second.size;
    return true;
}

static void release_thumbs() {
    for (auto & [k, t] : g_thumbs) {
        if (t.srv) t.srv->Release();
    }
    g_thumbs.clear();
}

static const char * SORTS[] = { N_("Plus récents"), N_("Plus anciens"), N_("Titre A → Z"), N_("N° de piste"), N_("Durée (longs d'abord)"),
                                N_("Validés d'abord") };

// Messages d'erreur de translate.cpp : répétés ici pour que i18n_sync.py les trouve (il ne lit pas translate.cpp) ;
// poll_translation les affiche avec tr().
[[maybe_unused]] static const char * const TRANSLATE_ERRORS[] = {
    N_("Fichier temporaire impossible à écrire."), N_("Traduction annulée."), N_("Le moteur de traduction local n'a pas pu démarrer."),
    N_("Le modèle local n'a pas renvoyé de traduction exploitable."), N_("Langue inconnue."), N_("Aucune parole à traduire."),
    N_("Pas de modèle de traduction local (Réglages › Analyse et paroles)."),
    N_("Pas de connexion à Internet et pas de modèle de traduction local (Réglages › Analyse et paroles)."),
    N_("Pas de connexion à Internet : cette langue n'est disponible qu'avec la traduction par Internet."),
};

struct App {
    HWND        hwnd   = nullptr;
    Studio *    studio = nullptr;
    Player      player;
    Form        form;
    Settings    settings;
    fs::path    draft_path, settings_path;
    std::string toast;
    double      toast_until = 0, last_draft_save = 0;
    bool        scroll_form_top = false, any_active = false;
    std::map<std::string, bool> watched;  // générations locales suivies jusqu'à leur fin
    int         unseen_ready = 0;         // morceaux terminés depuis la dernière consultation
    std::string drag_id;
    int         drag_ms = 0;
    // Filtres de la bibliothèque
    std::string filter_text, filter_style;
    int         filter_type = 0, filter_duration = 0;
    // Fenêtres modales
    std::string pending_delete;
    // Balise rapide négative à supprimer (après confirmation) ; owner : 0 formulaire, 1 Réglages
    std::string neg_pending_delete;
    int         neg_delete_owner = 0;
    std::string neg_new_tag;  // champ « Ajouter » des Réglages
    // Suppressions à confirmer dans les Réglages : un modèle, des fichiers de LoRA, toute la bibliothèque
    std::string              pending_model_delete;
    std::vector<std::string> pending_lora_delete;
    std::string              pending_lora_name;
    bool                     pending_delete_all = false;
    bool                     pending_analysis_delete = false;
    // Analyse audio (modèles ONNX dans models/analysis)
    bool                          analysis_present = false, whisper_present = false, pending_whisper_delete = false;
    std::shared_ptr<AnalysisJob>  analysis;
    fs::path                      analysis_auto_file;  // fichier déjà proposé à l'analyse automatique
    // Changement de langue demandé dans les Réglages : appliqué entre deux images (les polices sont reconstruites).
    std::string pending_language;
    void apply_language() {
        std::string code = pending_language;
        pending_language.clear();
        if (!i18n::set_language(code)) return;
        settings.ui_language = code;
        save_settings(settings, settings_path);
        build_fonts(true);
    }
    std::string extend_id, extend_title, extend_lyrics;
    int         extend_seconds = 60;
    bool        open_extend = false, open_preset = false, open_edit = false;
    std::string rerender_id;  // morceau pour lequel on choisit la graine du re-rendu
    bool        open_rerender = false;
    std::string preset_name;
    std::string edit_id, edit_title, edit_artist, edit_album, edit_track;
    std::string style_view;  // style affiché avec retours à la ligne
    // Tâches de fond (exports, vidéos), albums, réglages
    std::vector<std::shared_ptr<Job>> jobs;
    bool        open_albums = false, open_settings = false, open_video = false, albums_drawn = false;
    // Fenêtre « Créer des vidéos » : un morceau ou un album, une image par réseau
    std::vector<Song> video_songs;

    // ------------------------------------------------------------------ LoRA

    Downloader                                downloader;
    std::vector<std::shared_ptr<DownloadJob>> tracked_downloads;
    std::vector<DownloadJob *>                announced_downloads;
    std::map<std::string, unsigned long long> lora_files;  // fichiers de models/adapters et leur taille
    double                                    lora_scan_at = -10;

    // Fiche d'une LoRA personnelle (hors catalogue), lue (1.0.24) :
    //  1. dans un fichier .json du même nom posé à côté (« acdc.json » pour « acdc.safetensors »), écrit par la Forge
    //     ou à la main : { "name", "description", "author", "kind" (style, artist, instrument, slider), "trigger",
    //     "tags", "songs", "clips", "date" } — tous facultatifs ;
    //  2. à défaut, dans l'en-tête du fichier .safetensors (« __metadata__ » : trigger_word, base_model, source_steps,
    //     source_rank, source_clip_seconds… que la Forge y écrit).
    struct CustomLora {
        unsigned long long size = 0;  // taille du fichier lu (relu s'il change)
        long long          json_time = 0;  // date de la fiche .json (relue si la Forge la modifie, 1.0.25)
        std::string        name, description, author, kind, trigger, tags, date, details;
        std::string        sheet_trigger;  // mot de la fiche .json écarté car différent du mot appris (en-tête), 1.0.25
        int                songs = 0, clips = 0;
        bool               forge = false;  // créée par la Forge (métadonnées « source_* »)
    };
    std::map<std::string, CustomLora> custom_loras;  // nom du fichier → fiche

    static CustomLora read_custom_lora(const fs::path & dir, const std::string & file, unsigned long long size) {
        CustomLora c;
        c.size = size;
        // En-tête safetensors : 8 octets (taille, petit-boutiste) puis un objet JSON ; on n'en garde que « __metadata__ ».
        if (std::FILE * f = _wfopen((dir / widen(file)).c_str(), L"rb")) {
            unsigned char n8[8] = {};
            if (fread(n8, 1, 8, f) == 8) {
                unsigned long long n = 0;
                for (int i = 7; i >= 0; i--) n = (n << 8) | n8[i];
                if (n > 0 && n < 16ull * 1024 * 1024) {
                    std::string head((size_t) n, '\0');
                    if (fread(head.data(), 1, head.size(), f) == head.size()) {
                        if (yyjson_doc * doc = yyjson_read(head.data(), head.size(), 0)) {
                            yyjson_val * m = yyjson_obj_get(yyjson_doc_get_root(doc), "__metadata__");
                            c.trigger           = json_str(m, "trigger_word");
                            std::string rank = json_str(m, "source_rank"), steps = json_str(m, "source_steps"),
                                        clip = json_str(m, "source_clip_seconds"), base = json_str(m, "base_model");
                            c.forge = !steps.empty() || !rank.empty();
                            std::vector<std::string> parts;
                            if (!rank.empty()) parts.push_back(trf("rang %s", rank.c_str()));
                            if (!steps.empty()) parts.push_back(trf("%s pas", steps.c_str()));
                            if (!clip.empty()) parts.push_back(trf("extraits de %s s", clip.c_str()));
                            if (!base.empty()) parts.push_back(base);
                            for (size_t i = 0; i < parts.size(); i++) c.details += (i ? " · " : "") + parts[i];
                            yyjson_doc_free(doc);
                        }
                    }
                }
            }
            fclose(f);
        }
        // Fichier .json du même nom : il complète ou remplace ce que dit l'en-tête.
        std::string stem = file;
        if (stem.size() > 12 && stem.compare(stem.size() - 12, 12, ".safetensors") == 0) stem.resize(stem.size() - 12);
        std::string text = read_text(dir / widen(stem + ".json"));
        c.json_time      = json_stamp(dir, file);
        if (yyjson_doc * doc = yyjson_read(text.data(), text.size(), 0)) {
            yyjson_val * r = yyjson_doc_get_root(doc);
            auto take = [&](const char * key, std::string & dst) {
                std::string v = json_str(r, key);
                if (!v.empty()) dst = v;
            };
            take("name", c.name);
            take("description", c.description);
            take("author", c.author);
            take("kind", c.kind);
            // Mot déclencheur : celui que la LoRA a appris (« trigger_word » de l'en-tête) l'emporte (1.0.25).
            // Changer le mot dans la fiche sans réentraîner ne change pas ce que le modèle a appris.
            std::string learned = c.trigger;
            take("trigger", c.trigger);
            if (!learned.empty() && c.trigger != learned) {
                c.sheet_trigger = c.trigger;
                c.trigger       = learned;
            }
            take("date", c.date);
            yyjson_val * tags = yyjson_obj_get(r, "tags");
            if (yyjson_is_str(tags)) c.tags = yyjson_get_str(tags);
            size_t       i, n;
            yyjson_val * t;
            yyjson_arr_foreach(tags, i, n, t) if (yyjson_is_str(t)) c.tags += (c.tags.empty() ? "" : ", ") + std::string(yyjson_get_str(t));
            yyjson_val * v = yyjson_obj_get(r, "songs");
            if (yyjson_is_num(v)) c.songs = (int) yyjson_get_num(v);
            v = yyjson_obj_get(r, "clips");
            if (yyjson_is_num(v)) c.clips = (int) yyjson_get_num(v);
            yyjson_doc_free(doc);
        }
        return c;
    }

    // Date de modification de la fiche « <nom>.json » d'une LoRA (0 si elle n'existe pas).
    static long long json_stamp(const fs::path & dir, const std::string & file) {
        std::string stem = file;
        if (stem.size() > 12 && stem.compare(stem.size() - 12, 12, ".safetensors") == 0) stem.resize(stem.size() - 12);
        std::error_code ec;
        auto            t = fs::last_write_time(dir / widen(stem + ".json"), ec);
        return ec ? 0 : (long long) t.time_since_epoch().count();
    }

    // Fiche d'une LoRA personnelle (nullptr pour une LoRA du catalogue ou un fichier inconnu).
    const CustomLora * custom_lora(const std::string & file) const {
        auto it = custom_loras.find(file);
        return it == custom_loras.end() ? nullptr : &it->second;
    }
    // Nom affiché : celui de la fiche d'une LoRA personnelle s'il existe, sinon le nom habituel.
    std::string custom_label(const std::string & file) const {
        const CustomLora * c = custom_lora(file);
        return c && !c->name.empty() ? c->name : lora_label(file);
    }
    // Type d'une LoRA personnelle (colonne « Type », pastille du formulaire).
    static const char * custom_kind(const CustomLora * c) {
        std::string k = c ? c->kind : std::string();
        return k == "style" ? tr("Style") : k == "artist" ? tr("Artiste") : k == "slider" ? tr("Curseur") : k == "instrument" ? tr("Instrum.") : tr("Perso");
    }
    // Description affichée d'une LoRA personnelle.
    static std::string custom_description(const CustomLora & c) {
        if (!c.description.empty()) return c.description;
        return c.forge ? tr("LoRA créée par la Forge.") : tr("LoRA personnelle déposée dans modeles\\lora.");
    }
    // Mot déclencheur d'une LoRA (catalogue ou fiche personnelle) ; vide s'il n'y en a pas.
    std::string lora_trigger(const std::string & file) const {
        LoraRef r = lora_ref(file);
        if (r.entry) return r.entry->trigger;
        const CustomLora * c = custom_lora(file);
        return c ? c->trigger : std::string();
    }
    static constexpr const char *             INSTRUMENTAL_FILE = "ar_lora_inst_v3abc.bf16.safetensors";

    void scan_loras(bool force = false) {
        double now = ImGui::GetTime();
        if (!force && now - lora_scan_at < 2.0) return;
        lora_scan_at = now;
        lora_files.clear();
        std::error_code ec;
        for (const auto & e : fs::directory_iterator(studio->paths().adapters, ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == ".safetensors") {
                lora_files[narrow(e.path().filename().wstring())] = (unsigned long long) e.file_size(ec);
            }
        }
        // Fiches des LoRA personnelles : lues une fois, relues si le fichier change de taille, si sa fiche .json
        // change (la Forge peut modifier le nom ou le mot déclencheur, 1.0.25) ou si on force.
        const fs::path & dir = studio->paths().adapters;
        for (auto it = custom_loras.begin(); it != custom_loras.end();) it = lora_files.count(it->first) ? std::next(it) : custom_loras.erase(it);
        for (const auto & [file, size] : lora_files) {
            if (lora_ref(file).entry) continue;
            auto it = custom_loras.find(file);
            if (!force && it != custom_loras.end() && it->second.size == size && it->second.json_time == json_stamp(dir, file)) continue;
            std::string old_word = it != custom_loras.end() ? it->second.trigger : std::string();
            custom_loras[file]   = read_custom_lora(dir, file, size);
            // Mot déclencheur changé alors que la LoRA est cochée : l'ancien mot quitte le style, le nouveau y entre.
            const LoraUse * u = nullptr;
            for (const LoraUse & o : form.loras) if (o.file == file) u = &o;
            if (u && u->enabled && old_word != custom_loras[file].trigger) {
                style_trigger(old_word, false);
                style_trigger(custom_loras[file].trigger, true);
            }
        }
        // Premier balayage : les LoRA restées cochées depuis la dernière séance reçoivent leur mot dans le style.
        if (!triggers_synced) {
            triggers_synced = true;
            sync_lora_triggers();
        }
    }

    struct LoraRef {
        const LoraEntry * entry = nullptr;
        const LoraFile *  file  = nullptr;
    };
    static LoraRef lora_ref(const std::string & file) {
        for (const LoraEntry & e : LORA_CATALOG) {
            for (int i = 0; i < e.file_count; i++) {
                if (file == e.files[i].file) return { &e, &e.files[i] };
            }
        }
        return {};
    }
    static std::string lora_label(const std::string & file) {
        LoraRef r = lora_ref(file);
        if (!r.entry) {
            std::string s = file;
            for (const char * ext : { ".bf16.safetensors", ".safetensors" }) {
                size_t n = strlen(ext);
                if (s.size() > n && s.compare(s.size() - n, n, ext) == 0) {
                    s.resize(s.size() - n);
                    break;
                }
            }
            return s;
        }
        std::string n = tr(r.entry->name);
        if (r.file->half[0]) n += std::string(" (") + r.file->half + ")";
        return n;
    }
    bool lora_installed(const LoraEntry & e) const {
        for (int i = 0; i < e.file_count; i++) {
            if (!lora_files.count(e.files[i].file)) return false;
        }
        return true;
    }
    static std::string fmt_mb(unsigned long long bytes) {
        char buf[32];
        if (bytes >= 1000ull * 1000 * 1000) snprintf(buf, sizeof(buf), tr("%.2f Go"), bytes / 1e9);
        else snprintf(buf, sizeof(buf), tr("%.0f Mo"), bytes / 1e6);
        for (char & c : buf) if (c == '.') c = ',';
        return buf;
    }

    // Mot déclencheur d'une LoRA : ajouté au début du style quand elle est cochée, retiré sinon.
    void style_trigger(const std::string & word, bool present) {
        if (word.empty()) return;
        std::vector<std::string> tags = split_style(form.style);
        auto same = [&](const std::string & t) {
            return t.size() == word.size() && std::equal(t.begin(), t.end(), word.begin(), [](char a, char b) { return tolower((unsigned char) a) == tolower((unsigned char) b); });
        };
        auto it = std::find_if(tags.begin(), tags.end(), same);
        if (present && it == tags.end()) tags.insert(tags.begin(), word);
        else if (!present && it != tags.end()) tags.erase(it);
        else return;
        form.style = join_style(tags);
        form.dirty = true;
    }

    bool triggers_synced = false;  // mots des LoRA cochées remis dans le style au premier balayage (1.0.25)

    // Met dans le style le mot déclencheur de chaque LoRA cochée et installée (1.0.25) : après un préréglage,
    // la reprise d'un morceau, ou juste avant une génération si le mot a été effacé du champ Style.
    // Une LoRA sans mot déclencheur n'a rien à ajouter : elle est appliquée à la génération quand même.
    void sync_lora_triggers() {
        for (const LoraUse & u : form.loras) {
            if (u.enabled && lora_files.count(u.file)) style_trigger(lora_trigger(u.file), true);
        }
    }

    LoraUse * find_lora_use(const std::string & file) {
        for (LoraUse & u : form.loras) if (u.file == file) return &u;
        return nullptr;
    }

    void set_lora(const std::string & file, bool on) {
        LoraRef   r = lora_ref(file);
        LoraUse * u = find_lora_use(file);
        if (!u && on) {
            form.loras.push_back({ file, r.file ? r.file->scale : 1.f, true });
        } else if (u) {
            u->enabled = on;
        }
        std::string word = lora_trigger(file);  // catalogue, ou fiche d'une LoRA personnelle (1.0.24)
        if (!word.empty()) {
            bool any = false;
            for (const LoraUse & o : form.loras) any |= o.enabled && lora_trigger(o.file) == word;
            style_trigger(word, any);
        }
        form.dirty = true;
    }

    // Durée du fondu « sur la structure » : celle de la dernière section (outro, couplet…), de 5 à 30 s.
    static double structure_fade_seconds(const std::string & lyrics, bool instrumental) {
        std::string tags = structure_tags(lyrics), last = tags.substr(tags.rfind(',') == std::string::npos ? 0 : tags.rfind(',') + 1);
        for (auto & c : last) c = (char) tolower((unsigned char) c);
        double seconds;
        if (instrumental) {
            seconds = last.rfind("intro", 0) == 0 ? 15 : last.rfind("outro", 0) == 0 ? 20 : last.rfind("pre", 0) == 0 ? 15
                    : last.rfind("bridge", 0) == 0 ? 25 : 30;
        } else {
            seconds = last.rfind("outro", 0) == 0 || last.rfind("interlude", 0) == 0 ? 15 : 12;
        }
        return std::clamp(seconds, 5.0, 30.0);
    }

    // Un morceau terminé, neuf et ni validé ni signalé : en attente d'écoute et de vérification.
    static bool is_new(const Song & s) { return s.status == "done" && !s.heard && !s.validated && !s.flagged; }
    void        mark_heard(const Song & s) {
        if (!s.heard) studio->set_heard(s.id, true);
    }

    // Applique le résultat de draw_marks : 1 validé, 2 à problème, 3 retire les marques. Avec le commutateur,
    // un clic choisit l'état affiché (pas de bascule) ; avec les boutons, un second clic retire la marque.
    void apply_mark(const Song & s, int code) {
        if (code != 0) mark_heard(s);  // l'avoir évalué vaut vérification
        if (code == 1) studio->set_validated(s.id, settings.mark_style == 2 ? true : !s.validated);
        else if (code == 2) studio->set_flagged(s.id, settings.mark_style == 2 ? true : !s.flagged);
        else if (code == 3) {
            if (s.validated) studio->set_validated(s.id, false);
            if (s.flagged) studio->set_flagged(s.id, false);
        }
    }

    // Les LoRA cochées et installées d'un nouveau morceau.
    std::vector<std::pair<std::string, double>> chosen_loras() const {
        std::vector<std::pair<std::string, double>> out;
        for (const LoraUse & u : form.loras) {
            if (u.enabled && lora_files.count(u.file)) out.push_back({ u.file, u.scale });
        }
        return out;
    }

    bool preset_with_loras = true, preset_with_structure = true, preset_with_negative = true;  // cases de la fenêtre d'enregistrement d'un préréglage

    // ------------------------------------------------------------------ Structures personnelles et packs (1.0.18)
    bool        open_structure = false;           // fenêtre « Enregistrer la structure »
    std::string structure_name, structure_to_save;
    int         pending_structure_delete = -1;    // structure à supprimer (confirmation)
    std::string pending_pack_delete;              // pack à supprimer (confirmation)
    bool        open_export = false;              // fenêtre « Exporter des styles et structures »
    std::vector<char> export_styles, export_structures;  // éléments cochés pour l'export
    PackInfo    export_info;
    bool        settings_shown = false;           // Réglages affichés à cette image : les fenêtres des packs s'ouvrent au-dessus
    std::vector<fs::path> import_queue;           // packs à importer (bouton « Importer… » ou fichiers déposés)
    // Import en cours. Choix par élément : 0 nouveau, 1 remplacer, 2 garder les deux, 3 ignorer, -1 = à demander.
    struct ImportJob {
        PackFile         pack;
        std::vector<int> style_choice, structure_choice;
        bool             same_for_all = false;
        bool             pack_installed = false;  // un pack du même nom est déjà installé : question posée pour tout le pack
    };
    std::unique_ptr<ImportJob> import_job;

    // Groupes d'une liste : "" (éléments personnels) d'abord s'il y en a, puis chaque pack dans l'ordre d'installation.
    std::vector<std::string> pack_groups(bool styles) const {
        std::vector<std::string> g;
        bool                     own = false;
        auto add = [&](const std::string & pack) {
            if (pack.empty()) own = true;
            else if (std::find(g.begin(), g.end(), pack) == g.end()) g.push_back(pack);
        };
        if (styles) for (const Preset & p : settings.presets) add(p.pack);
        else for (const UserStructure & st : settings.structures) add(st.pack);
        if (own) g.insert(g.begin(), std::string());
        return g;
    }

    int find_preset(const std::string & name, const std::string & pack) const {
        for (size_t i = 0; i < settings.presets.size(); i++) if (settings.presets[i].name == name && settings.presets[i].pack == pack) return (int) i;
        return -1;
    }
    int find_structure(const std::string & name, const std::string & pack) const {
        for (size_t i = 0; i < settings.structures.size(); i++) if (settings.structures[i].name == name && settings.structures[i].pack == pack) return (int) i;
        return -1;
    }
    // Nom libre dans un groupe : « Nom », sinon « Nom (2) », « Nom (3) »…
    template <class T> static std::string free_name(const std::vector<T> & list, const std::string & name, const std::string & pack) {
        auto taken = [&](const std::string & n) {
            for (const T & x : list) if (x.name == n && x.pack == pack) return true;
            return false;
        };
        if (!taken(name)) return name;
        for (int k = 2;; k++) {
            std::string n = name + " (" + std::to_string(k) + ")";
            if (!taken(n)) return n;
        }
    }

    fs::path ask_pack() {
        wchar_t       file[MAX_PATH] = {};
        OPENFILENAMEW ofn{ sizeof(ofn) };
        ofn.hwndOwner       = hwnd;
        std::wstring filter = file_filter(wtr("Packs Studio Audio AI"), L"*.yue2pack");
        ofn.lpstrFilter     = filter.c_str();
        ofn.lpstrFile       = file;
        ofn.nMaxFile        = MAX_PATH;
        std::wstring title  = wtr("Importer des styles et structures");
        ofn.lpstrTitle      = title.c_str();
        ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
    }

    // Lit un pack ; les éléments qui existent déjà (même nom, même groupe) seront tranchés un par un dans une fenêtre.
    void import_pack(const fs::path & path) {
        auto        job = std::make_unique<ImportJob>();
        std::string error;
        if (!read_pack(path, job->pack, error)) {
            notify(trf("Import impossible (%s) : %s", narrow(path.filename().wstring()).c_str(), error.c_str()));
            return;
        }
        // Pack nommé déjà installé : on ne compare pas élément par élément, on demande pour le pack entier.
        bool installed = false;
        for (const PackInfo & info : settings.packs) installed |= !job->pack.info.name.empty() && info.name == job->pack.info.name;
        job->pack_installed = installed;
        for (const Preset & p : job->pack.styles) job->style_choice.push_back(!installed && find_preset(p.name, p.pack) >= 0 ? -1 : 0);
        for (const UserStructure & st : job->pack.structures) job->structure_choice.push_back(!installed && find_structure(st.name, st.pack) >= 0 ? -1 : 0);
        import_job = std::move(job);
        if (!import_job->pack_installed && !import_has_conflict()) finish_import();
    }

    // Nom libre pour un pack : « Rock 80s », sinon « Rock 80s (2) », « Rock 80s (3) »…
    std::string free_pack_name(const std::string & name) const {
        auto taken = [&](const std::string & n) {
            for (const PackInfo & info : settings.packs) if (info.name == n) return true;
            return false;
        };
        if (!taken(name)) return name;
        for (int k = 2;; k++) {
            std::string n = name + " (" + std::to_string(k) + ")";
            if (!taken(n)) return n;
        }
    }

    bool import_has_conflict() const {
        if (!import_job) return false;
        for (int c : import_job->style_choice) if (c < 0) return true;
        for (int c : import_job->structure_choice) if (c < 0) return true;
        return false;
    }

    // Ajoute les éléments selon les choix, mémorise le pack, signale les LoRA manquantes.
    void finish_import() {
        ImportJob &           j       = *import_job;
        int                   n_style = 0, n_struct = 0;
        std::set<std::string> missing;
        scan_loras(true);
        for (size_t i = 0; i < j.pack.styles.size(); i++) {
            int c = j.style_choice[i];
            if (c == 3) continue;
            Preset np = j.pack.styles[i];
            int    k  = find_preset(np.name, np.pack);
            if (c == 1 && k >= 0) settings.presets[k] = np;
            else {
                if (k >= 0) np.name = free_name(settings.presets, np.name, np.pack);
                settings.presets.push_back(np);
            }
            n_style++;
            for (const LoraUse & u : np.loras) if (!lora_files.count(u.file)) missing.insert(lora_label(u.file));
        }
        for (size_t i = 0; i < j.pack.structures.size(); i++) {
            int c = j.structure_choice[i];
            if (c == 3) continue;
            UserStructure st = j.pack.structures[i];
            int           k  = find_structure(st.name, st.pack);
            if (c == 1 && k >= 0) settings.structures[k] = st;
            else {
                if (k >= 0) st.name = free_name(settings.structures, st.name, st.pack);
                settings.structures.push_back(st);
            }
            n_struct++;
        }
        if (!j.pack.info.name.empty()) {  // pack nommé : ses informations sont gardées (mise à jour si déjà installé)
            bool found = false;
            for (PackInfo & info : settings.packs) {
                if (info.name == j.pack.info.name) {
                    info  = j.pack.info;
                    found = true;
                }
            }
            if (!found) settings.packs.push_back(j.pack.info);
        }
        save_settings(settings, settings_path);
        std::string what = trf("%d style(s) et %d structure(s) importé(s)", n_style, n_struct);
        notify(j.pack.info.name.empty() ? what + "." : trf("Pack « %s » : %s.", j.pack.info.name.c_str(), what.c_str()));
        if (!missing.empty()) {
            std::string names;
            for (const std::string & m : missing) names += (names.empty() ? "" : ", ") + m;
            notify(trf("Certains styles utilisent des LoRA non installées : %s (à télécharger dans Réglages › LoRA).", names.c_str()));
        }
        import_job.reset();
    }

    void delete_pack(const std::string & name, bool quiet = false) {
        std::erase_if(settings.presets, [&](const Preset & p) { return p.pack == name; });
        std::erase_if(settings.structures, [&](const UserStructure & st) { return st.pack == name; });
        std::erase_if(settings.packs, [&](const PackInfo & info) { return info.name == name; });
        save_settings(settings, settings_path);
        if (!quiet) notify(trf("Pack « %s » supprimé.", name.c_str()));
    }

    void begin_export() {
        export_styles.assign(settings.presets.size(), 1);
        export_structures.assign(settings.structures.size(), 1);
        open_export = true;
    }

    // Écrit les éléments cochés dans un fichier .yue2pack choisi par l'utilisateur ; vrai si le fichier est écrit.
    bool do_export() {
        PackFile pack;
        pack.info             = export_info;
        pack.info.name        = trim(pack.info.name);
        pack.info.author      = trim(pack.info.author);
        pack.info.version     = trim(pack.info.version);
        pack.info.description = trim(pack.info.description);
        for (size_t i = 0; i < settings.presets.size() && i < export_styles.size(); i++) if (export_styles[i]) pack.styles.push_back(settings.presets[i]);
        for (size_t i = 0; i < settings.structures.size() && i < export_structures.size(); i++) if (export_structures[i]) pack.structures.push_back(settings.structures[i]);
        std::wstring name = widen(safe_filename(pack.info.name.empty() ? std::string(tr("Mes styles")) : pack.info.name) + ".yue2pack");
        std::wstring label = wtr("Pack Studio Audio AI");
        fs::path     path = ask_save(name, label.c_str(), L"yue2pack");
        if (path.empty()) return false;
        if (!write_pack(path, pack)) {
            notify(tr("Écriture du fichier impossible."));
            return false;
        }
        notify(trf("%d style(s) et %d structure(s) exporté(s) dans %s.", (int) pack.styles.size(), (int) pack.structures.size(),
                   narrow(path.filename().wstring()).c_str()));
        return true;
    }

    // Carte de Réglages › Génération : export, import, packs installés.
    void draw_packs_settings() {
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.green, "%s", tr("Styles, structures et packs"));
        ImGui::PopFont();
        int own_styles = 0, own_structs = 0;
        for (const Preset & p : settings.presets) own_styles += p.pack.empty();
        for (const UserStructure & st : settings.structures) own_structs += st.pack.empty();
        muted_wrapped(trf("Vos styles (préréglages) : %d · vos structures : %d · packs installés : %d.", own_styles, own_structs, (int) settings.packs.size()));
        muted_wrapped(tr("Un fichier .yue2pack regroupe des styles et des structures : pour les sauvegarder, les partager ou installer un pack. Vous pouvez aussi glisser un fichier .yue2pack sur la fenêtre du Studio."));
        ImGui::BeginDisabled(settings.presets.empty() && settings.structures.empty());
        if (accent_button(tr("Exporter…"))) begin_export();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Importer…"))) {
            fs::path path = ask_pack();
            if (!path.empty()) import_queue.push_back(path);
        }
        if (settings.packs.empty()) return;
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (ImGui::BeginTable("##packs", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("Pack"), 0, 2.f);
            ImGui::TableSetupColumn(tr("Auteur"), 0, 1.4f);
            ImGui::TableSetupColumn(tr("Version"), 0, 0.7f);
            ImGui::TableSetupColumn(tr("Contenu"), 0, 1.6f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(tr("Supprimer")).x + 20 * g_dpi);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < settings.packs.size(); i++) {
                const PackInfo & info = settings.packs[i];
                int n_style = 0, n_struct = 0;
                for (const Preset & p : settings.presets) n_style += p.pack == info.name;
                for (const UserStructure & st : settings.structures) n_struct += st.pack == info.name;
                ImGui::PushID((int) i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(info.name.c_str());
                if (!info.description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.description.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(P.muted, "%s", info.author.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(P.muted, "%s", info.version.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(P.muted, "%s", trf("%d style(s), %d structure(s)", n_style, n_struct).c_str());
                ImGui::TableNextColumn();
                if (soft_button(tr("Supprimer"), P.danger)) pending_pack_delete = info.name;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // Fenêtres liées aux packs : export, éléments déjà présents à l'import, suppression d'un pack.
    // Dessinées au-dessus des Réglages s'ils sont ouverts, sinon au niveau principal (fichier déposé sur la fenêtre).
    void draw_pack_modals() {
        // ---- Export
        if (open_export) {
            ImGui::OpenPopup(tr("Exporter des styles et structures"));
            open_export = false;
        }
        ImGui::SetNextWindowSize(ImVec2(620 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Exporter des styles et structures"), nullptr, ImGuiWindowFlags_NoResize)) {
            muted_wrapped(tr("Cochez les éléments à mettre dans le fichier."));
            ImGui::BeginChild("##export_list", ImVec2(0, 240 * g_dpi), ImGuiChildFlags_Borders);
            auto list = [&](auto & items, std::vector<char> & checked, const char * title, bool structures) {
                if (items.empty()) return;
                ImGui::SeparatorText(title);
                for (size_t i = 0; i < items.size() && i < checked.size(); i++) {
                    const auto & it    = items[i];
                    std::string  label = it.pack.empty() ? it.name : it.pack + " › " + it.name;
                    bool         b     = checked[i] != 0;
                    ImGui::PushID((int) i);
                    if (ImGui::Checkbox(label.c_str(), &b)) checked[i] = b;
                    ImGui::PopID();
                    if (structures && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", arrow_text(settings.structures[i].sections).c_str());
                }
            };
            ImGui::PushID("styles");
            list(settings.presets, export_styles, tr("Styles"), false);
            ImGui::PopID();
            ImGui::PushID("structures");
            list(settings.structures, export_structures, tr("Structures"), true);
            ImGui::PopID();
            ImGui::EndChild();
            if (ImGui::SmallButton(tr("Tout cocher"))) {
                std::fill(export_styles.begin(), export_styles.end(), 1);
                std::fill(export_structures.begin(), export_structures.end(), 1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Tout décocher"))) {
                std::fill(export_styles.begin(), export_styles.end(), 0);
                std::fill(export_structures.begin(), export_structures.end(), 0);
            }
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
            ImGui::TextColored(P.muted, "%s", tr("Pack (facultatif : laissez le nom vide pour une simple sauvegarde)"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            tx::InputTextWithHint("##pack_name", tr("Nom du pack (ex. : Rock 80s)"), &export_info.name);
            float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(half);
            tx::InputTextWithHint("##pack_author", tr("Auteur"), &export_info.author);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            tx::InputTextWithHint("##pack_version", tr("Version (ex. : 1.0)"), &export_info.version);
            ImGui::SetNextItemWidth(-FLT_MIN);
            tx::InputTextWithHint("##pack_desc", tr("Description"), &export_info.description);
            int n = 0;
            for (char c : export_styles) n += c != 0;
            for (char c : export_structures) n += c != 0;
            if (modal_buttons(tr("Exporter…"), n > 0) && do_export()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- Import : pack du même nom déjà installé
        const char * installed_title = tr("Pack déjà installé");
        if (import_job && import_job->pack_installed && !ImGui::IsPopupOpen(installed_title)) ImGui::OpenPopup(installed_title);
        ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
        if (ImGui::BeginPopupModal(installed_title, nullptr, ImGuiWindowFlags_NoResize)) {
            if (!import_job || !import_job->pack_installed) {
                ImGui::CloseCurrentPopup();
            } else {
                const std::string & name    = import_job->pack.info.name;
                std::string         renamed = free_pack_name(name);
                ImGui::TextWrapped("%s", trf("Le pack « %s » est déjà installé. Remplacer : l'ancien est retiré puis le nouveau est importé. Garder les deux : le nouveau est ajouté sous le nom « %s ».",
                                             name.c_str(), renamed.c_str()).c_str());
                ImGui::Dummy(ImVec2(0, 6 * g_dpi));
                if (accent_button(tr("Remplacer le pack"))) {
                    delete_pack(name, true);
                    import_job->pack_installed = false;
                    finish_import();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Garder les deux"))) {
                    // Le pack importé prend un nom libre : ses éléments sont rangés à part et il a sa propre ligne
                    import_job->pack.info.name = renamed;
                    for (Preset & p : import_job->pack.styles) p.pack = renamed;
                    for (UserStructure & st : import_job->pack.structures) st.pack = renamed;
                    import_job->pack_installed = false;
                    finish_import();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (soft_button(tr("Annuler l'import"), P.danger, false)) {
                    import_job.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        // ---- Import : élément déjà présent
        const char * conflict_title = tr("Élément déjà présent");
        if (import_has_conflict() && !import_job->pack_installed && !ImGui::IsPopupOpen(conflict_title)) ImGui::OpenPopup(conflict_title);
        ImGui::SetNextWindowSize(ImVec2(500 * g_dpi, 0));
        if (ImGui::BeginPopupModal(conflict_title, nullptr, ImGuiWindowFlags_NoResize)) {
            // Premier élément à trancher : style, puis structure
            int * choice = nullptr;
            std::string text;
            if (import_job) {
                for (size_t i = 0; i < import_job->style_choice.size() && !choice; i++) {
                    if (import_job->style_choice[i] >= 0) continue;
                    choice                = &import_job->style_choice[i];
                    const Preset & p      = import_job->pack.styles[i];
                    text = p.pack.empty() ? trf("Le style « %s » existe déjà dans vos styles.", p.name.c_str())
                                          : trf("Le style « %s » existe déjà dans le pack « %s ».", p.name.c_str(), p.pack.c_str());
                }
                for (size_t i = 0; i < import_job->structure_choice.size() && !choice; i++) {
                    if (import_job->structure_choice[i] >= 0) continue;
                    choice                  = &import_job->structure_choice[i];
                    const UserStructure & st = import_job->pack.structures[i];
                    text = st.pack.empty() ? trf("La structure « %s » existe déjà dans vos structures.", st.name.c_str())
                                           : trf("La structure « %s » existe déjà dans le pack « %s ».", st.name.c_str(), st.pack.c_str());
                }
            }
            if (!choice) {
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::Checkbox(tr("Faire de même pour les éléments suivants"), &import_job->same_for_all);
                ImGui::Dummy(ImVec2(0, 6 * g_dpi));
                int picked = -1;
                if (accent_button(tr("Remplacer"))) picked = 1;
                ImGui::SameLine();
                if (ImGui::Button(tr("Garder les deux"))) picked = 2;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("L'élément importé est ajouté avec un numéro : « Nom (2) »."));
                ImGui::SameLine();
                if (ImGui::Button(tr("Ignorer"))) picked = 3;
                ImGui::SameLine();
                if (soft_button(tr("Annuler l'import"), P.danger, false)) {
                    import_job.reset();
                    ImGui::CloseCurrentPopup();
                } else if (picked > 0) {
                    if (import_job->same_for_all) {
                        for (int & c : import_job->style_choice) if (c < 0) c = picked;
                        for (int & c : import_job->structure_choice) if (c < 0) c = picked;
                    } else {
                        *choice = picked;
                    }
                    if (!import_has_conflict()) {
                        finish_import();
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndPopup();
        }

        // ---- Suppression d'un pack
        const char * delete_title = tr("Supprimer le pack ?");
        if (!pending_pack_delete.empty() && !ImGui::IsPopupOpen(delete_title)) ImGui::OpenPopup(delete_title);
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(delete_title, nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", trf("Les styles et structures du pack « %s » seront retirés du Studio. Vous pourrez le réimporter depuis son fichier .yue2pack.",
                                         pending_pack_delete.c_str()).c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_pack_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                delete_pack(pending_pack_delete);
                pending_pack_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // Fenêtres « Enregistrer la structure » et « Supprimer la structure ? » (ouvertes depuis le bloc Paroles).
    void draw_structure_modals() {
        if (open_structure) {
            ImGui::OpenPopup(tr("Enregistrer la structure"));
            open_structure = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Enregistrer la structure"), nullptr, ImGuiWindowFlags_NoResize)) {
            muted_wrapped(arrow_text(structure_to_save));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool enter = ImGui::InputTextWithHint("##structure_name", tr("Nom de la structure (ex. : Pop courte avec pont)"), &structure_name,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            text_context_menu(structure_name);
            std::string name     = trim(structure_name);
            int         existing = find_structure(name, "");
            if (existing >= 0) colored_wrapped(P.warn, tr("Une de vos structures porte déjà ce nom : elle sera remplacée."));
            muted_wrapped(tr("Elle apparaîtra dans la liste des structures, rubrique « Mes structures », pour une chanson comme pour un instrumental."));
            bool valid = !name.empty();
            if (modal_buttons(tr("Enregistrer"), valid) || (enter && valid)) {
                UserStructure st{ name, structure_to_save, "" };
                if (existing >= 0) settings.structures[existing] = st;
                else settings.structures.push_back(st);
                save_settings(settings, settings_path);
                notify(trf("Structure « %s » enregistrée.", name.c_str()));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const char * delete_title = tr("Supprimer la structure ?");
        if (pending_structure_delete >= (int) settings.structures.size()) pending_structure_delete = -1;
        if (pending_structure_delete >= 0 && !ImGui::IsPopupOpen(delete_title)) ImGui::OpenPopup(delete_title);
        ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
        if (ImGui::BeginPopupModal(delete_title, nullptr, ImGuiWindowFlags_NoResize)) {
            if (pending_structure_delete < 0) {
                ImGui::CloseCurrentPopup();
            } else {
                const UserStructure & st = settings.structures[pending_structure_delete];
                ImGui::TextWrapped("%s", trf("La structure « %s » sera supprimée de la liste.", st.name.c_str()).c_str());
                muted_wrapped(arrow_text(st.sections));
                ImGui::Dummy(ImVec2(0, 6 * g_dpi));
                if (ImGui::Button(tr("Annuler"))) {
                    pending_structure_delete = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                    std::string name = st.name;
                    settings.structures.erase(settings.structures.begin() + pending_structure_delete);
                    pending_structure_delete = -1;
                    save_settings(settings, settings_path);
                    notify(trf("Structure « %s » supprimée.", name.c_str()));
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // Packs à importer (bouton « Importer… » ou fichiers déposés sur la fenêtre), un à la fois.
    void update_imports() {
        for (const std::wstring & w : g_dropped) {
            fs::path path(w);
            if (lower(narrow(path.extension().wstring())) == ".yue2pack") import_queue.push_back(path);
            else notify(tr("Seuls les packs .yue2pack peuvent être déposés sur la fenêtre."));
        }
        g_dropped.clear();
        if (!import_job && !import_queue.empty()) {
            fs::path next = import_queue.front();
            import_queue.erase(import_queue.begin());
            import_pack(next);
        }
    }

    // Résumé d'un préréglage (infobulle) : style, LoRA avec leur force, structures.
    std::string preset_summary(const Preset & p) const {
        std::string s = p.style;
        if (!p.loras.empty()) {
            s += tr("\nLoRA :");
            for (const LoraUse & u : p.loras) {
                char sc[16];
                snprintf(sc, sizeof(sc), "%.2g", u.scale);
                for (char & c : sc) if (c == '.') c = ',';
                s += " " + lora_label(u.file) + " ×" + sc + (&u == &p.loras.back() ? "" : ",");
            }
        }
        auto arrow = [](std::string t) {
            std::string out;
            for (char c : t) out += c == ',' ? std::string(" > ") : std::string(1, c);
            return out;
        };
        if (!p.structure_song.empty()) s += tr("\nStructure (chanson) : ") + arrow(p.structure_song);
        if (!p.structure_inst.empty()) s += tr("\nStructure (instrumental) : ") + arrow(p.structure_inst);
        if (p.has_negative && !p.negative_style.empty()) {
            char sc[16];
            snprintf(sc, sizeof(sc), "%.1f", p.negative_scale);
            s += "\n" + trf("À éviter : %s (force %s)", p.negative_style.c_str(), sc);
        }
        return s;
    }

    // Applique un préréglage : style, LoRA (si le préréglage en contient) et structure des paroles du mode actuel.
    void apply_preset(const Preset & p) {
        Form & f = form;
        f.style  = p.style;
        f.dirty  = true;
        if (p.has_negative) {  // préréglage récent : il mémorise le style à éviter (vide = le champ est vidé)
            f.negative_style = p.negative_style;
            f.negative_scale = p.negative_scale;
        }
        if (!p.loras.empty()) {
            scan_loras(true);
            f.loras.clear();
            std::string missing;
            for (const LoraUse & u : p.loras) {
                if (lora_files.count(u.file)) f.loras.push_back(u);
                else missing += (missing.empty() ? "" : ", ") + lora_label(u.file);
            }
            if (!missing.empty()) notify(trf("LoRA du préréglage non installée(s) : %s (à télécharger dans Réglages).", missing.c_str()));
        }
        sync_lora_triggers();  // le style du préréglage remplace l'ancien : on y remet les mots des LoRA cochées
        const std::string & st = f.instrumental ? p.structure_inst : p.structure_song;
        if (!st.empty()) {
            std::string   text   = skeleton_text(st, f.instrumental);
            std::string & target = f.instrumental ? f.lyrics_inst : f.lyrics_song;
            if (f.instrumental || !has_lyrics_text(target)) {
                target = text;
            } else {  // des paroles seraient perdues : confirmation
                f.pending_structure = true;
                f.pending_text      = text;
                f.pending_name      = p.name + tr(" (préréglage)");
            }
        }
        ImGui::ClearActiveID();
    }

    bool downloads_running() const {
        for (const auto & j : tracked_downloads) if (j->state == 0) return true;
        return false;
    }

    // ---- Mises à jour des modèles et des LoRA : taille locale différente de celle du dépôt.
    unsigned long long model_local_size(const std::string & file) const {
        std::error_code ec;
        auto            n = fs::file_size(studio->paths().gguf_dir / widen(file), ec);
        return ec ? 0 : (unsigned long long) n;
    }
    bool model_update_available(const std::string & file) {
        unsigned long long r = remote->get(file), l = model_local_size(file);
        return r > 0 && l > 0 && r != l;
    }
    bool lora_update_available(const LoraEntry & e) {
        if (!lora_installed(e)) return false;
        for (int i = 0; i < e.file_count; i++) {
            unsigned long long r = remote->get(e.files[i].file);
            auto               it = lora_files.find(e.files[i].file);
            if (r > 0 && it != lora_files.end() && it->second != r) return true;
        }
        return false;
    }
    // Lance, en tâche de fond, la lecture des tailles à jour des modèles et LoRA installés.
    void start_update_check() {
        scan_loras(true);
        std::vector<std::pair<std::string, std::string>> items;
        for (const char * f : MODEL_FILES_KNOWN) {
            if (model_local_size(f) > 0) items.push_back({ f, std::string(MODEL_BASE_URL) + f });
        }
        for (const LoraEntry & e : LORA_CATALOG) {
            if (!lora_installed(e)) continue;
            for (int i = 0; i < e.file_count; i++) items.push_back({ e.files[i].file, latest_url(e.files[i].url) });
        }
        refresh_analysis();
        if (analysis_present) {
            for (const AnalysisFile & f : analysis_files()) items.push_back({ f.name, f.url });
        }
        refresh_whisper();
        if (whisper_present) items.push_back({ whisper_file().name, whisper_file().url });
        if (!items.empty()) std::thread(check_remote_sizes, remote, items).detach();
    }
    void start_model_update(const std::string & file) {
        std::vector<DlFile> files = { { std::string(MODEL_BASE_URL) + file, file, remote->get(file) } };
        auto job = downloader.start("model:" + file, model_label(file), studio->paths().gguf_dir, files);
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(trf("Mise à jour de « %s » lancée.", model_label(file).c_str()));
    }
    void start_lora_update(const LoraEntry & e) {
        std::vector<DlFile> files;
        for (int i = 0; i < e.file_count; i++) files.push_back({ latest_url(e.files[i].url), e.files[i].file, remote->get(e.files[i].file) });
        auto job = downloader.start(e.id, tr(e.name), studio->paths().adapters, files);
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(trf("Mise à jour de « %s » lancée.", tr(e.name)));
    }

    // Annonce la fin des téléchargements et relit le dossier des LoRA.
    void update_downloads() {
        if (remote->state == 2 && !updates_announced) {
            updates_announced = true;
            int n = 0;
            for (const char * f : MODEL_FILES_KNOWN) n += model_update_available(f);
            for (const LoraEntry & e : LORA_CATALOG) n += lora_update_available(e);
            n += analysis_update_count() > 0;
            n += whisper_update_available();
            if (n > 0) notify(trf("%d mise(s) à jour de modèles ou de LoRA disponible(s) : voir Réglages.", n));
        }
        for (const auto & j : tracked_downloads) {
            if (j->state == 0) continue;
            if (std::find(announced_downloads.begin(), announced_downloads.end(), j.get()) != announced_downloads.end()) continue;
            announced_downloads.push_back(j.get());
            if (j->key == "whisper") {
                refresh_whisper();
                if (j->state == 1) notify(tr("Modèle de transcription des paroles prêt."));
                else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
                continue;
            }
            if (j->key == "analysis") {
                refresh_analysis();
                if (j->state == 1) notify(tr("Modèle d'analyse audio prêt."));
                else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
                continue;
            }
            if (finish_ai_download(*j)) continue;  // modèles IA et llama.cpp (1.0.26)
            if (j->key.rfind("model:", 0) == 0) {
                models = studio->paths().list_models();
                if (j->state == 1) notify(trf("Modèle « %s » mis à jour : il sera utilisé pour les prochains morceaux.", j->label.c_str()));
                else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
                continue;
            }
            if (j->state == 1) notify(trf("LoRA « %s » téléchargée : elle est disponible dans le formulaire.", j->label.c_str()));
            else if (j->state == 2) notify(trf("Téléchargement de « %s » impossible : %s", j->label.c_str(), j->error.c_str()));
            scan_loras(true);
        }
    }

    void start_lora_download(const LoraEntry & e) {
        std::vector<DlFile> files;
        for (int i = 0; i < e.file_count; i++) files.push_back({ e.files[i].url, e.files[i].file, e.files[i].bytes });
        fs::create_directories(studio->paths().adapters);
        auto job = downloader.start(e.id, tr(e.name), studio->paths().adapters, files);
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(trf("Téléchargement de la LoRA « %s » lancé.", tr(e.name)));
    }

    // ---- Analyse audio : modèles d'étiquetage (téléchargement, mises à jour, suppression)
    fs::path analysis_dir() const { return studio->paths().models_dir / "analyse"; }  // 1.0.42 : modeles\analyse
    void     refresh_analysis() { analysis_present = analysis_installed(analysis_dir()); }
    unsigned long long analysis_local_size(const AnalysisFile & f) const {
        std::error_code ec;
        auto            n = fs::file_size(analysis_dir() / widen(f.name), ec);
        return ec ? 0 : (unsigned long long) n;
    }
    unsigned long long analysis_total_bytes() const {
        unsigned long long n = 0;
        for (const AnalysisFile & f : analysis_files()) n += f.bytes;
        return n;
    }
    int analysis_update_count() {
        int n = 0;
        if (!analysis_present) return 0;
        for (const AnalysisFile & f : analysis_files()) {
            unsigned long long r = remote->get(f.name), l = analysis_local_size(f);
            n += r > 0 && l > 0 && r != l;
        }
        return n;
    }
    void start_analysis_download(bool update) {
        std::vector<DlFile> files;
        for (const AnalysisFile & f : analysis_files()) {
            unsigned long long r = update ? remote->get(f.name) : 0;
            files.push_back({ f.url, f.name, r > 0 ? r : f.bytes });
        }
        std::error_code ec;
        fs::create_directories(analysis_dir(), ec);
        auto job = downloader.start("analysis", tr("Analyse audio"), analysis_dir(), files);
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(update ? tr("Mise à jour du modèle d'analyse audio lancée.") : tr("Téléchargement du modèle d'analyse audio lancé."));
    }
    void check_analysis_updates() {
        std::vector<std::pair<std::string, std::string>> items;
        for (const AnalysisFile & f : analysis_files()) items.push_back({ f.name, f.url });
        std::thread(check_remote_sizes, remote, items).detach();
    }
    void delete_analysis_files() {
        std::error_code ec;
        int             n = 0;
        for (const AnalysisFile & f : analysis_files()) n += remove_inside(analysis_dir() / widen(f.name), ec) ? 1 : 0;
        if (analysis) analysis->cancel = true;
        analysis.reset();
        refresh_analysis();
        notify(trf("Modèle d'analyse audio supprimé (%d fichiers).", n));
    }

    // ---- Transcription des paroles : modèle Whisper (fichier unique dans models/whisper)
    fs::path whisper_dir() const { return studio->paths().models_dir / "voix"; }  // 1.0.42 : modeles\voix (Whisper)
    unsigned long long whisper_local_size() const {
        std::error_code ec;
        auto            n = fs::file_size(whisper_dir() / widen(whisper_file().name), ec);
        return ec ? 0 : (unsigned long long) n;
    }
    void refresh_whisper() { whisper_present = whisper_local_size() > 0 && fs::exists(studio->paths().whisper); }
    bool whisper_update_available() {
        unsigned long long r = remote->get(whisper_file().name), l = whisper_local_size();
        return r > 0 && l > 0 && r != l;
    }
    void start_whisper_download(bool update) {
        const AnalysisFile & wf = whisper_file();
        unsigned long long   r  = update ? remote->get(wf.name) : 0;
        std::vector<DlFile>  files = { { wf.url, wf.name, r > 0 ? r : wf.bytes } };
        std::error_code      ec;
        fs::create_directories(whisper_dir(), ec);
        auto job = downloader.start("whisper", tr("Transcription des paroles"), whisper_dir(), files);
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(update ? tr("Mise à jour du modèle de transcription des paroles lancée.") : tr("Téléchargement du modèle de transcription des paroles lancé."));
    }
    void delete_whisper_file() {
        std::error_code ec;
        remove_inside(whisper_dir() / widen(whisper_file().name), ec);
        refresh_whisper();
        if (remove_refused) return;
        notify(tr("Modèle de transcription des paroles supprimé."));
    }

    // ---- Langue chantée des morceaux de la bibliothèque, vérifiée à l'écoute (Whisper) à la demande
    std::atomic<bool> lang_running{ false }, lang_stop{ false };
    std::atomic<int>  lang_checked{ 0 }, lang_fixed{ 0 }, lang_total{ 0 }, lang_done_flag{ 0 };  // done_flag : 1 terminé, 2 interrompu

    // Raison pour laquelle la carte graphique n'est pas libre (texte vide : libre). Un morceau en pause dans la file ne gêne pas.
    std::string busy_reason() {
        if (game_active) return tr("un jeu est en cours");
        if (studio->gate_closed()) return tr("la file de génération est suspendue");
        if (analysis && analysis->state == 0) return tr("une analyse audio est en cours");
        if (downloads_running()) return tr("un téléchargement est en cours");
        for (const Song & s : studio->snapshot()) if (s.status == "running" || s.waiting()) return tr("une génération est en cours ou en attente");
        return std::string();
    }
    bool app_busy() { return !busy_reason().empty(); }
    int language_check_pending() {
        int n = 0;
        for (const Song & s : studio->snapshot()) {
            n += s.status == "done" && s.lyrics_lang_check == 0;  // chantés (même sans texte écrit) et instrumentaux (des paroles s'y glissent parfois)
        }
        return n;
    }
    // Appelée à chaque image : annonce la fin et interrompt si la carte graphique est demandée par une génération.
    void poll_language_check() {
        if (int flag = lang_done_flag.exchange(0)) {
            int n = lang_checked, fixed = lang_fixed;
            lang_checked = lang_fixed = 0;
            if (flag == 2) notify(tr("Vérification de la langue interrompue : une génération a démarré."));
            else notify(trf("Langue des paroles vérifiée à l'écoute : %d morceau(x), %d corrigé(s).", n, fixed));
        }
        if (lang_running && studio->local_active()) lang_stop = true;
    }
    void start_language_check() {
        if (lang_running || !whisper_present) return;
        std::vector<Song> todo;
        for (const Song & s : studio->snapshot()) {
            if (s.status != "done" || s.lyrics_lang_check > 0) continue;
            todo.push_back(s);
        }
        if (todo.empty()) {
            notify(tr("Tous les morceaux chantés ont déjà été vérifiés."));
            return;
        }
        lang_total   = (int) todo.size();
        lang_checked = lang_fixed = 0;
        lang_stop    = false;
        lang_running = true;
        Studio * st  = studio;
        Paths    pa  = studio->paths();
        std::thread([this, st, pa, todo]() {
            fs::path work = app_temp_dir() / L"langue";  // 1.0.30 : dossier temporaire du programme
            for (const Song & s : todo) {
                if (lang_stop) break;
                fs::path audio;
                for (const wchar_t * n : { L"audio.mp3", L"audio.flac", L"audio.wav" }) {
                    if (fs::exists(song_folder(s.id) / n)) {
                        audio = song_folder(s.id) / n;
                        break;
                    }
                }
                if (audio.empty()) {
                    st->set_language_check(s.id, "", 1);
                    continue;
                }
                if (s.instrumental) {  // marqué instrumental : des paroles ont-elles tout de même été chantées ?
                    // 1) la musique « parle-t-elle » une langue connue de façon cohérente ? (sur de la musique seule, Whisper
                    //    renvoie des langues au hasard avec une faible confiance) 2) combien de mots distincts y entend-on ?
                    float       lavg = 0;
                    int         lvotes = 0;
                    fs::path    model = pa.models_dir / "voix" / whisper_file().name;
                    std::string code = detect_audio_language(audio, pa.whisper, model, work, s.audio_seconds, lavg, lvotes);
                    std::string lang;
                    int         words = 0;
                    if (language_index_for_code(code) > 0 && lvotes >= 2 && lavg >= 0.5f) {
                        detect_audio_vocals(audio, pa.whisper, model, work, s.audio_seconds, lang, words);
                    }
                    if (lang_stop && code.empty()) break;
                    bool vocals = words >= 10 && lang == code;
                    st->set_heard_vocals(s.id, vocals ? lang : "");
                    if (vocals) lang_fixed++;
                    lang_checked++;
                    continue;
                }
                float       avg   = 0;
                int         votes = 0;
                std::string heard = detect_audio_language(audio, pa.whisper, pa.models_dir / "voix" / whisper_file().name, work, s.audio_seconds, avg, votes);
                if (lang_stop && heard.empty()) break;  // interrompu : on recommencera plus tard
                int  li   = language_index_for_code(heard);
                bool sure = li > 0 && votes >= 2 && avg >= (s.lyrics_lang.empty() ? 0.45f : 0.6f);
                if (li > 0 && heard == s.lyrics_lang && avg >= 0.35f) {
                    st->set_language_check(s.id, "", 2);  // confirmée
                } else if (sure) {
                    st->set_language_check(s.id, heard, 2);
                    lang_fixed++;
                } else {
                    st->set_language_check(s.id, "", 1);  // non concluant : la langue déclarée ou déduite du texte reste
                }
                lang_checked++;
            }
            std::error_code ec;
            fs::remove_all(work, ec);
            lang_done_flag = lang_stop ? 2 : 1;
            lang_running   = false;
        }).detach();
    }

    // ---- Analyse d'un fichier audio choisi pour une reprise
    void start_analysis() {
        Form & f = form;
        if (!analysis_present || f.cover.empty()) return;
        if (analysis) analysis->cancel = true;
        auto job  = std::make_shared<AnalysisJob>();
        job->file = f.cover;
        job->with_score = studio->can_transcribe() && !studio->local_active();  // la partition utilise la carte graphique
        job->with_lyrics = whisper_present && !studio->local_active();
        job->search_web  = settings.lyrics_web;
        analysis  = job;
        Paths    pa   = studio->paths();
        fs::path work = app_temp_dir() / L"analyse";  // 1.0.30 : dossier temporaire du programme
        std::thread([job, pa, work]() {
            AudioAnalysis out;
            auto          progress = [&](const std::string & what, float f) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->stage    = what;
                job->progress = f;
            };
            bool ok = analyze_audio(job->file, pa.models_dir / "analyse", work, job->with_score ? pa.transcribe : fs::path(), pa.transcriber,
                                    job->with_lyrics ? pa.whisper : fs::path(), pa.models_dir / "voix" / whisper_file().name, progress,
                                    job->cancel, out);
            // Paroles sur Internet (morceau chanté seulement) : corrigent la transcription de Whisper.
            // Délais courts : hors ligne, l'analyse se termine quand même en quelques secondes.
            if (ok && job->search_web && out.voice >= 0.3f && !job->cancel) {
                progress("web", 0.97f);
                job->identity = guess_song_identity(job->file, out.duration);
                std::string heard;
                for (const LyricLine & l : out.lyric_lines) heard += l.text + "\n";
                job->web = fetch_web_lyrics(job->identity, heard, job->cancel);
                if (job->web.status == WebLyricsStatus::Found) {
                    std::vector<std::string> secs = suggested_structure(out, false);
                    bool intro = !secs.empty() && secs.front() == "Intro";
                    bool outro = !secs.empty() && secs.back() == "Outro";
                    job->web_text = web_lyrics_to_yue2(job->web.plain, intro, outro);
                }
            }
            job->result = out;
            if (ok) job->sugg = build_suggestions(out);
            job->state = ok ? 1 : (job->cancel ? 3 : 2);
        }).detach();
    }

    // Paroles proposées par l'analyse : celles d'Internet si elles ont été trouvées et vérifiées, sinon Whisper.
    static std::string analysis_lyrics(const AnalysisJob & job) {
        return !job.web_text.empty() ? job.web_text : suggested_lyrics(job.result);
    }
    static bool analysis_has_lyrics(const AnalysisJob & job) {
        return !job.web_text.empty() || !job.result.lyric_lines.empty();
    }

    // Ajoute au style les balises cochées, règle le tempo et propose la structure ; generate : lance ensuite la génération.
    void apply_analysis(bool generate) {
        Form & f = form;
        if (!analysis || analysis->state != 1) return;
        const AudioAnalysis & a = analysis->result;
        const bool detected = analysis->use_lyrics && analysis_has_lyrics(*analysis);
        if (generate && !f.instrumental && (a.voice < 0.35f || !(has_lyrics_text(f.lyrics_song) || detected))) {
            set_mode(f, true);
            notify(a.voice < 0.35f ? tr("Fichier instrumental : génération en mode instrumental.")
                                   : tr("Pas de paroles à chanter : génération en mode instrumental."));
        }
        std::vector<std::string> tags = split_tags(f.style);
        int                      added = 0;
        for (const Suggestion & sg : analysis->sugg) {
            if (!sg.selected || (f.instrumental && sg.group == "voice")) continue;
            if (is_bpm_tag(sg.tag)) {
                tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
            } else if (std::any_of(tags.begin(), tags.end(), [&](const std::string & t) { return lower(t) == lower(sg.tag); })) {
                continue;
            }
            tags.push_back(sg.tag);
            added++;
        }
        f.style = join_style(tags);
        if (f.instrumental) f.style = set_language(f.style, "");
        if (detected && !f.instrumental) {
            std::string   text   = analysis_lyrics(*analysis);
            std::string & target = f.lyrics_song;
            if (!has_lyrics_text(target)) {
                target = text;
            } else if (!generate) {  // des paroles seraient perdues : confirmation
                f.pending_structure = true;
                f.pending_text      = text;
                f.pending_name      = tr("paroles du fichier analysé");
            }
            int li = language_index_for_code(a.lyrics_lang);
            if (li > 0) {
                f.language = li;
                f.style    = set_language(f.style, LANGUAGES[li].tag);
            }
        } else if (analysis->use_structure) {
            std::vector<std::string> secs = suggested_structure(a, f.instrumental);
            if (secs.size() >= 3) {
                std::string joined;
                for (size_t i = 0; i < secs.size(); i++) joined += (i ? "," : "") + secs[i];
                std::string   text   = skeleton_text(joined, f.instrumental);
                std::string & target = f.instrumental ? f.lyrics_inst : f.lyrics_song;
                if (f.instrumental || !has_lyrics_text(target)) {
                    target = text;
                } else if (!generate) {  // des paroles seraient perdues : confirmation
                    f.pending_structure = true;
                    f.pending_text      = text;
                    f.pending_name      = tr("structure du fichier analysé");
                }
            }
        }
        f.cursor = -1;
        f.dirty  = true;
        ImGui::ClearActiveID();
        if (!generate) {
            notify(trf("%d balise(s) ajoutée(s) au style.", added));
            return;
        }
        if (trim(f.title).empty()) f.title = narrow(f.cover.stem().wstring());
        if (trim(f.style).empty()) {
            notify(tr("Aucune balise de style à appliquer : génération annulée."));
            return;
        }
        request_submit(false);
    }

    // Suggestions d'une ou plusieurs catégories, en pastilles cochables.
    void draw_suggestion_group(const char * title, std::initializer_list<const char *> groups, const ImVec4 & color) {
        std::vector<Suggestion *> items;
        for (Suggestion & sg : analysis->sugg) {
            for (const char * g : groups) if (sg.group == g) items.push_back(&sg);
        }
        if (items.empty()) return;
        ImGuiStyle & st = ImGui::GetStyle();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", title);
        float avail = ImGui::GetContentRegionAvail().x;
        float x     = ImGui::CalcTextSize(title).x + st.ItemSpacing.x;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        for (size_t i = 0; i < items.size(); i++) {
            Suggestion & sg   = *items[i];
            std::string  text = (sg.selected ? "✓ " : "") + sg.tag;
            float        w    = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
            if (x + st.ItemSpacing.x * 0.6f + w <= avail) {
                ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
                x += st.ItemSpacing.x * 0.6f + w;
            } else {
                x = w;
            }
            ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, sg.selected ? 0.55f : 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, sg.selected ? 0.65f : 0.28f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Text, sg.selected ? P.accent_ink : color);
            ImGui::PushID((int) i);
            if (ImGui::SmallButton(text.c_str())) sg.selected = !sg.selected;
            ImGui::PopID();
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sg.why.c_str());
        }
        ImGui::PopStyleVar(2);
    }

    // Résultat de la recherche des paroles sur Internet, sous les balises de l'analyse.
    void draw_web_lyrics_status(const AnalysisJob & job) {
        const WebLyrics &    w  = job.web;
        const SongIdentity & id = job.identity;
        std::string          who = id.artist + " – " + id.title;
        switch (w.status) {
            case WebLyricsStatus::NotTried: return;
            case WebLyricsStatus::Found:
                colored_wrapped(P.ok, trf("Paroles trouvées sur Internet : %s", w.source.c_str()).c_str());
                if (ImGui::IsItemHovered()) {
                    std::string tip = tr("Source : lrclib.net (base de paroles ouverte). Structure reconstruite : strophes répétées = refrain.");
                    if (w.match >= 0) tip += "\n" + trf("Mots entendus par Whisper retrouvés : %d %%", (int) std::lround(w.match * 100));
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                break;
            case WebLyricsStatus::NoIdentity: muted_wrapped(tr("Paroles sur Internet : groupe ou titre introuvables (renseignez les étiquettes du fichier ou nommez-le « Groupe - Titre »).")); break;
            case WebLyricsStatus::Offline: muted_wrapped(tr("Paroles sur Internet : pas de connexion, transcription Whisper conservée.")); break;
            case WebLyricsStatus::NotFound: muted_wrapped(trf("Paroles sur Internet : « %s » introuvable, transcription Whisper conservée.", who.c_str())); break;
            case WebLyricsStatus::Mismatch:
                muted_wrapped(trf("Paroles sur Internet : trouvées pour « %s » mais trop différentes de ce que Whisper a entendu (%d %% de mots communs) ; transcription Whisper conservée.",
                                  who.c_str(), (int) std::lround(w.match * 100)));
                break;
        }
    }

    void draw_analysis_panel() {
        Form & f = form;
        if (!analysis_present) {
            muted_wrapped(tr("Analyse du style du fichier : modèle non installé (Réglages › Analyse audio)."));
            if (ImGui::SmallButton(tr("Ouvrir les réglages"))) open_settings = true;
            return;
        }
        if (!analysis || analysis->file != f.cover) {
            if (settings.analysis_auto && analysis_auto_file != f.cover) {
                analysis_auto_file = f.cover;
                start_analysis();
            } else {
                if (ImGui::Button(tr("Analyser le style de ce fichier"))) start_analysis();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Détecte genre, ambiance, instruments, voix, tempo, tonalité et structure, puis propose des balises de style."));
            }
            return;
        }
        AnalysisJob & job = *analysis;
        if (job.state == 0) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", tr("Analyse du fichier…"));
            ImGui::SameLine();
            progress_bar(job.progress, ImVec2(160 * g_dpi, 0));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Annuler"))) job.cancel = true;
            if (!job.with_score) colored_wrapped(P.muted, tr("Sans partition (tempo, tonalité, structure) : génération en cours ou moteur absent."));
            return;
        }
        if (job.state != 1) {
            if (job.state == 2) colored_wrapped(P.warn, trf("Analyse impossible : %s", job.result.error.c_str()).c_str());
            else ImGui::TextColored(P.muted, "%s", tr("Analyse annulée."));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Relancer l'analyse"))) start_analysis();
            return;
        }
        const AudioAnalysis & a = job.result;
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.accent, "%s", tr("Analyse du fichier"));
        ImGui::PopFont();
        ImGui::SameLine(0, 10 * g_dpi);
        if (ImGui::SmallButton(tr("Relancer l'analyse"))) start_analysis();
        {
            std::string info = a.voice >= 0.5f ? std::string(tr("voix chantée")) : std::string(tr("plutôt instrumental"));
            if (a.bpm > 0) info += trf("  ·  %d BPM", (int) std::lround(a.bpm));
            if (!a.key.empty()) info += "  ·  " + a.key;
            if (a.duration > 0) info += "  ·  " + fmt_time(a.duration);
            muted_wrapped(info);
        }
        if (!a.score_error.empty()) muted_wrapped(trf("Partition indisponible (%s).", a.score_error.c_str()));
        if (a.voice < 0.35f && !f.instrumental) muted_wrapped(tr("Ce fichier semble instrumental : passez en mode « Instrumental » pour une reprise fidèle."));
        draw_suggestion_group(tr("Genres"), { "genre" }, P.violet);
        draw_suggestion_group(tr("Ambiance"), { "mood" }, P.teal);
        draw_suggestion_group(tr("Voix"), { "voice" }, P.pink);
        draw_suggestion_group(tr("Instruments"), { "instrument" }, P.orange);
        draw_suggestion_group(tr("Tempo et tonalité"), { "tempo", "key" }, P.amber);
        if (job.sugg.empty()) muted_wrapped(tr("Aucune balise fiable détectée."));
        if (!a.lyrics_error.empty()) muted_wrapped(trf("Paroles non transcrites (%s).", a.lyrics_error.c_str()));
        draw_web_lyrics_status(job);
        if (analysis_has_lyrics(job)) {
            ImGui::Checkbox(tr("Reprendre les paroles détectées"), &job.use_lyrics);
            ImGui::SameLine(0, 8 * g_dpi);
            if (!job.web_text.empty()) ImGui::TextColored(P.ok, "%s", tr("(paroles trouvées sur Internet)"));
            else ImGui::TextColored(P.muted, "%s", trf("(%d lignes)", (int) a.lyric_lines.size()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Relisez et corrigez les paroles dans le bloc Paroles avant de générer."));
            if (job.use_lyrics) {
                std::string preview = analysis_lyrics(job);
                ImGui::BeginChild("##lyr_preview", ImVec2(-FLT_MIN, 110 * g_dpi), ImGuiChildFlags_Borders);
                ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
                ImGui::TextWrapped("%s", preview.c_str());
                ImGui::PopStyleColor();
                ImGui::EndChild();
            }
        }
        if (!analysis_has_lyrics(job) ? !a.sections.empty() : (!a.sections.empty() && !job.use_lyrics)) {
            ImGui::Checkbox(tr("Reprendre la structure détectée dans les paroles"), &job.use_structure);
            std::string st;
            for (const std::string & t : suggested_structure(a, f.instrumental)) st += (st.empty() ? "" : " · ") + t;
            if (ImGui::IsItemHovered() && !st.empty()) ImGui::SetTooltip("%s", st.c_str());
        }
        ImGui::BeginDisabled(job.sugg.empty());
        if (accent_button(tr("Appliquer au style et aux paroles"), ImVec2(0, 0), &P.violet)) apply_analysis(false);
        ImGui::SameLine();
        if (soft_button(tr("Appliquer et générer"), P.green, false)) apply_analysis(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(tr("Mode automatique : applique les balises cochées et la structure, puis lance la génération. Sans paroles à chanter, la reprise est générée en instrumental."));
        }
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
    }

    GpuMonitor gpu_monitor;

    static std::string fmt_gb(unsigned long long bytes) {
        char buf[32];
        // Unités binaires comme le Gestionnaire des tâches de Windows (12 Go = 12 Gio)
        if (bytes >= 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), tr("%.1f Go"), bytes / 1073741824.0);
        else snprintf(buf, sizeof(buf), tr("%.0f Mo"), bytes / 1048576.0);
        for (char & c : buf) if (c == '.') c = ',';
        return buf;
    }

    // 1.0.67 : compteur de voiture : arc en dégradé vert → rouge, aiguille, valeur et nom. Valeurs relues toutes les
    // 2 secondes : l'aiguille rejoint la nouvelle valeur en douceur. limit : repère orange (GPU seulement, 100 = aucun).
    float gauge_anim[4] = { 0, 0, 0, 0 };
    float gauge_height() const { return (settings.gauge_style == 0 ? 58 * g_dpi + 2 * ImGui::GetTextLineHeight() : 48 * g_dpi); }
    void draw_dial(int index, ImVec2 p, float w, float h, const char * label, const std::string & value, float fraction, int limit, const std::string & tip) {
        const bool sport = settings.gauge_style == 0;
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(index);
        ImGui::InvisibleButton("##compteur", ImVec2(w, h));
        ImGui::PopID();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s : %s\n%s", label, value.c_str(), tip.c_str());
        float & f = gauge_anim[index];
        f += (std::clamp(fraction, 0.f, 1.f) - f) * std::min(1.f, ImGui::GetIO().DeltaTime * 6.f);

        ImDrawList * dl  = ImGui::GetWindowDrawList();
        const float  th  = ImGui::GetTextLineHeight();
        const float  r   = sport ? std::min(46 * g_dpi, w * .46f) : std::min(28 * g_dpi, w * .46f);
        const float  cx  = sport ? p.x + w * .5f : p.x + r + 4 * g_dpi;
        const float  cy  = sport ? p.y + 4 * g_dpi + r + 4 * g_dpi : p.y + h - 8 * g_dpi;
        const float  pi  = 3.14159265f;
        auto at = [&](float t, float radius) { const float a = pi + pi * t; return ImVec2(cx + radius * std::cos(a), cy + radius * std::sin(a)); };
        const int    steps = 48;
        const float  thick = (sport ? 7 : 5) * g_dpi;
        for (int i = 0; i < steps; i++) {
            const float t0 = i / (float) steps, t1 = (i + 1) / (float) steps;
            dl->AddLine(at(t0, r), at(t1 + .004f, r), ImGui::GetColorU32(t0 <= f && f > 0 ? gauge_color(t0) : P.chip), thick);
        }
        if (sport) {
            for (int i = 0; i <= 20; i++) {
                const bool major = i % 5 == 0;
                dl->AddLine(at(i / 20.f, r - thick), at(i / 20.f, r - thick - (major ? 7 : 4) * g_dpi), ImGui::GetColorU32(major ? P.text : P.muted), major ? 1.5f * g_dpi : 1.f);
            }
        }
        if (limit < 100) dl->AddLine(at(limit / 100.f, r - thick * .9f), at(limit / 100.f, r + thick), ImGui::GetColorU32(P.amber), 3 * g_dpi);
        const ImVec2 tip_pt = at(f, r - thick - (sport ? 3 : 1) * g_dpi);
        const float  a = pi + pi * f, nx = -std::sin(a) * 2.6f * g_dpi, ny = std::cos(a) * 2.6f * g_dpi;
        dl->AddTriangleFilled(tip_pt, ImVec2(cx + nx, cy + ny), ImVec2(cx - nx, cy - ny), IM_COL32(255, 72, 60, 255));
        dl->AddCircleFilled(ImVec2(cx, cy), 5 * g_dpi, ImGui::GetColorU32(P.chip));
        dl->AddCircle(ImVec2(cx, cy), 5 * g_dpi, ImGui::GetColorU32(P.muted));

        ImGui::PushFont(g_bold);
        const ImVec2 vs = ImGui::CalcTextSize(value.c_str());
        ImGui::PopFont();
        const ImVec2 ls = ImGui::CalcTextSize(label);
        ImVec2       vp, lp;
        if (sport) {
            vp = ImVec2(cx - vs.x * .5f, cy + 7 * g_dpi);
            lp = ImVec2(cx - ls.x * .5f, vp.y + th);
        } else {
            vp = ImVec2(cx + r + 10 * g_dpi, p.y + h * .5f - th);
            lp = ImVec2(vp.x, vp.y + th);
        }
        // fenêtre étroite : le texte qui ne tient pas reste dans la bulle
        if (sport ? vs.x <= w : vp.x + vs.x <= p.x + w) {
            ImGui::PushFont(g_bold);
            dl->AddText(vp, ImGui::GetColorU32(P.text), value.c_str());
            ImGui::PopFont();
        }
        if (sport ? ls.x <= w : lp.x + ls.x <= p.x + w) dl->AddText(lp, ImGui::GetColorU32(P.muted), label);
    }

    // Quatre compteurs dans l'en-tête : mémoire vidéo utilisée, débordement vers la mémoire système,
    // utilisation du processeur (CPU) et de la carte graphique (GPU). Valeurs relues toutes les 2 secondes.
    // Moyenne glissante de l'utilisation GPU (16 dernières secondes) : avec une limite, un long morceau alterne des pas
    // de plusieurs secondes à 100 % et des pauses ; seule la moyenne montre l'effet de la limite.
    std::vector<std::pair<double, double>> gpu_history;  // (instant, utilisation en %)
    double gpu_average(double now, double value) {
        if (value >= 0 && (gpu_history.empty() || now - gpu_history.back().first >= 1.9)) gpu_history.push_back({ now, value });
        while (!gpu_history.empty() && now - gpu_history.front().first > 16.0) gpu_history.erase(gpu_history.begin());
        if (gpu_history.empty()) return -1;
        double sum = 0;
        for (auto & h : gpu_history) sum += h.second;
        return sum / gpu_history.size();
    }

    void draw_gauges(ImVec2 p, float width, float height) {
        GpuMem g    = gpu_monitor.get();
        float  cell = std::floor(width / 4);
        if (cell < 44 * g_dpi) return;  // fenêtre trop étroite : pas de place sans recouvrir les boutons
        std::string used_text = tr("indisponible"), spill_text = tr("indisponible"), used_tip = tr("Mémoire vidéo dédiée occupée sur la carte, tous programmes confondus.");
        float       used_f = 0, spill_f = 0;
        if (g.ok) {
            used_text = fmt_gb(g.used);
            used_f    = g.total ? (float) ((double) g.used / (double) g.total) : 0.f;
            if (g.total) used_tip = trf("%.0f %%", 100.0 * g.used / g.total) + " · " + fmt_gb(g.total) + "\n" + used_tip;
            const double scale = 2.0 * 1073741824.0;  // compteur plein à 2 Go de débordement
            spill_text = fmt_gb(g.shared);
            spill_f    = (float) std::min(1.0, (double) g.shared / scale);
        }
        draw_dial(0, p, cell, height, tr("Mémoire GPU"), used_text, used_f, 100, used_tip);
        draw_dial(1, ImVec2(p.x + cell, p.y), cell, height, tr("Débordement"), spill_text, spill_f, 100,
                  tr("Mémoire système utilisée par la carte quand sa mémoire est pleine : bien plus lente,\nelle ralentit fortement la génération. Compteur plein = 2 Go."));
        // limit : limite réglée (GPU seulement, 100 = aucune), repère orange sur le compteur.
        double gpu_avg = gpu_average(ImGui::GetTime(), g.gpu);
        auto load = [&](int index, const char * label, double pct, const char * tip, int limit) {
            std::string text = pct < 0 ? std::string(tr("indisponible")) : trf("%.0f %%", pct);
            std::string info = tip;
            float       f    = pct < 0 ? 0.f : (float) (pct / 100.0);
            if (limit < 100 && pct >= 0 && gpu_avg >= 0) {
                // Avec une limite : le compteur montre la moyenne sur 16 s, comparée à la limite.
                text = trf("%.0f %%", gpu_avg);
                f    = (float) (gpu_avg / 100.0);
                info += "\n" + trf("Instantané : %.0f %%", pct) + "\n" + trf("Limite réglée : %d (repère orange).", limit) + "\n" +
                        tr("Avec une limite, le compteur montre la moyenne des 16 dernières secondes : sur un long morceau, la carte alterne des pas de plusieurs secondes à pleine puissance et des pauses, la limite s'applique en moyenne.");
            }
            draw_dial(index, ImVec2(p.x + cell * index, p.y), cell, height, label, text, f, limit, info);
        };
        load(2, tr("Utilisation CPU"), g.cpu, tr("Utilisation du processeur, tous programmes confondus (comme le Gestionnaire des tâches)."), 100);
        load(3, tr("Utilisation GPU"), g.gpu,
             tr("Utilisation de la carte graphique : moteur le plus chargé (3D, calcul, vidéo…), tous programmes confondus.\nPendant une génération, elle doit rester proche du maximum."), settings.gpu_limit);
    }

    // Modèles YuE2-3B disponibles (Q8_0, Q6_K, Q5_K_M…) : le choix rapide de la barre du haut s'applique aux
    // prochains morceaux ; le modèle par défaut (Réglages) est celui pris à chaque démarrage.
    std::vector<std::string> models;
    std::string              current_model;
    static std::string model_label(const std::string & file) {
        if (file.empty()) return "Q8_0";
        std::string s = file;
        if (s.rfind("YuE2-3B-", 0) == 0) s = s.substr(8);
        if (s.size() > 5 && s.compare(s.size() - 5, 5, ".gguf") == 0) s.resize(s.size() - 5);
        return s;
    }
    void init_models() {
        models        = studio->paths().list_models();
        current_model = settings.model;
        if (std::find(models.begin(), models.end(), current_model) == models.end()) {
            // Modèle par défaut conseillé : Q6_K (qualité jugée équivalente, plus léger), à défaut Q8_0
            current_model = models.empty() ? "" : models.front();
            for (const char * preferred : { "YuE2-3B-Q6_K.gguf", "YuE2-3B-Q8_0.gguf" }) {
                if (std::find(models.begin(), models.end(), preferred) != models.end()) {
                    current_model = preferred;
                    break;
                }
            }
        }
        studio->set_active_model(current_model);
    }
    // Solveur de la phase NAR : méthode + nombre de pas (voir solver_specs : AB3 16 … MP 32, du plus rapide au plus lent).
    std::string current_solver = "ab2_32";
    static const char * solver_label(const std::string & s) { return solver_spec(s).label; }
    // Clé canonique d'un réglage mémorisé (anciennes valeurs « ab2 » / « midpoint » comprises ; vide = AB2 32, le défaut).
    static std::string solver_key(const std::string & s) { return s.empty() ? std::string("ab2_32") : std::string(solver_spec(s).key); }
    // ---- Recommandations selon le matériel (carte, mémoire vive, processeur, disque des modèles)
    SystemInfo sysinfo;  // lu au démarrage
    struct Advice {
        int         level;  // 0 bon, 1 conseil, 2 attention
        std::string text;
    };
    // Conseils pour ce PC ; rec_model / rec_solver : réglages conseillés (fichier GGUF, clé de solveur).
    std::vector<Advice> build_advice(std::string & rec_model, std::string & rec_solver) const {
        std::vector<Advice> out;
        const GpuInfo &     g      = gpu_profile().gpu;
        const double        vram   = (double) g.vram / 1073741824.0;
        const double        ram    = (double) sysinfo.ram_total / 1073741824.0;
        // Modèle : la génération occupe 4 à 7 Go en plus du modèle (2,6 à 3,8 Go selon la quantification).
        if (vram >= 15) rec_model = "YuE2-3B-Q8_0.gguf";
        else if (vram >= 11) rec_model = "YuE2-3B-Q6_K.gguf";
        else rec_model = "YuE2-3B-Q5_K_M.gguf";
        rec_solver = vram >= 11 && g.vendor != GpuVendor::Intel ? "ab2_32" : "ab3_20";
        if (!g.found) {
            out.push_back({ 2, tr("Carte graphique non reconnue : recommandations impossibles.") });
            return out;
        }
        out.push_back({ 0, trf("Modèle conseillé : %s (carte de %.0f Go).", model_label(rec_model).c_str(), vram) });
        if (vram >= 15) out.push_back({ 0, tr("Durée : jusqu'à 6 minutes sans débordement.") });
        else if (vram >= 11) out.push_back({ 0, tr("Durée : jusqu'à 4 à 5 minutes sans débordement, si aucun autre programme n'occupe la carte.") });
        else if (vram >= 8) out.push_back({ 1, tr("Durée : 3 minutes au plus ; au-delà, la mémoire de la carte déborde et la génération ralentit fortement.") });
        else out.push_back({ 2, tr("Carte trop juste (moins de 8 Go) : 2 minutes au plus, débordement probable.") });
        out.push_back({ 0, trf("Solveur conseillé : %s.", solver_label(rec_solver)) });
        if (g.vendor == GpuVendor::Amd) out.push_back({ 0, tr("Carte AMD : mode stable (FP32) conservé, indispensable sur les RX 6000.") });
        else if (g.vendor == GpuVendor::Nvidia && !gpu_profile().cuda_installed) out.push_back({ 1, tr("Carte Nvidia : installez le moteur CUDA (dossier « outils\\moteur\\cuda »), souvent plus rapide que Vulkan.") });
        else if (g.vendor == GpuVendor::Nvidia) out.push_back({ 0, tr("Carte Nvidia : moteur CUDA installé.") });
        else out.push_back({ 1, tr("Carte Intel ou inconnue : non testée, gardez le mode stable.") });
        if (ram > 0 && ram < 15) out.push_back({ 2, trf("Mémoire vive : %.0f Go. 16 Go conseillés : en cas de débordement, la carte se sert de la mémoire vive.", ram) });
        else if (ram >= 31) out.push_back({ 0, trf("Mémoire vive : %.0f Go, confortable.", ram) });
        else if (ram > 0) out.push_back({ 0, trf("Mémoire vive : %.0f Go, suffisante.", ram) });
        if (sysinfo.models_disk == DiskKind::Hdd) {
            out.push_back({ 2, trf("Les modèles sont sur un disque dur (%s) : chaque génération recharge 3 à 4 Go, soit environ 30 s de plus. Placez le dossier des modèles sur un SSD.", sysinfo.models_drive.c_str()) });
        } else if (sysinfo.models_disk == DiskKind::Ssd) {
            out.push_back({ 0, trf("Modèles sur un SSD (%s) : chargement rapide.", sysinfo.models_drive.c_str()) });
        }
        if (sysinfo.threads > 0 && sysinfo.threads < 8) out.push_back({ 1, tr("Processeur de moins de 8 cœurs logiques : l'analyse audio et les exports seront plus lents.") });
        return out;
    }
    // Réglages › Système › Recommandations pour ce PC.
    void draw_advice() {
        const GpuInfo & g   = gpu_profile().gpu;
        const char *    dsk = sysinfo.models_disk == DiskKind::Ssd ? "SSD" : sysinfo.models_disk == DiskKind::Hdd ? tr("disque dur") : tr("type inconnu");
        muted_wrapped(trf("%s · %.0f Go de mémoire vive · %s (%d cœurs logiques) · disque des modèles : %s", g.name.c_str(),
                          (double) sysinfo.ram_total / 1073741824.0, sysinfo.cpu_name.c_str(), sysinfo.threads, dsk));
        std::string rec_model, rec_solver;
        for (const Advice & a : build_advice(rec_model, rec_solver)) {
            ImVec4 c = a.level == 0 ? P.ok : a.level == 1 ? P.amber : P.warn;
            ImGui::TextColored(c, "%s", "●");  // pastille verte (bon), ambre (conseil) ou orange (attention)
            ImGui::SameLine(0, 6 * g_dpi);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.text.c_str());
            ImGui::PopTextWrapPos();
        }
        const bool installed = std::find(models.begin(), models.end(), rec_model) != models.end();
        const bool same      = solver_key(settings.solver) == rec_solver && (!installed || settings.model == rec_model);
        ImGui::BeginDisabled(same);
        if (accent_button(tr("Appliquer les réglages conseillés"), ImVec2(0, 0), &P.green)) {
            if (installed) {
                settings.model = rec_model;
                current_model  = rec_model;
            }
            settings.solver = rec_solver;
            current_solver  = rec_solver;
            studio->set_active_solver(rec_solver);
            save_settings(settings, settings_path);
            notify(tr("Réglages conseillés appliqués (modèle et solveur par défaut)."));
        }
        ImGui::EndDisabled();
        if (same) {
            ImGui::SameLine();
            ImGui::TextColored(P.ok, "%s", tr("● réglages déjà conformes"));
        } else if (!installed) {
            ImGui::SameLine();
            colored_wrapped(P.muted, trf("(modèle %s non installé : seul le solveur sera changé)", model_label(rec_model).c_str()).c_str());
        }
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    }

    // ---- Vérification de la mémoire vidéo avant une génération
    bool                    open_vram = false;  // ouvrir l'avertissement « mémoire vidéo insuffisante »
    std::vector<GpuProcess> vram_hogs;          // programmes qui occupent la carte
    double                  vram_need = 0, vram_free = 0;  // en Go
    // Vrai si la génération peut démarrer sans déborder. Sinon remplit vram_hogs / vram_need / vram_free.
    bool vram_check_ok() {
        if (studio->local_active()) return true;  // une génération tourne déjà : le morceau attendra son tour
        GpuMem g = gpu_monitor.get();
        if (!g.ok || g.total == 0) return true;  // compteurs indisponibles : pas de vérification
        const Form &    f = form;
        std::error_code ec;
        double model_gb = current_model.empty() ? 3.8 : (double) fs::file_size(studio->paths().gguf_dir / widen(current_model), ec) / 1073741824.0;
        if (ec) model_gb = 3.8;
        // Durée prévue : celle de la reprise, l'estimation d'après les paroles (durée automatique) ou le plafond.
        double secs = f.duration;
        if (!f.cover.empty() && f.cover_seconds > 0) secs = std::min(secs, f.cover_seconds);
        else if (f.auto_duration) secs = std::min(secs, estimate_seconds(f.instrumental ? f.lyrics_inst : f.lyrics_song, f.instrumental));
        // Mesures sur RX 6700 XT : 7 Go (Q6_K, 2 min) à 10 Go (Q8_0, 6 min) ; environ 4 Go de travail + 0,4 Go par minute.
        vram_need = model_gb + 4.0 + 0.4 * std::clamp(secs / 60.0, 1.0, 6.0);
        // Balises négatives : le moteur compose deux versions en parallèle, son cache de travail double
        // (mesuré : 2,7 Go de plus avec Q6_K, quelle que soit la durée).
        if (!trim(f.negative_style).empty()) vram_need += 2.7;
        vram_free = (double) (g.total > g.used ? g.total - g.used : 0) / 1073741824.0;
        if (vram_free >= vram_need) return true;
        const GpuInfo & gi = gpu_profile().gpu;
        vram_hogs = gpu_memory_by_process(gi.found ? gi.luid : g_adapter_luid, 6);
        DWORD self = GetCurrentProcessId();
        vram_hogs.erase(std::remove_if(vram_hogs.begin(), vram_hogs.end(), [&](const GpuProcess & p) { return p.pid == self; }), vram_hogs.end());
        return false;
    }
    // Bouton « Générer » : vérifie d'abord la mémoire vidéo libre ; « Ajouter en pause » ne démarre rien.
    void request_submit(bool held) {
        // L'IA locale occupe la carte graphique ou le processeur : pas de génération en même temps (1.0.26).
        if (ai_local_busy()) {
            notify(tr("L'IA locale est en train d'écrire : attendez la fin (ou « Arrêter ») avant de générer."));
            return;
        }
        if (held || vram_check_ok()) submit(held);
        else open_vram = true;
    }
    void draw_vram_popup() {
        if (open_vram) {
            ImGui::OpenPopup(tr("Mémoire vidéo insuffisante"));
            open_vram = false;
        }
        ImGui::SetNextWindowSize(ImVec2(560 * g_dpi, 0));
        if (!ImGui::BeginPopupModal(tr("Mémoire vidéo insuffisante"), nullptr, ImGuiWindowFlags_NoResize)) return;
        ImGui::TextWrapped("%s", trf("Cette génération demande environ %.1f Go de mémoire vidéo ; il en reste %.1f Go de libre. Le reste débordera vers la mémoire système et la génération sera beaucoup plus lente (2 à 8 fois).",
                                     vram_need, vram_free).c_str());
        if (!vram_hogs.empty()) {
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
            ImGui::TextColored(P.muted, "%s", tr("Programmes qui occupent la carte :"));
            for (const GpuProcess & p : vram_hogs) {
                ImGui::BulletText("%s — %s", p.name.c_str(), fmt_gb(p.dedicated).c_str());
            }
            muted_wrapped(tr("Fermez ceux dont vous n'avez pas besoin (navigateur, jeux, logiciels vidéo), puis relancez la génération."));
        }
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (accent_button(tr("Générer quand même"), ImVec2(0, 0), &P.warn)) {
            submit(false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Ajouter en pause"))) {
            submit(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Gestionnaire des tâches"))) ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Réglages › Système › Carte graphique : carte détectée, moteur utilisé, compatibilité et choix du moteur.
    // Limite d'utilisation de la carte : de 30 à 100 % (100 = aucune limite), appliquée aussi pendant une génération.
    void draw_gpu_limit() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Limite d'utilisation du GPU"));
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::SetNextItemWidth(std::min(280 * g_dpi, ImGui::GetContentRegionAvail().x));
        std::string shown = settings.gpu_limit >= 100 ? std::string(tr("aucune limite")) : std::to_string(settings.gpu_limit) + " %%";  // %% : format d'ImGui
        if (ImGui::SliderInt("##gpu_limit", &settings.gpu_limit, 30, 100, shown.c_str(), ImGuiSliderFlags_AlwaysClamp)) {
            set_gpu_limit(settings.gpu_limit);  // pris en compte dès le pas suivant, même pendant une génération
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Le moteur est mis en pause par intervalles très courts pour laisser souffler la carte (température, bruit, autres programmes).\nExemple : limite à 90 → génération environ 1,1 fois plus longue. Effet approximatif : la jauge « Utilisation GPU » fait foi."));
        }
    }

    void draw_gpu_profile() {
        GpuProfile &    gp = gpu_profile();
        const GpuInfo & g  = gp.gpu;
        if (g.found) {
            ImGui::TextColored(P.muted, tr("Carte : %s (%s, %.0f Go)"), g.name.c_str(), gpu_vendor_name(g.vendor),
                               (double) g.vram / 1073741824.0);
        } else {
            ImGui::TextColored(P.warn, "%s", tr("Carte graphique non reconnue."));
        }
        ImGui::TextColored(P.muted, tr("Moteur utilisé : %s"), gp.cuda_active ? "CUDA (Nvidia)" : "Vulkan");

        // Compatibilité : mode stable (noyaux FP32) ou rapide (FP16). Pris en compte dès la génération suivante.
        const char * compat_labels[] = { N_("Automatique"), N_("Stable (plus lent, sans grésillement)"), N_("Rapide (FP16)") };
        const char * compat_notes[]  = {
            N_("Stable sur les cartes AMD et Intel, rapide sur les cartes Nvidia."),
            N_("Impose les calculs en précision complète (FP32) : indispensable sur les cartes AMD RDNA2 (RX 6000), à essayer si un morceau grésille ou sature."),
            N_("Calculs en demi-précision : plus rapide, mais peut produire des grésillements sur certaines cartes AMD."),
        };
        // Les deux listes commencent au même endroit : après le plus long des deux libellés.
        const float label_w = std::max(ImGui::CalcTextSize(tr("Compatibilité de la carte")).x, ImGui::CalcTextSize(tr("Moteur de calcul")).x) + 10 * g_dpi;
        const float combo_x = ImGui::GetCursorPosX() + label_w;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Compatibilité de la carte"));
        ImGui::SameLine(combo_x);
        ImGui::SetNextItemWidth(320 * g_dpi);
        int c = gp.compat.load();
        if (ImGui::BeginCombo("##gpu_compat", tr(compat_labels[c]))) {
            for (int i = 0; i < 3; i++) {
                if (ImGui::Selectable(tr(compat_labels[i]), c == i)) {
                    gp.compat           = i;
                    settings.gpu_compat = compat_to_string(i);
                    save_settings(settings, settings_path);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(compat_notes[i]));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::TextColored(P.muted, "%s", gp.stable() ? tr("→ mode stable") : tr("→ mode rapide"));
        if (gp.cuda_active) muted_wrapped(tr("Avec le moteur CUDA, ce réglage n'a pas d'effet (il concerne le moteur Vulkan)."));

        // Moteur de calcul : Vulkan (toutes cartes) ou CUDA (Nvidia, si le dossier « moteur\cuda » ou l'ancien « moteur_cuda » est installé).
        const char * backend_labels[] = { N_("Automatique"), "Vulkan", "CUDA" };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Moteur de calcul"));
        ImGui::SameLine(combo_x);
        ImGui::SetNextItemWidth(320 * g_dpi);
        int b = gp.backend.load();
        if (ImGui::BeginCombo("##gpu_backend", tr(backend_labels[b]))) {
            for (int i = 0; i < 3; i++) {
                const bool cuda_missing = i == BACKEND_CUDA && !gp.cuda_installed;
                ImGui::BeginDisabled(cuda_missing);
                if (ImGui::Selectable(tr(backend_labels[i]), b == i)) {
                    gp.backend           = i;
                    settings.gpu_backend = backend_to_string(i);
                    save_settings(settings, settings_path);
                }
                ImGui::EndDisabled();
                if (cuda_missing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", tr("Moteur CUDA non installé : dossier « outils\\moteur\\cuda » absent."));
                }
            }
            ImGui::EndCombo();
        }
        if (gp.want_cuda() != gp.cuda_active) {
            ImGui::TextColored(P.warn, "%s", tr("Redémarrez l'application pour changer de moteur."));
        } else if (g.vendor == GpuVendor::Nvidia && !gp.cuda_installed) {
            muted_wrapped(tr("Carte Nvidia : le moteur Vulkan fonctionne. Le moteur CUDA, souvent plus rapide, s'installe en décompressant son archive dans le dossier de l'application (dossier « outils\\moteur\\cuda »)."));
        }
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    }

    void init_solver() {
        current_solver = solver_key(settings.solver);
        studio->set_active_solver(current_solver);
    }
    // as_default : choisit aussi le solveur par défaut au démarrage (Réglages).
    void draw_solver_combo(const char * id, float width, bool as_default) {
        const std::string shown = as_default ? solver_key(settings.solver) : current_solver;
        ImGui::SetNextItemWidth(width);
        const bool open = ImGui::BeginCombo(id, solver_label(shown));
        if (open) {
            for (const SolverSpec & sp : solver_specs()) {
                char line[96];
                snprintf(line, sizeof(line), tr("%s   ·   %d évaluations"), sp.label, sp.evals);
                if (ImGui::Selectable(line, shown == sp.key)) {
                    current_solver = sp.key;
                    if (as_default) {
                        settings.solver = sp.key;
                        save_settings(settings, settings_path);
                    }
                    studio->set_active_solver(sp.key);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(sp.note));
            }
            ImGui::EndCombo();
        }
    }

    // Liste déroulante des modèles ; as_default : choisit aussi le modèle par défaut (Réglages).
    void draw_model_combo(const char * id, float width, bool as_default) {
        const std::string & shown = as_default ? (settings.model.empty() ? current_model : settings.model) : current_model;
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo(id, model_label(shown).c_str())) {
            for (const std::string & m : models) {
                std::error_code ec;
                double          gb = (double) fs::file_size(studio->paths().gguf_dir / widen(m), ec) / 1e9;
                char            line[96];
                snprintf(line, sizeof(line), tr("%s   (%.2f Go)"), model_label(m).c_str(), gb);
                if (ImGui::Selectable(line, m == shown)) {
                    current_model = m;
                    if (as_default) {
                        settings.model = m;
                        save_settings(settings, settings_path);
                    }
                    studio->set_active_model(m);
                }
            }
            ImGui::EndCombo();
        }
    }
    std::string       video_title, video_key;
    fs::path          img_yt, img_tt, img_ig, video_dest;
    bool              video_yt = true, video_tt = true, video_ig = true;
    bool              video_clip = false;  // 1.0.52 : clip animé avec paroles au lieu de l'image fixe
    int               clip_style = 0;      // 1.0.53 : type de rendu des images (index dans clip_styles())
    bool              clip_style_loaded = false;
    bool              video_short = false;  // 1.0.74 : Short 9:16 de 30 s centré sur le refrain (dossier « Shorts »)
    bool              video_redo = false;  // 1.0.55 : refaire le scénario et les images du clip (sinon ceux déjà faits sont réutilisés)
    // 1.0.56 : par morceau de la fenêtre (id) : lignes de paroles et nombre d'images recommandé (clip_recommend_images)
    std::map<std::string, int> video_lines, video_rec;
    // Nombre d'images retenu pour un morceau selon le choix de l'utilisateur ; 0 = automatique (l'IA décide, 6 à 12)
    int clip_images_for(const Song & s) {
        const std::string & m    = settings.clip_image_mode;
        const int           base = video_rec.count(s.id) ? video_rec[s.id] : 0;
        if (m == "fixed") return std::clamp(atoi(settings.clip_image_count.c_str()), 0, 60);
        if (m == "more") return std::min(60, (int) (base * 1.5 + 0.5));
        if (m == "less") return std::max(3, (int) (base * 0.5 + 0.5));
        return base;
    }
    bool        exp_validated = true, exp_flac = true, exp_mp3 = true, exp_wav = false;
    // Jeux : la file est suspendue tant qu'un jeu tourne
    bool        game_active = false;
    std::string game_name;
    double      last_game_check = 0, game_clear_since = 0;
    // Lecture continue
    std::vector<std::string> play_queue;
    bool        was_playing = false;
    int         last_pos = 0;
    // Pilote graphique et test de santé
    std::string driver_version;
    bool        driver_changed = false;
    std::shared_ptr<DriverUpdate> driver_update = std::make_shared<DriverUpdate>();
    std::shared_ptr<UpdateInfo>   app_update    = std::make_shared<UpdateInfo>();  // version plus récente du Studio
    std::string update_address() const { return settings.update_url.empty() ? std::string(UPDATE_MANIFEST_URL) : settings.update_url; }
    // Recherche de mise à jour en tâche de fond (au démarrage, ou bouton « Vérifier maintenant »).
    void start_app_update_check() {
        if (app_update->state == 1) return;
        app_update = std::make_shared<UpdateInfo>();
        std::thread(check_for_update, app_update, update_address()).detach();
    }
    // Pastille de l'en-tête : version, et « Mise à jour x.y.z » quand une version plus récente est publiée.
    void draw_version_badge() {
        ImGui::SameLine(0, 8 * g_dpi);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", STUDIO_VERSION);
        if (app_update->state == 3) {
            ImGui::SameLine(0, 8 * g_dpi);
            if (soft_button(trf("Mettre à jour (%s)", app_update->latest.c_str()).c_str(), P.amber) && !app_update->url.empty()) {
                ShellExecuteW(nullptr, L"open", widen(app_update->url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (ImGui::IsItemHovered()) {
                std::string tip = trf("Version installée : %s · nouvelle version : %s\nOuvre la page de la dernière version sur GitHub (fichiers à télécharger).", STUDIO_VERSION, app_update->latest.c_str());
                if (!app_update->notes.empty()) tip += "\n\n" + app_update->notes;
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
    }
    // Réglages › Système : version installée et état de la recherche de mise à jour.
    void draw_update_settings() {
        ImGui::TextColored(P.muted, tr("Version installée : %s"), STUDIO_VERSION);
        ImGui::SameLine(0, 12 * g_dpi);
        switch (app_update->state.load()) {
            case 0: colored_wrapped(P.muted, tr("Recherche de mise à jour : pas encore de site de publication.")); break;
            case 1: ImGui::TextColored(P.muted, "%s", tr("Recherche d'une mise à jour…")); break;
            case 2: ImGui::TextColored(P.ok, "%s", tr("● à jour")); break;
            case 3: colored_wrapped(P.amber, trf("Mise à jour disponible : %s", app_update->latest.c_str()).c_str()); break;
            default: ImGui::TextColored(P.muted, "%s", tr("Recherche de mise à jour impossible (pas de connexion ?).")); break;
        }
        if (!update_address().empty()) {
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::BeginDisabled(app_update->state == 1);
            if (ImGui::SmallButton(tr("Vérifier maintenant"))) start_app_update_check();
            ImGui::EndDisabled();
        }
        // 1.0.81 : sauvegarde automatique du jour et vérification au démarrage
        ImGui::TextColored(P.muted, "%s", last_backup.empty() ? tr("Sauvegarde automatique : aucune pour l'instant.")
                                                              : trf("Sauvegarde automatique : %s (%d gardées)", last_backup.c_str(), backup_count).c_str());
        ImGui::SameLine(0, 12 * g_dpi);
        if (ImGui::SmallButton(tr("Ouvrir les sauvegardes"))) ShellExecuteW(nullptr, L"open", backups_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (startup_issues.empty()) ImGui::TextColored(P.ok, "%s", tr("● Vérification au démarrage : aucun fichier manquant."));
        for (const std::string & s : startup_issues) colored_wrapped(P.amber, "• " + s);
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    }

    // ---- 1.0.81 : sauvegarde automatique et vérification au démarrage (après les suppressions accidentelles du 23/10)
    // Une sauvegarde par jour, au premier lancement, dans <programme>\sauvegardes\AAAA-MM-JJ : config\*.json (réglages,
    // styles, mots à éviter, clés d'API chiffrées, dernier album), liste et fiches des LoRA, liste des modèles présents
    // (modeles.txt) et le meta.json de chaque morceau (titres, groupes, albums, réglages). Ni audio ni modèles (trop
    // lourds). Les 15 dernières sont gardées. Au démarrage, un modèle ou une LoRA présents à la dernière sauvegarde mais
    // absents maintenant, et une LoRA cochée dans un de vos styles mais absente, sont signalés.
    std::vector<std::string> startup_issues;
    bool                     startup_issues_open = false;
    std::string              last_backup;
    int                      backup_count = 0;
    fs::path backups_dir() const { return app_dir() / L"sauvegardes"; }
    // Fichiers de modeles\ d'au moins 1 Mo (les notes .txt et petites fiches ne comptent pas), chemin relatif → taille
    std::map<std::string, unsigned long long> model_files() const {
        std::map<std::string, unsigned long long> out;
        std::error_code ec;
        const fs::path root = studio->paths().models_dir;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            unsigned long long size = (unsigned long long) it->file_size(ec);
            if (size >= (1ull << 20)) out[narrow(it->path().lexically_relative(root).generic_wstring())] = size;
        }
        return out;
    }
    void startup_checks() {
        std::error_code ec;
        const fs::path        root = backups_dir();
        std::vector<fs::path> olds;
        for (const auto & e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec)) olds.push_back(e.path());
        std::sort(olds.begin(), olds.end());
        // 1. Modèles et LoRA présents à la dernière sauvegarde, absents maintenant
        const auto models   = model_files();
        bool       vanished = false;  // un fichier a disparu : la fenêtre s'ouvre à chaque lancement, pas une fois par jour
        if (!olds.empty()) {
            std::ifstream in(olds.back() / L"modeles.txt");
            const std::string when = narrow(olds.back().filename().wstring());
            for (std::string line; std::getline(in, line);) {
                std::string name = line.substr(0, line.find('\t'));
                if (!name.empty() && !models.count(name)) {
                    std::string shown = name;
                    std::replace(shown.begin(), shown.end(), '/', '\\');
                    startup_issues.push_back(trf("Disparu depuis la sauvegarde du %s : modeles\\%s", when.c_str(), shown.c_str()));
                    vanished = true;
                }
            }
        }
        // 2. LoRA cochées dans vos styles mais absentes du dossier des LoRA
        scan_loras(true);
        std::set<std::string> seen;
        for (const Preset & p : settings.presets)
            for (const LoraUse & u : p.loras)
                if (u.enabled && !lora_files.count(u.file) && seen.insert(u.file).second)
                    startup_issues.push_back(trf("Le style « %s » utilise une LoRA absente : %s", p.name.c_str(), u.file.c_str()));
        // 3. Sauvegarde du jour (une seule par jour)
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t day[16];
        swprintf(day, 16, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        const fs::path dst = root / day;
        const bool     first_today = !fs::exists(dst, ec);
        if (first_today) {
            fs::create_directories(dst / L"config", ec);
            for (const auto & e : fs::directory_iterator(studio->paths().config, ec))
                if (e.is_regular_file(ec)) fs::copy_file(e.path(), dst / L"config" / e.path().filename(), fs::copy_options::overwrite_existing, ec);
            {
                std::ofstream out(dst / L"modeles.txt", std::ios::binary);
                for (const auto & [name, size] : models) out << name << '\t' << size << '\n';
            }
            fs::create_directories(dst / L"lora", ec);
            for (const auto & e : fs::directory_iterator(studio->paths().adapters, ec))
                if (e.is_regular_file(ec) && e.path().extension() == L".json") fs::copy_file(e.path(), dst / L"lora" / e.path().filename(), fs::copy_options::overwrite_existing, ec);
            const fs::path lib = studio->paths().library;
            for (fs::recursive_directory_iterator it(lib, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (it->path().filename() != L"meta.json" || !it->is_regular_file(ec)) continue;
                const fs::path to = dst / L"Audios" / it->path().lexically_relative(lib);
                fs::create_directories(to.parent_path(), ec);
                fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
            }
            olds.push_back(dst);
            // les 15 dernières sont gardées (dossiers de ce programme seulement, nommés AAAA-MM-JJ)
            while (olds.size() > 15) {
                if (olds.front().filename().wstring().size() == 10) fs::remove_all(olds.front(), ec);
                olds.erase(olds.begin());
            }
        }
        last_backup         = narrow(fs::path(day).wstring());
        backup_count        = (int) olds.size();
        startup_issues_open = !startup_issues.empty() && (first_today || vanished);  // LoRA d'un style absente : une fois par jour
    }
    void draw_startup_issues() {
        if (startup_issues_open) {
            ImGui::OpenPopup(tr("Vérification au démarrage"));
            startup_issues_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(720 * g_dpi, 0));
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(tr("Vérification au démarrage"), nullptr, ImGuiWindowFlags_NoResize)) return;
        muted_wrapped(tr("Des fichiers attendus sont introuvables. Vos réglages, vos styles, la liste des LoRA et la fiche de chaque morceau sont sauvegardés chaque jour dans le dossier « sauvegardes » du programme."));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        for (const std::string & s : startup_issues) colored_wrapped(P.amber, "• " + s);
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (soft_button(tr("Ouvrir le dossier des modèles"), P.violet)) ShellExecuteW(nullptr, L"open", studio->paths().models_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (soft_button(tr("Ouvrir les sauvegardes"), P.teal)) ShellExecuteW(nullptr, L"open", backups_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    std::shared_ptr<RemoteSizes>  remote = std::make_shared<RemoteSizes>();  // fichiers à jour (modèles, LoRA)
    bool                          updates_announced = false;
    bool        driver_update_announced = false;
    double      last_health_seen = 0;

    // Coupe le style en lignes à la largeur du champ, uniquement après les virgules.
    static std::string wrap_style(const std::string & style, float width) {
        std::string out = style;
        float       x   = 0;
        size_t      start = 0;
        while (start < out.size()) {
            size_t comma = out.find(", ", start);
            size_t end   = comma == std::string::npos ? out.size() : comma + 1;
            float  w     = ImGui::CalcTextSize(out.c_str() + start, out.c_str() + end).x;
            if (x > 0 && x + w > width && start > 0 && out[start - 1] == ' ') {
                out[start - 1] = '\n';
                x              = 0;
            }
            x += w + ImGui::CalcTextSize(" ").x;
            if (comma == std::string::npos) break;
            start = comma + 2;
        }
        return out;
    }

    void notify(const std::string & text) {
        toast       = text;
        toast_until = ImGui::GetTime() + 4.0;
    }

    // ------------------------------------------------------------------ Formulaire : actions

    void clear_form() {
        Form & f = form;
        f.title.clear();
        f.style.clear();
        f.negative_style.clear();
        f.artist.clear();
        f.album.clear();
        (f.instrumental ? f.lyrics_inst : f.lyrics_song).clear();
        f.cover.clear();
        f.cover_seconds = 0;
        f.cursor        = -1;
        f.dirty         = true;
        ImGui::ClearActiveID();
        notify(tr("Formulaire vidé : vous pouvez coller un nouveau morceau."));
    }

    static bool has_lyrics_text(const std::string & lyrics) {
        size_t start = 0;
        while (start < lyrics.size()) {
            size_t      end  = lyrics.find('\n', start);
            std::string line = trim(lyrics.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (!line.empty() && !(line.front() == '[' && line.back() == ']')) return true;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return false;
    }

    // Paroles en anglais, français, espagnol ou italien ? Compte les mots-outils les plus fréquents.
    static int guess_language(const std::string & lyrics) {
        static const char * en[] = { "the", "and", "you", "is", "my", "to", "in", "of", "me", "it", "we", "your",
                                     "on", "i'm", "don't", "be", "all", "for", "with", "this" };
        static const char * fr[] = { "le", "la", "les", "je", "tu", "et", "est", "une", "des", "que", "dans", "pour",
                                     "pas", "mon", "ma", "qui", "sur", "nous", "c'est", "au" };
        static const char * es[] = { "el", "los", "las", "yo", "eres", "y", "es", "una", "en", "que", "por", "mi", "no", "con" };
        static const char * it[] = { "il", "gli", "io", "sei", "e", "è", "una", "di", "che", "per", "non", "mio", "con", "sono" };
        int         score[5] = { 0, 0, 0, 0, 0 };
        std::string word;
        auto        flush = [&] {
            for (const char * w : en) score[1] += word == w;
            for (const char * w : fr) score[2] += word == w;
            for (const char * w : es) score[3] += word == w;
            for (const char * w : it) score[4] += word == w;
            word.clear();
        };
        for (char c : fold(lyrics) + " ") {
            if (isalpha((unsigned char) c) || c == '\'' || (unsigned char) c >= 0x80) word += c;
            else flush();
        }
        int best = 0;
        for (int i = 1; i < 5; i++) {
            if (score[i] > score[best]) best = i;
        }
        return score[best] >= 5 ? best : 0;
    }

    // Convertit des blocs au format Suno vers le formulaire au format YuE2.
    void adapt(const std::string & title_block, const std::string & style_block, const std::string & lyrics_block,
               bool instrumental) {
        Form &     f  = form;
        TitleBlock tb = parse_title_block(title_block);
        if (!tb.title.empty()) f.title = tb.title;
        if (!tb.artist.empty()) f.artist = tb.artist;
        if (!tb.album.empty()) f.album = tb.album;
        std::vector<std::string> extra;
        std::string              lyrics = convert_lyrics(lyrics_block, instrumental, extra);
        std::string              style  = convert_style(style_block);
        if (style.empty()) style = style_block;  // rien de reconnu : texte d'origine conservé
        std::vector<std::string> tags = split_style(merge_style(style, extra));  // le BPM des paroles l'emporte
        if (tags.size() > 18) tags.resize(18);  // au-delà, le style devient flou et contradictoire
        int lang = instrumental ? 0 : guess_language(lyrics);
        if (lang) f.language = lang;
        if (f.instrumental != instrumental) set_mode(f, instrumental);
        f.style = set_language(join_style(tags), instrumental ? "" : LANGUAGES[f.language].tag);
        (instrumental ? f.lyrics_inst : f.lyrics_song) = lyrics;
        f.auto_duration = true;
        f.cursor        = -1;
        f.dirty         = true;
        ImGui::ClearActiveID();
        notify(trf("Converti au format YuE2 : %d balises de style, %s estimées.", (int) split_style(f.style).size(),
                   fmt_time(estimate_seconds(lyrics, instrumental)).c_str()));
    }

    void pick_cover() {
        wchar_t       file[MAX_PATH] = {};
        OPENFILENAMEW ofn{ sizeof(ofn) };
        ofn.hwndOwner   = hwnd;
        std::wstring filter = file_filter(wtr("Fichiers audio"), L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.opus");
        std::wstring title  = wtr("Morceau à reprendre");
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile   = file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrTitle  = title.c_str();
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return;
        Form & f        = form;
        f.cover         = file;
        f.cover_seconds = media_duration(f.cover);
        // Reprise chantée : la mélodie seule guide la voix ; instrumentale : partition complète (accords).
        f.cot = f.instrumental ? 0 : 1;
    }

    void submit(bool held) {
        scan_loras(true);
        sync_lora_triggers();  // mot déclencheur des LoRA cochées : toujours présent dans le style envoyé
        Form &        f      = form;
        std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
        std::string   style  = join_style(split_style(f.style));
        std::string   words  = trim(lyrics);
        // Paroles collées au format Suno sans conversion : adaptées au vol (sections, BPM des paroles…).
        if (needs_conversion(words)) {
            std::vector<std::string> extra;
            words = trim(convert_lyrics(words, f.instrumental, extra));
            style = merge_style(style, extra);
        }
        if (f.instrumental || !has_lyrics_text(words)) {
            // Aucune parole dans la structure : on retire les références à la voix (et la langue chantée) du style,
            // sinon le modèle invente des paroles pour remplir les sections vides.
            std::vector<std::string> kept;
            int                      removed = 0;
            for (const std::string & t : split_tags(style)) {
                bool lang = false;
                for (const auto & L : LANGUAGES) lang = lang || (L.tag[0] && lower(L.tag) == lower(t));
                if (lang || is_voice_tag(t)) removed++;
                else kept.push_back(t);
            }
            if (removed > 0) {
                style = join_style(kept);
                notify(f.instrumental ? trf("Instrumental : %d balise(s) de voix retirée(s) du style avant la génération.", removed)
                                      : trf("Aucune parole dans la structure : %d balise(s) de voix retirée(s) du style avant la génération.", removed));
            }
        }
        // 1.0.48 (prototype) : morceau long. Les paroles sont coupées entre deux sections, vers le milieu ; la partie 2
        // sera lancée seule à la fin de la partie 1 (Studio::worker) et les deux seront assemblées.
        std::string long_rest;
        if (f.long_track && f.cover.empty()) {
            // 1.0.70 : 3 parties pour un texte de plus de 6 minutes (estimate_seconds plafonne à 360 s) : chacune reste sous la limite de mémoire vidéo d'une carte de 12 Go
            const bool three = ai_sung_lines(words) >= 60 || (int) std::count(words.begin(), words.end(), '[') >= 18;
            auto parts = split_long_lyrics(words, three ? 3 : 2);
            if (parts.second.empty()) {
                notify(tr("Morceau long : il faut au moins deux sections ([Verse], [Chorus]…) pour le couper en deux parties."));
                return;
            }
            words     = parts.first;
            long_rest = parts.second;
            if (three) {
                auto rest = split_long_lyrics(long_rest);
                if (!rest.second.empty()) long_rest = rest.first + LONG_PART_MARK + rest.second;
            }
        }
        double estimate = estimate_seconds(words, f.instrumental);
        for (int v = 1; v <= f.versions; v++) {
            Song s;
            s.title             = trim(f.title) + (f.versions > 1 ? " (v" + std::to_string(v) + ")" : "");
            s.style             = style;
            s.lyrics            = words;
            s.instrumental      = f.instrumental;
            s.instrumental_lora = f.engine == 0;
            s.cot               = COT_VALUES[f.cot];
            s.adapters          = chosen_loras();
            s.negative_style    = join_style(split_style(f.negative_style));
            if (v == 1) remember_negative_tags(s.negative_style);
            s.negative_scale    = f.negative_scale;
            if (f.fade_structure) s.fade_seconds = structure_fade_seconds(words, f.instrumental);
            s.artist            = trim(f.artist);
            s.album             = trim(f.album);
            s.track             = submit_track > 0 ? std::to_string(submit_track) : parse_title_block(f.title).track;  // album IA : n° de piste
            s.auto_duration     = f.auto_duration;
            if (!f.cover.empty()) {
                s.kind             = "cover";
                s.source_file      = narrow(f.cover.wstring());
                s.parent_title     = narrow(f.cover.filename().wstring());
                s.duration         = std::clamp(std::ceil(f.cover_seconds) + 15, 30.0, 360.0);
                s.expected_seconds = f.cover_seconds;
            } else if (f.auto_duration) {
                // Chanson : plafond maximal, le modèle termine à la fin des paroles.
                // Instrumental : il ne conclut pas seul, le plafond suit la structure (+ fondu si atteint).
                s.duration         = s.instrumental ? estimate : 360;
                s.expected_seconds = estimate;
            } else {
                s.duration         = f.duration;
                s.expected_seconds = std::min<double>(f.duration, estimate);
            }
            if (!long_rest.empty()) {
                s.long_rest     = long_rest;
                s.title        += tr(" (partie 1)");
                s.auto_duration = true;
                s.duration      = s.instrumental ? std::min(360.0, estimate) : 360;
            }
            s.seed = s.lm_seed = f.seed < 0 ? -1 : f.seed + v - 1;
            studio->submit(s, held);
        }
        const std::string quoted = trim(f.title);
        if (submit_quiet) return;  // album IA : un seul message pour tout l'album
        if (f.versions > 1) {
            notify(held ? trf("%d versions de « %s » ajoutées à la file, en pause.", f.versions, quoted.c_str())
                        : trf("%d versions de « %s » ajoutées à la file.", f.versions, quoted.c_str()));
        } else {
            notify(held ? trf("« %s » ajouté à la file, en pause.", quoted.c_str()) : trf("« %s » ajouté à la file.", quoted.c_str()));
        }
    }

    void reuse(const Song & s) {
        Form & f = form;
        f.cover.clear();
        f.cover_seconds = 0;
        set_mode(f, s.instrumental);
        f.title    = s.title;
        f.style    = s.style;
        f.artist   = s.artist;
        f.album    = s.album;
        f.language = detect_language(s.style);
        (s.instrumental ? f.lyrics_inst : f.lyrics_song) = s.lyrics;
        f.engine        = s.instrumental_lora ? 0 : 1;
        f.loras.clear();
        for (const auto & [name, scale] : s.adapters) f.loras.push_back({ name, (float) scale, true });
        scan_loras(true);
        sync_lora_triggers();
        f.fade_structure = s.fade_seconds > 0;
        f.negative_style = s.negative_style;
        f.negative_scale = (float) s.negative_scale;
        f.cot           = s.cot == "melody" ? 1 : s.cot == "off" ? 2 : 0;
        f.duration      = (int) std::min(360.0, s.duration);
        f.auto_duration = s.auto_duration;
        f.seed          = -1;
        if (s.kind == "cover" && fs::exists(widen(s.source_file))) {
            f.cover         = widen(s.source_file);
            f.cover_seconds = media_duration(f.cover);
        }
        f.dirty = true;
        ImGui::ClearActiveID();
        scroll_form_top = true;
    }

    // same_seed : on rejoue le même tirage de bruit acoustique (rendu identique à l'original, mais
    // sans l'écrêtage des anciennes versions) ; sinon, nouvelle graine aléatoire = nouvelle variation.
    void rerender(const Song & s, bool same_seed) {
        Song r;
        r.kind              = "rerender";
        r.parent            = s.id;
        r.parent_title      = s.title;
        r.title             = s.title + tr(" (re-rendu)");
        r.style             = s.style;
        r.lyrics            = s.lyrics;
        r.instrumental      = s.instrumental;
        r.instrumental_lora = s.instrumental_lora;
        r.adapters          = s.adapters;
        r.fade_seconds      = s.fade_seconds;
        r.negative_style    = s.negative_style;  // pour mémoire : un re-rendu ne recompose pas
        r.negative_scale    = s.negative_scale;
        r.cot               = s.cot;
        r.duration          = s.audio_seconds;
        r.artist            = s.artist;
        r.album             = s.album;
        r.track             = s.track;
        r.seed              = same_seed && s.has_used_seeds ? s.used_seed : -1;
        studio->submit(r);
        notify(same_seed ? trf("Re-rendu de « %s » (même graine) ajouté à la file.", s.title.c_str())
                         : trf("Re-rendu de « %s » (graine aléatoire) ajouté à la file.", s.title.c_str()));
    }

    // Nouvelle composition à partir du même style et des mêmes paroles (nouvelle graine) : la seule
    // façon de corriger une composition défectueuse, qu'un re-rendu ne fait que resynthétiser.
    void recompose(const Song & s) {
        Song n;
        std::string title = s.title;
        // Retire les suffixes ajoutés par les re-rendus et recompositions précédents (pas d'empilement).
        for (const std::string suffix : { std::string(tr(" (re-rendu)")), std::string(tr(" (recomposé)")) }) {
            while (title.size() > suffix.size() &&
                   title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0) {
                title.resize(title.size() - suffix.size());
            }
        }
        n.title             = title + tr(" (recomposé)");
        n.style             = s.style;
        n.lyrics            = s.lyrics;
        n.instrumental      = s.instrumental;
        n.instrumental_lora = s.instrumental_lora;
        n.adapters          = s.adapters;
        n.fade_seconds      = s.fade_seconds;
        n.negative_style    = s.negative_style;  // 1.0.46 : la recomposition garde le style à éviter (oubli)
        n.negative_scale    = s.negative_scale;
        n.cot               = s.cot;
        n.artist            = s.artist;
        n.album             = s.album;
        n.track             = s.track;
        n.seed = n.lm_seed  = -1;
        double estimate     = estimate_seconds(s.lyrics, s.instrumental);
        bool   automatic    = s.kind == "rerender" || s.auto_duration;
        n.auto_duration     = automatic;
        if (s.kind == "cover") {
            n.kind             = "cover";
            n.source_file      = s.source_file;
            n.parent_title     = s.parent_title;
            n.duration         = s.duration;
            n.expected_seconds = s.expected_seconds;
        } else if (automatic) {
            n.duration         = s.instrumental ? estimate : 360;
            n.expected_seconds = estimate;
        } else {
            n.duration         = s.duration;
            n.expected_seconds = std::min(s.duration, estimate);
        }
        studio->submit(n);
        notify(trf("Recomposition de « %s » ajoutée à la file.", title.c_str()));
    }

    // ------------------------------------------------------------------ Formulaire : affichage

    void draw_form_toolbar() {
        ImGui::PushFont(g_header);
        ImGui::TextUnformatted(tr("Nouveau morceau"));
        ImGui::PopFont();
        const char * labels[] = { tr("Adapter au format YuE2"), tr("Tout vider") };
        float        w        = 0;
        for (const char * l : labels) w += ImGui::CalcTextSize(l).x + ImGui::GetStyle().FramePadding.x * 2;
        w += ImGui::GetStyle().ItemSpacing.x;
        {   // boutons à droite du titre ; sous le titre quand la place manque (langues à textes longs)
            float title_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
            float want_x    = ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize;
            if (want_x >= title_end + 12 * g_dpi) ImGui::SameLine(want_x);
        }
        if (soft_button(labels[0], P.teal)) {
            adapt(form.title, form.style, form.instrumental ? form.lyrics_inst : form.lyrics_song, form.instrumental);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Convertit le style et les paroles déjà collés (format Suno) en format YuE2."));
        ImGui::SameLine();
        if (danger_button(labels[1])) clear_form();
    }

    // Tempo du style : curseur de 60 à 260 BPM, par pas de 1, sur une barre colorée par mouvement (Largo … Presto).
    // Il lit et écrit la balise « 120 BPM » du style (une seule à la fois) ; sans balise, le tempo est « non défini ».
    static constexpr int TEMPO_MIN = 60, TEMPO_MAX = 260;
    static const char * tempo_name(int bpm) {
        return bpm < 76 ? "Largo" : bpm < 108 ? "Adagio / Andante" : bpm < 120 ? "Moderato" : bpm < 156 ? "Allegro" : "Vivace / Presto";
    }

    // ------------------------------------------------------------------ Traduction des paroles


    struct TranslateJob {
        std::atomic<int>   state{ 0 };  // 0 en cours, 1 terminé
        std::atomic<float> progress{ 0 };
        std::atomic<bool>  cancel{ false };
        TranslateResult    result;
        std::string        target;
        bool               used_web = false;
    };
    std::shared_ptr<TranslateJob>     translate_job;
    std::string                       translate_undo;  // paroles avant la dernière traduction (« Annuler la traduction »)
    std::string                       translate_last;  // paroles produites par la traduction : tant qu'elles sont intactes, on peut annuler
    std::string                       translate_engine;  // « web » ou « local » : moteur de la dernière traduction
    std::shared_ptr<std::atomic<int>> translate_web_state = std::make_shared<std::atomic<int>>(0);  // 0 inconnu, 1 test, 2 en ligne, 3 hors ligne
    LocalTranslator                   local_translator;
    double                            local_scan_at = -100;
    bool                              pending_translate_delete = false;

    fs::path app_dir() const {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return fs::path(buf).parent_path();
    }
    fs::path translate_dir() const { return studio->paths().models_dir / "traduction"; }

    // Recherche du moteur local (llama.cpp + modèle), au plus toutes les 5 secondes.
    void refresh_translator(bool force = false) {
        if (!force && ImGui::GetTime() - local_scan_at < 5) return;
        local_translator = find_local_translator(app_dir(), studio->paths().models_dir);
        local_scan_at    = ImGui::GetTime();
    }

    // Le service Internet répond-il ? Test en tâche de fond (quelques secondes au plus hors ligne).
    void test_translate_web() {
        if (*translate_web_state == 1) return;
        *translate_web_state = 1;
        auto state           = translate_web_state;
        std::thread([state] { *state = translate_web_available() ? 2 : 3; }).detach();
    }

    bool web_translation_possible() const { return settings.translate_web && *translate_web_state != 3; }

    // Langue proposée : par Internet (toutes), ou hors ligne si le modèle local la connaît.
    bool translate_lang_usable(const TranslateLang & l) const {
        return web_translation_possible() || (local_translator.ready() && l.llm);
    }

    void start_translation(const std::string & lyrics) {
        auto job      = std::make_shared<TranslateJob>();
        job->target   = settings.translate_target;
        job->used_web = web_translation_possible();
        translate_job = job;
        LocalTranslator local = local_translator;
        std::thread([job, lyrics, local] {
            job->result = translate_lyrics(lyrics, job->target, job->used_web, local, &job->progress, &job->cancel);
            job->state  = 1;
        }).detach();
    }

    // Fin de la traduction : les paroles sont remplacées (structure intacte) et la langue du style suit.
    void poll_translation() {
        if (!translate_job || translate_job->state != 1) return;
        auto job = translate_job;
        translate_job.reset();
        const TranslateResult & r = job->result;
        if (job->used_web && r.engine == "local") *translate_web_state = 3;  // Internet n'a pas répondu
        if (!r.ok) {
            notify(trf("Traduction impossible : %s", tr(r.error.c_str())));
            return;
        }
        Form & f         = form;
        translate_undo   = f.lyrics_song;
        f.lyrics_song    = r.text;
        translate_last   = r.text;
        translate_engine = r.engine;
        f.cursor         = -1;
        f.dirty          = true;
        ImGui::ClearActiveID();
        const TranslateLang * lang = find_translate_lang(job->target);
        std::string           code = job->target == "zh-CN" ? "zh" : job->target;
        int                   li   = language_index_for_code(code);
        if (lang && lang->yue2 && li > 0) {
            f.language = li;
            f.style    = set_language(f.style, LANGUAGES[li].tag);  // balise de langue du style
        }
        const char * engine = r.engine == "web" ? tr("Internet") : tr("modèle local");
        notify(trf("Paroles traduites en %s (%s).", lang ? lang->name : job->target.c_str(), engine));
        if (r.kept > 0) notify(trf("%d ligne(s) non traduite(s) par le modèle local : relisez les paroles.", r.kept));
        if (lang && !lang->yue2) notify(tr("Attention : YuE2 ne chante pas cette langue, le résultat sera incertain."));
    }

    // Ligne « Traduire vers [langue] [Traduire] » du bloc Paroles.
    void draw_translate_row(std::string & lyrics) {
        draw_translate_row_inner(lyrics);
    }
    void draw_translate_row_inner(std::string & lyrics) {
        Form & f = form;
        refresh_translator();
        if (settings.translate_web && *translate_web_state == 0) test_translate_web();
        const auto &          langs = translate_languages();
        const TranslateLang * cur   = find_translate_lang(settings.translate_target);
        if (!cur) cur = &langs[0];

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Traduire vers"));
        ImGui::SameLine(0, 8 * g_dpi);
        ImGui::SetNextItemWidth(220 * g_dpi);
        if (ImGui::BeginCombo("##tr_lang", cur->name, ImGuiComboFlags_HeightLarge)) {
            ImGui::SeparatorText(tr("Chantées par YuE2"));
            for (const TranslateLang & l : langs) {
                if (!l.yue2) continue;
                ImGui::BeginDisabled(!translate_lang_usable(l));
                if (ImGui::Selectable(l.name, cur == &l)) {
                    settings.translate_target = l.code;
                    save_settings(settings, settings_path);
                }
                ImGui::EndDisabled();
            }
            ImGui::SeparatorText(tr("Autres langues (non chantées par YuE2)"));
            for (const TranslateLang & l : langs) {
                // La liste s'adapte au moteur : sans Internet, seules les langues du modèle local restent.
                if (l.yue2 || !translate_lang_usable(l)) continue;
                if (ImGui::Selectable(l.name, cur == &l)) {
                    settings.translate_target = l.code;
                    save_settings(settings, settings_path);
                }
            }
            if (!web_translation_possible()) {
                ImGui::Separator();
                colored_wrapped(P.muted, tr("Sans Internet : seules les langues du modèle local sont proposées."));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        bool        running = translate_job != nullptr;
        std::string why     = !has_lyrics_text(lyrics)       ? tr("Aucune parole à traduire.")
                            : !translate_lang_usable(*cur)   ? tr("Pas de connexion à Internet et pas de modèle de traduction local (Réglages › Analyse et paroles).")
                                                             : "";
        ImGui::BeginDisabled(running || !why.empty());
        if (accent_button(tr("Traduire"), ImVec2(0, 0), &P.teal)) start_translation(lyrics);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", !why.empty() ? why.c_str()
                                                 : tr("Traduit les lignes chantées ; les balises de section ([Verse], [Chorus]…) et les lignes vides ne bougent pas.\nPar Internet (Google Traduction) si possible, sinon avec le modèle local."));
        }
        ImGui::SameLine();
        if (running) {
            progress_bar(translate_job->progress, ImVec2(140 * g_dpi, ImGui::GetFrameHeight()), "");
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Arrêter"))) translate_job->cancel = true;
        } else if (!translate_undo.empty() && lyrics == translate_last) {
            if (ImGui::SmallButton(tr("Annuler la traduction"))) {
                lyrics = translate_undo;
                translate_undo.clear();
                f.dirty = true;
                ImGui::ClearActiveID();
            }
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", translate_engine == "web" ? tr("traduit par Internet") : tr("traduit par le modèle local"));
        } else {
            int  web   = *translate_web_state;
            bool local = local_translator.ready();
            std::string state = !settings.translate_web ? (local ? tr("hors ligne : modèle local") : tr("Internet désactivé, pas de modèle local"))
                              : web == 3            ? (local ? tr("hors ligne : modèle local") : tr("hors ligne"))
                              : web == 2            ? tr("● Internet")
                                                    : tr("test de la connexion…");
            ImGui::TextColored(P.muted, "%s", state.c_str());
        }
    }

    // Réglages › Analyse et paroles : moteurs de traduction.
    void draw_translate_settings() {
        draw_translate_settings_inner();
    }
    void draw_translate_settings_inner() {
        refresh_translator();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.violet, "%s", tr("Traduction des paroles"));
        ImGui::PopFont();
        muted_wrapped(tr("Le bouton « Traduire » du bloc Paroles traduit les lignes chantées dans une autre langue sans toucher aux balises de section. Par Internet : Google Traduction (gratuit, plus de 100 langues ; le texte des paroles est envoyé à Google). Hors ligne : modèle local Qwen2.5-3B (environ 2 Go, exécuté par le processeur pour laisser la carte libre, une vingtaine de langues, 20 s à 1 min par morceau)."));
        if (ImGui::Checkbox(tr("Utiliser Internet (Google Traduction)"), &settings.translate_web)) {
            save_settings(settings, settings_path);
            if (settings.translate_web) test_translate_web();
        }
        // État d'Internet
        ImGui::AlignTextToFramePadding();
        int web = *translate_web_state;
        ImGui::TextColored(P.muted, "%s", tr("Internet :"));
        ImGui::SameLine();
        if (web == 2) ImGui::TextColored(P.ok, "%s", tr("● disponible"));
        else if (web == 3) ImGui::TextColored(P.warn, "%s", tr("● injoignable"));
        else if (web == 1) ImGui::TextColored(P.muted, "%s", tr("test…"));
        else ImGui::TextColored(P.muted, "%s", tr("non testé"));
        ImGui::SameLine(0, 12 * g_dpi);
        ImGui::BeginDisabled(web == 1);
        if (ImGui::SmallButton(tr("Tester"))) test_translate_web();
        ImGui::EndDisabled();
        // Moteur local : programme llama.cpp
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Moteur local (llama.cpp) :"));
        ImGui::SameLine();
        if (!local_translator.exe.empty()) ImGui::TextColored(P.ok, "%s", tr("● installé"));
        else ImGui::TextColored(P.warn, "%s", tr("absent (dossier « outils\\llama » de l'application)"));
        // Modèle local
        const TranslateModelFile & mf  = translate_model_file();
        auto                       job = downloader.find("translate");
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Modèle local :"));
        ImGui::SameLine();
        if (job && job->state == 0) {
            unsigned long long dd = job->done, tt = std::max<unsigned long long>(1, job->total);
            progress_bar((float) ((double) dd / (double) tt), ImVec2(200 * g_dpi, 0));
            ImGui::SameLine();
            if (ImGui::SmallButton("✕##tr_cancel")) job->cancel = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Annuler le téléchargement"));
        } else if (!local_translator.model.empty()) {
            std::error_code ec;
            ImGui::TextColored(P.ok, "%s", tr("● installé"));
            ImGui::SameLine(0, 8 * g_dpi);
            ImGui::TextColored(P.muted, "%s", fmt_mb(fs::file_size(local_translator.model, ec)).c_str());
            ImGui::SameLine(0, 12 * g_dpi);
            if (delete_button(tr("Supprimer le modèle de traduction du disque"))) pending_translate_delete = true;
        } else {
            if (accent_button(tr("Télécharger le modèle de traduction"), ImVec2(0, 0), &P.violet)) {
                std::vector<DlFile> files = { { mf.url, mf.name, mf.bytes } };
                std::error_code     ec;
                fs::create_directories(translate_dir(), ec);
                auto j = downloader.start("translate", tr("Traduction des paroles"), translate_dir(), files);
                if (std::find(tracked_downloads.begin(), tracked_downloads.end(), j) == tracked_downloads.end()) tracked_downloads.push_back(j);
                announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), j.get()), announced_downloads.end());
                notify(tr("Téléchargement du modèle de traduction lancé."));
                local_scan_at = -100;
            }
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", trf("environ %s", fmt_mb(mf.bytes).c_str()).c_str());
            if (job && job->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", job->error.c_str()).c_str());
        }
        muted_wrapped(tr("Modèle Qwen2.5-3B-Instruct (Alibaba Cloud), licence Qwen Research : usage personnel et non commercial."));
    }

    bool has_negative_tag(const std::string & tag) const {
        for (const std::string & t : settings.negative_tags) if (lower(t) == lower(tag)) return true;
        return false;
    }

    // Ajoute aux balises rapides les termes du champ qui n'y sont pas encore (casse ignorée), puis enregistre.
    void remember_negative_tags(const std::string & field) {
        bool added = false;
        for (const std::string & t : split_style(field)) {
            std::string tag = trim(t);
            if (tag.empty() || tag.size() > 48 || has_negative_tag(tag)) continue;
            settings.negative_tags.push_back(tag);
            added = true;
        }
        if (added) save_settings(settings, settings_path);
    }

    // Balises rapides négatives, chacune suivie d'une croix (suppression après confirmation).
    // field : champ du formulaire (un clic sur la balise l'ajoute ou la retire) ; nullptr dans les Réglages.
    // owner : 0 formulaire, 1 Réglages (la fenêtre de confirmation s'ouvre là où l'on a cliqué).
    bool draw_negative_chips(std::string * field, int owner) {
        std::vector<std::string> tags  = field ? split_tags(*field) : std::vector<std::string>();
        ImGuiStyle &             st    = ImGui::GetStyle();
        float                    avail = ImGui::GetContentRegionAvail().x, x = 0;
        float                    cross = ImGui::CalcTextSize("×").x + st.FramePadding.x * 2;
        bool                     changed = false;
        const ImVec4 &           color   = P.warn;
        if (settings.negative_tags.empty()) colored_wrapped(P.muted, tr("Aucune balise rapide : tapez un terme dans le champ pour l'ajouter."));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        for (size_t i = 0; i < settings.negative_tags.size(); i++) {
            const std::string & tag = settings.negative_tags[i];
            auto        it   = std::find_if(tags.begin(), tags.end(), [&](const std::string & t) { return lower(t) == lower(tag); });
            bool        on   = field && it != tags.end();
            std::string text = (on ? "✓ " : "") + tag;
            float       w    = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2 + 1 * g_dpi + cross;
            if (i > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
                ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
                x += st.ItemSpacing.x * 0.6f + w;
            } else {
                x = w;
            }
            ImGui::PushID((int) i);
            ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, on ? 0.55f : 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, on ? 0.65f : 0.28f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Text, on ? P.accent_ink : color);
            if (ImGui::SmallButton(text.c_str()) && field) {
                if (on) tags.erase(it);
                else tags.push_back(tag);
                changed = true;
            }
            ImGui::SameLine(0, 1 * g_dpi);
            if (ImGui::SmallButton("×")) {
                neg_pending_delete = tag;
                neg_delete_owner   = owner;
            }
            bool cross_hovered = ImGui::IsItemHovered();
            ImGui::PopStyleColor(4);
            // Bulle après PopStyleColor : sinon elle reprend la couleur de texte de la balise (illisible).
            if (cross_hovered) ImGui::SetTooltip("%s", tr("Supprimer cette balise rapide"));
            ImGui::PopID();
        }
        ImGui::PopStyleVar(2);
        if (changed) *field = join_style(tags);

        // Confirmation, pour éviter une suppression par erreur de clic.
        const char * title = tr("Supprimer la balise rapide ?");
        if (!neg_pending_delete.empty() && neg_delete_owner == owner && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
        ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", trf("La balise « %s » sera retirée des balises rapides (le champ « Style à éviter » n'est pas modifié).",
                                         neg_pending_delete.c_str()).c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                neg_pending_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                auto & list = settings.negative_tags;
                list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string & t) { return lower(t) == lower(neg_pending_delete); }), list.end());
                save_settings(settings, settings_path);
                neg_pending_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    // Réglages › Génération : gestion des balises rapides négatives.
    void draw_negative_tags_settings() {
        draw_negative_tags_settings_inner();
    }
    void draw_negative_tags_settings_inner() {
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.warn, "%s", tr("Balises rapides « Style à éviter »"));
        ImGui::PopFont();
        muted_wrapped(tr("Balises proposées sous le champ « Style à éviter » du formulaire. Un terme tapé dans ce champ est ajouté ici automatiquement ; la croix d'une balise la supprime (après confirmation)."));
        draw_negative_chips(nullptr, 1);
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        float add_w = ImGui::CalcTextSize(tr("Ajouter")).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::min(320 * g_dpi, ImGui::GetContentRegionAvail().x - add_w));
        bool enter = tx::InputTextWithHint("##neg_new", tr("Nouvelle balise (ex. : brass section)"), &neg_new_tag, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trim(neg_new_tag).empty());
        if ((ImGui::Button(tr("Ajouter")) || enter) && !trim(neg_new_tag).empty()) {
            remember_negative_tags(neg_new_tag);  // accepte aussi plusieurs termes séparés par des virgules
            neg_new_tag.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Rétablir la liste par défaut"))) {
            settings.negative_tags = default_negative_tags();
            save_settings(settings, settings_path);
        }
    }

    // Balises négatives (expérimental) : style à éviter, avec quelques suggestions et la force du guidage.
    // Le moteur yue2.cpp (modifié) compose en parallèle une version guidée par ces balises et s'en éloigne.
    void draw_negative_style() {
        draw_negative_style_inner();
    }
    void draw_negative_style_inner() {
        Form & f = form;
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Style à éviter"));
        ImGui::SameLine(0, 8 * g_dpi);
        ImGui::TextColored(P.amber, "%s", tr("expérimental"));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Balises que le morceau doit éviter (instruments, effets de voix…), séparées par des virgules.\nYuE2 n'a pas été entraîné pour cela : l'effet est à juger à l'oreille.\nQuand ce champ est rempli, la composition est un peu plus lente (environ ×1,3) et demande environ 2,7 Go de mémoire vidéo en plus."));
        }
        // Champ d'une ligne (comme input_line), mais on repère la fin de saisie pour mémoriser les nouveaux termes.
        float clear_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 0.5f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear_w);
        if (ImGui::InputTextWithHint("##neg_style", tr("Ex. : vocoder, autotune, synthesizer"), &f.negative_style)) f.dirty = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) remember_negative_tags(f.negative_style);
        if (text_context_menu(f.negative_style)) f.dirty = true;
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        f.dirty |= clear_button("##neg_style", f.negative_style);
        f.dirty |= draw_negative_chips(&f.negative_style, 0);
        if (!trim(f.negative_style).empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", tr("Force"));
            ImGui::SameLine(0, 10 * g_dpi);
            ImGui::SetNextItemWidth(std::min(260 * g_dpi, ImGui::GetContentRegionAvail().x));
            f.dirty |= ImGui::SliderFloat("##neg_scale", &f.negative_scale, 1.2f, 3.0f, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr("1,5 conseillé. Plus fort, le morceau s'éloigne davantage du style à éviter, mais peut perdre en naturel."));
            }
            ImGui::SameLine(0, 10 * g_dpi);
            ImGui::TextColored(P.muted, "%s", tr("composition ≈ ×1,3, +2,7 Go de mémoire vidéo"));
        }
    }

    bool draw_tempo_slider(std::string & style) {
        std::vector<std::string> tags = split_tags(style);
        int                      bpm  = 0;
        for (const std::string & t : tags) {
            if (is_bpm_tag(t)) {
                bpm = atoi(t.c_str());
                break;
            }
        }
        const bool unset = bpm <= 0;
        int        value = unset ? 120 : std::clamp(bpm, TEMPO_MIN, TEMPO_MAX);
        bool       changed = false;

        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Tempo"));
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::PushFont(g_bold);
        if (unset) ImGui::TextColored(P.muted, "%s", tr("non défini"));
        else ImGui::TextColored(P.accent, "%d BPM", bpm);
        ImGui::PopFont();
        if (!unset) {
            ImGui::SameLine(0, 10 * g_dpi);
            ImGui::TextColored(P.muted, "%s", tempo_name(value));
            ImGui::SameLine(0, 10 * g_dpi);
            if (ImGui::SmallButton(tr("Retirer le tempo"))) {
                tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
                style   = join_style(tags);
                changed = true;
            }
        }
        // barre des mouvements, alignée sur la piste du curseur
        float  pad = 8 * g_dpi, x0 = ImGui::GetCursorScreenPos().x + pad, x1 = x0 + ImGui::GetContentRegionAvail().x - 2 * pad;
        float  y = ImGui::GetCursorScreenPos().y + 2 * g_dpi, h = 6 * g_dpi;
        struct Zone { int from; const ImVec4 * color; const char * label; };
        const Zone zones[] = { { 60, &P.blue, "Largo" }, { 76, &P.teal, "Andante" }, { 108, &P.green, "Moderato" },
                               { 120, &P.amber, "Allegro" }, { 156, &P.orange, "Presto" } };
        ImDrawList * dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < 5; i++) {
            int   to  = i < 4 ? zones[i + 1].from : TEMPO_MAX;
            float a   = x0 + (x1 - x0) * (zones[i].from - TEMPO_MIN) / (float) (TEMPO_MAX - TEMPO_MIN);
            float b   = x0 + (x1 - x0) * (to - TEMPO_MIN) / (float) (TEMPO_MAX - TEMPO_MIN);
            bool  cur = !unset && value >= zones[i].from && value <= to;
            dl->AddRectFilled(ImVec2(a + (i ? 1 : 0), y), ImVec2(b - 1, y + h), ImGui::GetColorU32(alpha(*zones[i].color, cur ? 0.95f : 0.45f)), 3 * g_dpi);
        }
        ImGui::Dummy(ImVec2(0, h + 4 * g_dpi));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, unset ? P.muted : P.accent);
        int edited = value;
        if (ImGui::SliderInt("##tempo", &edited, TEMPO_MIN, TEMPO_MAX, "")) {
            tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm_tag), tags.end());
            tags.push_back(std::to_string(edited) + " BPM");
            style   = join_style(tags);
            changed = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Tempo du morceau, de 60 à 260 BPM par pas de 1 : ajoute la balise « BPM » au style. Ctrl+clic pour saisir un nombre."));
        }
        // repères sous le curseur
        const char * marks[] = { "60", "110", "160", "210", "260" };
        float        ly      = ImGui::GetCursorScreenPos().y;
        for (int i = 0; i < 5; i++) {
            float w = ImGui::CalcTextSize(marks[i]).x;
            float x = x0 + (x1 - x0) * i / 4.f - (i == 0 ? 0 : i == 4 ? w : w / 2);
            dl->AddText(ImVec2(x, ly), ImGui::GetColorU32(P.muted), marks[i]);
        }
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 2 * g_dpi));
        return changed;
    }

    void draw_song_section() {
        Form & f = form;
        std::string summary_song = (trim(f.title).empty() ? std::string(tr("(sans titre)")) : trim(f.title)) +
                                   (f.instrumental ? tr("  ·  instrumental") : tr("  ·  chanson"));
        if (!begin_section("sec_morceau", tr("Morceau"), P.blue, nullptr, ellipsize(summary_song, 520 * g_dpi))) {
            end_section();
            return;
        }
        f.dirty |= input_line("##title", tr("Nom du morceau (ex. : 01. City Lights)"), f.title);
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        f.dirty |= input_line("##artist", tr("Groupe ou chanteur"), f.artist, half);
        ImGui::SameLine();
        f.dirty |= input_line("##album", tr("Album"), f.album);

        // Type et reprise sur une ligne
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, tr("Type"));
        for (int i = 0; i < 2; i++) {
            ImGui::SameLine(0, i ? 4 * g_dpi : 10 * g_dpi);
            bool         selected = f.instrumental == (i == 1);
            const char * text     = i ? tr("Instrumental") : tr("Chanson");
            if (selected ? accent_button(text, ImVec2(0, 0), &P.blue) : ImGui::Button(text)) {
                if (!selected) set_mode(f, i == 1);
            }
        }
        ImGui::SameLine(0, 18 * g_dpi);
        ImGui::TextColored(P.muted, tr("Reprise"));
        ImGui::SameLine();
        if (!studio->can_transcribe()) {
            ImGui::TextColored(P.muted, tr("modèles de transcription absents"));
        } else if (f.cover.empty()) {
            if (ImGui::Button(tr("Choisir un fichier audio…"))) pick_cover();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Facultatif : réinterpréter un morceau existant dans le style choisi."));
        } else {
            std::string name = narrow(f.cover.filename().wstring()) + "  (" + fmt_time(f.cover_seconds) + ")";
            pill(name.c_str(), P.blue);
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Retirer"))) {
                f.cover.clear();
                f.cover_seconds = 0;
                f.cot           = COT_FASTEST;
                if (analysis) analysis->cancel = true;
                analysis.reset();
            }
        }
        if (!f.cover.empty()) {
            muted_wrapped(f.instrumental ? tr("La partition (mélodie et accords) sera transcrite puis rejouée dans le style choisi.")
                                         : tr("La mélodie sera transcrite puis chantée dans le style choisi : écrivez les paroles à chanter."));
            draw_analysis_panel();
        }
        end_section();
    }

    void draw_style_section() {
        Form & f = form;
        if (!begin_section("sec_style", tr("Style"), P.violet, tr("balises en anglais, séparées par des virgules"),
                           ellipsize(trim(f.style).empty() ? std::string(tr("(vide)")) : f.style, 520 * g_dpi))) {
            end_section();
            return;
        }
        {   // présentation du bloc : classique, pastilles, constructeur
            static const char * modes[] = { N_("Classique"), N_("Pastilles"), N_("Constructeur") };
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, tr("Présentation"));
            for (int i = 0; i < 3; i++) {
                ImGui::SameLine(0, 6 * g_dpi);
                ImGui::PushID(i);
                bool chosen = settings.style_mode == i;
                if (chosen ? accent_button(tr(modes[i]), ImVec2(0, 0), &P.violet) : ImGui::SmallButton(tr(modes[i]))) {
                    settings.style_mode = i;
                    save_settings(settings, settings_path);
                }
                ImGui::PopID();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(tr("Classique : onglets de balises.\nPastilles : vos balises en pastilles retirables, catégories repliables.\nConstructeur : une liste par catégorie (genres, ambiance, voix, instruments, tempo)."));
            }
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        }

        // Langue · profil d'influence · préréglage
        float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
        ImGui::SetNextItemWidth(third);
        std::string lang = f.language ? trf("Langue : %s", tr(LANGUAGES[f.language].label)) : std::string(tr(LANGUAGES[0].label));
        bool lang_open = ImGui::BeginCombo("##language", lang.c_str());
        if (lang_open) {
            for (int i = 0; i < (int) LANGUAGES.size(); i++) {
                if (ImGui::Selectable(tr(LANGUAGES[i].label), f.language == i)) {
                    f.language        = i;
                    f.style           = set_language(f.style, LANGUAGES[i].tag);
                    settings.language = i;
                    save_settings(settings, settings_path);
                    f.dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Langue placée en tête du style. YuE2 chante surtout l'anglais :\nles autres langues (français, espagnol, italien, allemand, portugais, japonais, coréen,\nmandarin, cantonais) sont possibles mais moins naturelles."));
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(third);
        bool influence_open = ImGui::BeginCombo("##influence", tr("Profil d'influence…"), ImGuiComboFlags_HeightLarge);
        if (influence_open) {
            for (const Influence & inf : INFLUENCES) {
                if (ImGui::Selectable(inf.name)) {
                    f.style = set_language(influence_style(inf), LANGUAGES[f.language].tag);
                    f.dirty = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", influence_style(inf).c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const Preset * current = nullptr;
        for (const Preset & p : settings.presets) {
            if (!f.style.empty() && p.style == f.style) current = &p;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string preview = current ? trf("Préréglage : %s", current->name.c_str())
                            : settings.presets.empty() ? tr("Aucun préréglage") : tr("Préréglages…");
        bool presets_open = ImGui::BeginCombo("##presets", preview.c_str(), ImGuiComboFlags_HeightLarge);
        if (presets_open) {
            std::vector<std::string> groups = pack_groups(true);
            for (const std::string & group : groups) {
                // Titre de groupe seulement quand des packs sont installés (sinon la liste reste comme avant)
                if (!group.empty()) ImGui::SeparatorText(group.c_str());
                else if (groups.size() > 1) ImGui::SeparatorText(tr("Mes styles"));
                for (size_t i = 0; i < settings.presets.size(); i++) {
                    const Preset & p = settings.presets[i];
                    if (p.pack != group) continue;
                    std::string entry = p.name + (p.loras.empty() ? "" : tr("  · LoRA")) + (p.structure_song.empty() && p.structure_inst.empty() ? "" : tr("  · structure")) +
                                        (p.has_negative && !p.negative_style.empty() ? tr("  · à éviter") : "");
                    ImGui::PushID((int) i);
                    if (ImGui::Selectable(entry.c_str(), current == &p)) apply_preset(p);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset_summary(p).c_str());
                    ImGui::PopID();
                }
            }
            ImGui::Separator();
            ImGui::BeginDisabled(trim(f.style).empty());
            if (ImGui::Selectable(tr("Enregistrer le style actuel…"))) {
                preset_name = current ? current->name : "";
                open_preset = true;
            }
            ImGui::EndDisabled();
            if (current && ImGui::Selectable(trf("Supprimer « %s »", current->name.c_str()).c_str())) {
                std::string name = current->name;
                settings.presets.erase(settings.presets.begin() + (current - settings.presets.data()));
                save_settings(settings, settings_path);
                notify(trf("Préréglage « %s » supprimé.", name.c_str()));
            }
            ImGui::EndCombo();
        }

        // Champ de style sur plusieurs lignes, avec ascenseur. Retour à la ligne « doux » : l'espace qui suit
        // une virgule devient un saut de ligne à l'affichage (même longueur, le curseur ne bouge pas).
        float   clear_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 0.5f;
        float   field_w = ImGui::GetContentRegionAvail().x - clear_w;
        ImGuiID style_id = ImGui::GetID("##style");
        if (ImGui::GetActiveID() != style_id) {
            style_view = wrap_style(f.style, field_w - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ScrollbarSize);
        }
        bool style_changed = ImGui::InputTextMultiline(
            "##style", &style_view,
            ImVec2(field_w, ImGui::GetTextLineHeight() * 3.6f + ImGui::GetStyle().FramePadding.y * 2));
        style_changed |= text_context_menu(style_view);
        if (style_changed) {
            f.style = style_view;
            std::replace(f.style.begin(), f.style.end(), '\n', ' ');
            f.dirty = true;
        }
        if (f.style.empty()) {
            // indice affiché dans le champ vide
            ImVec2 min = ImGui::GetItemRectMin() + ImGui::GetStyle().FramePadding;
            ImGui::GetWindowDrawList()->AddText(min, ImGui::GetColorU32(P.muted),
                                                tr("Ex. : French, grunge, 90s, melancholic, 110 BPM, distorted guitar, raspy male vocal"));
        }
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        f.dirty |= clear_button("##style_clear", f.style);
        f.dirty |= draw_tempo_slider(f.style);
        draw_negative_style();

        // Balises par catégorie, chacune à sa couleur
        auto group_of = [&](const std::string & tag) -> const ChipGroup * {
            for (const ChipGroup & g : STYLE_GROUPS) {
                for (const char * item : g.items) if (lower(item) == lower(tag)) return &g;
            }
            return nullptr;
        };
        if (settings.style_mode == 1) {
            // Pastilles : les balises du style, retirables ; puis les catégories repliables
            std::vector<std::string> tags = split_tags(f.style);
            if (!tags.empty()) {
                ImGui::Dummy(ImVec2(0, 2 * g_dpi));
                ImGuiStyle & st    = ImGui::GetStyle();
                float        avail = ImGui::GetContentRegionAvail().x, x = 0;
                int          remove = -1;
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                for (size_t i = 0; i < tags.size(); i++) {
                    const ChipGroup * g     = group_of(tags[i]);
                    ImVec4            color = g ? category_color(g->color) : P.muted;
                    std::string       text  = tags[i] + "  ✕";
                    float             w     = ImGui::CalcTextSize(text.c_str()).x + st.FramePadding.x * 2;
                    if (i > 0 && x + st.ItemSpacing.x * 0.6f + w <= avail) {
                        ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
                        x += st.ItemSpacing.x * 0.6f + w;
                    } else {
                        x = w;
                    }
                    ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, 0.30f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.65f));
                    ImGui::PushStyleColor(ImGuiCol_Text, P.text);
                    ImGui::PushID((int) i);
                    if (ImGui::SmallButton(text.c_str())) remove = (int) i;
                    ImGui::PopID();
                    ImGui::PopStyleColor(4);
                }
                ImGui::PopStyleVar(2);
                if (remove >= 0) {
                    tags.erase(tags.begin() + remove);
                    f.style = join_style(tags);
                    f.dirty = true;
                }
                ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            }
            bool first = true;
            for (const ChipGroup & group : STYLE_GROUPS) {
                ImVec4 color = category_color(group.color);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                bool open = ImGui::CollapsingHeader(tr(group.name), first ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                ImGui::PopStyleColor();
                first = false;
                if (open) {
                    f.dirty |= style_chips(group.items, f.style, color);
                    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
                }
            }
        } else if (settings.style_mode == 2) {
            // Constructeur : une liste par catégorie, qui coche et décoche les balises du style
            if (ImGui::BeginTable("##builder", 2, ImGuiTableFlags_SizingStretchSame)) {
                for (const ChipGroup & group : STYLE_GROUPS) {
                    ImGui::TableNextColumn();
                    ImVec4                   color = category_color(group.color);
                    std::vector<std::string> tags  = split_tags(f.style), chosen;
                    for (const std::string & t : tags) {
                        for (const char * item : group.items) {
                            if (lower(item) == lower(t)) chosen.push_back(t);
                        }
                    }
                    std::string label = tr(group.name);
                    label += " : ";
                    if (chosen.empty()) label += "—";
                    for (size_t i = 0; i < chosen.size() && i < 3; i++) label += (i ? ", " : "") + chosen[i];
                    if (chosen.size() > 3) label += " +" + std::to_string(chosen.size() - 3);
                    label += "  ▾";
                    ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, chosen.empty() ? 0.10f : 0.25f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.38f));
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::PushID(group.name);
                    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##choices");
                    ImGui::SetNextWindowSizeConstraints(ImVec2(420 * g_dpi, 0), ImVec2(520 * g_dpi, 420 * g_dpi));
                    if (ImGui::BeginPopup("##choices")) {
                        ImGui::TextColored(color, "%s", tr(group.name));
                        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
                        f.dirty |= style_chips(group.items, f.style, color);
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor(3);
                }
                ImGui::EndTable();
            }
        } else if (ImGui::BeginTabBar("style_groups")) {
            for (const ChipGroup & group : STYLE_GROUPS) {
                ImVec4 color = category_color(group.color);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::PushStyleColor(ImGuiCol_TabSelected, alpha(color, 0.22f));
                ImGui::PushStyleColor(ImGuiCol_TabHovered, alpha(color, 0.16f));
                ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, color);
                bool open = ImGui::BeginTabItem(tr(group.name));
                ImGui::PopStyleColor(4);
                if (open) {
                    ImGui::Dummy(ImVec2(0, 1 * g_dpi));
                    f.dirty |= style_chips(group.items, f.style, color);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        end_section();
    }

    void draw_lyrics_section() {
        Form &        f      = form;
        std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
        std::string tags_csv = structure_tags(lyrics);
        size_t      sections = tags_csv.empty() ? 0 : std::count(tags_csv.begin(), tags_csv.end(), ',') + 1;
        std::string summary_lyrics = trf("%d section(s)", (int) sections) +
                                     (f.instrumental || !has_lyrics_text(lyrics) ? "" : tr("  ·  paroles écrites"));
        if (!begin_section("sec_paroles", f.instrumental ? tr("Structure") : tr("Paroles"), P.teal,
                           f.instrumental ? tr("sections uniquement, sans paroles") : tr("une balise par section"), summary_lyrics)) {
            end_section();
            return;
        }
        float clear_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 0.5f;
        float combo_w = ImGui::GetContentRegionAvail().x - clear_w;
        ImGui::SetNextItemWidth(combo_w);
        // Liste de la largeur du champ : sans cette limite, elle s'élargissait à tout l'écran (croix placée en bout de ligne)
        ImGui::SetNextWindowSizeConstraints(ImVec2(combo_w, 0), ImVec2(combo_w, FLT_MAX));
        if (ImGui::BeginCombo("##structure", tr("Appliquer une structure type…"), ImGuiComboFlags_HeightLarge)) {
            // Applique un texte de structure ; si des paroles écrites seraient perdues, on demande confirmation.
            auto apply = [&](const std::string & text, const std::string & name) {
                if (f.instrumental || !has_lyrics_text(lyrics)) {
                    lyrics  = text;
                    f.dirty = true;
                } else {
                    f.pending_structure = true;
                    f.pending_text      = text;
                    f.pending_name      = name;
                }
            };
            for (int i = 0; i < IM_ARRAYSIZE(STRUCTURES); i++) {
                if (ImGui::Selectable(tr(STRUCTURES[i].name))) apply(structure_text(STRUCTURES[i], f.instrumental), tr(STRUCTURES[i].name));
            }
            // Structures personnelles (« Mes structures »), puis celles de chaque pack importé
            for (const std::string & group : pack_groups(false)) {
                ImGui::SeparatorText(group.empty() ? tr("Mes structures") : group.c_str());
                for (size_t i = 0; i < settings.structures.size(); i++) {
                    const UserStructure & st = settings.structures[i];
                    if (st.pack != group) continue;
                    ImGui::PushID((int) i);
                    float x_w = ImGui::GetFrameHeight();
                    if (ImGui::Selectable(st.name.c_str(), false, ImGuiSelectableFlags_AllowOverlap,
                                          ImVec2(ImGui::GetContentRegionAvail().x - x_w, 0))) {
                        apply(skeleton_text(st.sections, f.instrumental), st.name);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", arrow_text(st.sections).c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("×")) {  // suppression, après confirmation
                        pending_structure_delete = (int) i;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Supprimer cette structure"));
                    ImGui::PopID();
                }
            }
            ImGui::Separator();
            ImGui::BeginDisabled(tags_csv.empty());
            if (ImGui::Selectable(tr("Enregistrer la structure actuelle…"))) {
                structure_to_save = tags_csv;
                structure_name.clear();
                open_structure = true;
            }
            ImGui::EndDisabled();
            if (tags_csv.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", tr("Ajoutez d'abord des sections ([Verse], [Chorus]…) dans le champ ci-dessous."));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        f.dirty |= clear_button("##lyrics_clear", lyrics);

        if (!f.instrumental) draw_translate_row(lyrics);

        int tag = f.instrumental ? tag_row(INST_TAGS, IM_ARRAYSIZE(INST_TAGS), P.teal)
                                 : tag_row(SONG_TAGS, IM_ARRAYSIZE(SONG_TAGS), P.teal);
        if (tag >= 0) {
            std::string t      = f.instrumental ? INST_TAGS[tag] : SONG_TAGS[tag];
            size_t      pos    = f.cursor < 0 ? lyrics.size() : std::min((size_t) f.cursor, lyrics.size());
            std::string before = lyrics.substr(0, pos);
            std::string gap    = f.instrumental ? "\n" : "\n\n";
            std::string insert = t + "\n";
            if (!before.empty() &&
                before.compare(before.size() - std::min(before.size(), gap.size()), gap.size(), gap) != 0) {
                insert = (before.back() == '\n' ? gap.substr(1) : gap) + insert;
            }
            lyrics.insert(pos, insert);
            f.cursor = (int) (pos + insert.size());
            f.dirty  = true;
        }
        float height = (f.instrumental ? 160.f : 300.f) * g_dpi;
        f.dirty |= ImGui::InputTextMultiline("##lyrics", &lyrics, ImVec2(-FLT_MIN, height),
                                             ImGuiInputTextFlags_CallbackAlways, lyrics_callback, &f);
        f.dirty |= text_context_menu(lyrics);
        end_section();
    }

    void draw_lora_section() {
        Form & f = form;
        scan_loras();
        int active_count = 0;
        for (const LoraUse & u : f.loras) active_count += u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE;
        if (!begin_section("sec_lora", tr("LoRA"), P.pink, tr("optionnelles, cumulables, une force par LoRA"),
                           active_count ? trf("%d LoRA active(s)", active_count) : std::string(tr("aucune LoRA")))) {
            end_section();
            return;
        }
        std::vector<std::string> usable;
        for (const auto & [file, size] : lora_files) {
            if (file != INSTRUMENTAL_FILE) usable.push_back(file);
        }
        if (usable.empty()) {
            muted_wrapped(tr("Aucune LoRA installée. Téléchargez-en dans Réglages > LoRA : elles changent le style, la voix ou l'ambiance du morceau."));
            if (ImGui::SmallButton(tr("Ouvrir les réglages"))) open_settings = true;
        } else {
            int active = 0;
            for (const LoraUse & u : f.loras) active += u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE;
            // Cartes : case + nom + type, description sur 2 lignes, force (ou taille et mot déclencheur)
            float line_h = ImGui::GetTextLineHeight();
            float gap    = ImGui::GetStyle().ItemSpacing.x;
            float inner  = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
            int   cols   = std::max(1, (int) (inner / (250 * g_dpi)));
            float card_w = std::floor((inner - gap * (cols - 1)) / cols);
            float card_h = 2 * line_h + 2 * ImGui::GetFrameHeight() + 40 * g_dpi;
            float rows   = (float) std::min<size_t>((usable.size() + cols - 1) / cols, 3);
            ImGui::BeginChild("##lora_cards", ImVec2(0, rows * (card_h + ImGui::GetStyle().ItemSpacing.y) + 4 * g_dpi), ImGuiChildFlags_None);
            int index = 0;
            for (const std::string & file : usable) {
                if (index % cols) ImGui::SameLine(0, gap);
                index++;
                ImGui::PushID(file.c_str());
                LoraUse * use = find_lora_use(file);
                bool      on  = use && use->enabled;
                LoraRef   r   = lora_ref(file);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, on ? tint(P.card, P.accent, 0.14f) : P.card);
                ImGui::PushStyleColor(ImGuiCol_Border, on ? P.accent : P.line);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * g_dpi, 7 * g_dpi));
                ImGui::BeginChild("##card", ImVec2(card_w, card_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
                if (ImGui::Checkbox("##on", &on)) set_lora(file, on);
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                const char * kind = !r.entry ? custom_kind(custom_lora(file)) : !strcmp(r.entry->kind, "style") ? tr("Style") : !strcmp(r.entry->kind, "artist") ? tr("Artiste")
                                  : !strcmp(r.entry->kind, "slider") ? tr("Curseur") : !strcmp(r.entry->kind, "instrument") ? tr("Instrument") : tr("Compos.");
                float pill_w = pill_width(kind);
                ImGui::PushFont(g_bold);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - pill_w - 8 * g_dpi);
                ImGui::TextUnformatted(custom_label(file).c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                ImGui::SameLine(ImGui::GetContentRegionMax().x - pill_w);
                pill(kind, on ? P.accent : P.muted);

                const CustomLora * cl = r.entry ? nullptr : custom_lora(file);
                std::string desc = r.entry ? std::string(tr(r.entry->description)) : cl ? custom_description(*cl) : std::string(tr("LoRA personnelle déposée dans modeles\\lora."));
                float       wrap = ImGui::GetContentRegionAvail().x;
                std::string shown = desc;
                if (ImGui::CalcTextSize(shown.c_str(), nullptr, false, wrap).y > 2 * line_h + 1) {  // 2 lignes au plus
                    std::string cut;
                    size_t      pos = 0;
                    while (pos < desc.size()) {
                        size_t next = desc.find(' ', pos + 1);
                        if (next == std::string::npos) next = desc.size();
                        std::string trial = desc.substr(0, next) + "…";
                        if (ImGui::CalcTextSize(trial.c_str(), nullptr, false, wrap).y > 2 * line_h + 1) break;
                        cut = trial;
                        pos = next;
                    }
                    shown = cut.empty() ? desc : cut;
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
                ImGui::TextColored(P.muted, "%s", shown.c_str());
                ImGui::PopTextWrapPos();
                // la description occupe toujours deux lignes : la force reste alignée d'une carte à l'autre
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (shown.find('\n') == std::string::npos &&
                                     ImGui::CalcTextSize(shown.c_str(), nullptr, false, wrap).y < 1.5f * line_h ? line_h : 0));
                float lo = r.entry ? r.entry->range_min : 0.f, hi = r.entry ? r.entry->range_max : 2.f;
                if (on && use) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::SliderFloat("##force", &use->scale, lo, hi, tr("force %.2f"))) f.dirty = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Intensité de la LoRA (1,0 = réglage conseillé en général)."));
                } else {
                    unsigned long long size = lora_files.count(file) ? lora_files[file] : 0;
                    std::string        info = fmt_mb(size);
                    std::string word = lora_trigger(file);
                    info += word.empty() ? std::string(tr("  ·  sans mot déclencheur")) : std::string(tr("  ·  mot : ")) + word;
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(P.muted, "%s", info.c_str());
                }
                ImGui::EndChild();
                if (ImGui::IsItemHovered()) {
                    std::string tip = trf("%s\nFichier : %s", desc.c_str(), file.c_str());
                    std::string word = lora_trigger(file);
                    tip += word.empty() ? std::string(tr("\nSans mot déclencheur : la LoRA est quand même appliquée à la génération."))
                                        : trf("\nMot déclencheur « %s » : ajouté au style quand la LoRA est cochée.", word.c_str());
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (active > 0) {
                muted_wrapped(trf("%d LoRA active(s). Chacune est fusionnée dans le modèle au chargement : comptez 10 à 30 s de plus par morceau, sans ralentissement ensuite.", active));
            } else {
                ImGui::TextColored(P.muted, tr("Aucune LoRA utilisée pour ce morceau."));
            }
        }
        end_section();
    }

    void draw_generation_section() {
        Form &        f      = form;
        std::string & lyrics = f.instrumental ? f.lyrics_inst : f.lyrics_song;
        std::string   summary_gen = model_label(current_model) + "  ·  " + solver_label(current_solver) + "  ·  " +
                                    (f.auto_duration ? std::string(tr("durée auto")) : trf("%d s", f.duration)) + "  ·  " +
                                    (f.versions > 1 ? trf("%d versions", f.versions) : trf("%d version", f.versions));
        bool open = begin_section("sec_generation", tr("Génération"), P.amber, nullptr, summary_gen);

        // Boutons toujours visibles, sur une même ligne, même quand le bloc est replié.
        // Pas de génération sans nom ni style (ni paroles pour une chanson).
        std::string missing;
        auto        need = [&](const char * what) { missing += (missing.empty() ? "" : ", ") + std::string(tr(what)); };
        if (trim(f.title).empty()) need(N_("nom du morceau"));
        if (trim(f.style).empty()) need(N_("style"));
        if (!f.instrumental && trim(lyrics).empty()) need(N_("paroles"));
        std::string button;
        if (!f.cover.empty()) button = f.versions > 1 ? trf("Générer %d versions de la reprise", f.versions) : std::string(tr("Générer la reprise"));
        else if (f.instrumental) button = f.versions > 1 ? trf("Générer %d versions de l'instrumental", f.versions) : std::string(tr("Générer l'instrumental"));
        else button = f.versions > 1 ? trf("Générer %d versions de la chanson", f.versions) : std::string(tr("Générer la chanson"));
        float       hold_w = ImGui::CalcTextSize(tr("Ajouter en pause")).x + ImGui::GetStyle().FramePadding.x * 2 + 10 * g_dpi;
        float       gen_w  = ImGui::GetContentRegionAvail().x - hold_w - ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginDisabled(!missing.empty());
        ImGui::PushFont(g_bold);
        if (accent_button(button.c_str(), ImVec2(std::max(gen_w, 120 * g_dpi), 38 * g_dpi))) request_submit(false);
        ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button(tr("Ajouter en pause"), ImVec2(hold_w, 38 * g_dpi))) submit(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(tr("Ajoute à la file sans lancer : vous le lancerez quand la carte graphique sera libre."));
        }
        ImGui::EndDisabled();
        if (!missing.empty()) muted_wrapped(trf("À renseigner pour lancer la génération : %s.", missing.c_str()));

        if (open) {
            static const char * engines[] = { N_("LoRA instrumentale"), N_("Modèle de base") };
            ImGui::Dummy(ImVec2(0, 3 * g_dpi));
            if (ImGui::BeginTable("options", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(P.muted, tr("Mode de composition"));
                ImGui::TableNextColumn();
                ImGui::TextColored(P.muted, f.instrumental ? tr("Moteur instrumental") : tr("Graine (-1 = aléatoire)"));
                ImGui::TableNextColumn();
                ImGui::Checkbox(tr("Durée automatique"), &f.auto_duration);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::Combo("##cot", &f.cot, tr_list(COT_LABELS, (int) IM_ARRAYSIZE(COT_LABELS)).data(), IM_ARRAYSIZE(COT_LABELS));
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (f.instrumental) {
                    // LoRA instrumentale supprimée : seul le modèle de base reste possible (1.0.29).
                    bool inst_lora = lora_files.count(INSTRUMENTAL_FILE) > 0;
                    if (!inst_lora) f.engine = 1;
                    ImGui::BeginDisabled(!inst_lora);
                    ImGui::Combo("##engine", &f.engine, tr_list(engines, (int) IM_ARRAYSIZE(engines)).data(), 2);
                    ImGui::EndDisabled();
                } else {
                    ImGui::InputScalar("##seed", ImGuiDataType_S64, &f.seed);
                    seed_context_menu(f.seed);
                }
                ImGui::TableNextColumn();
                if (f.auto_duration) {
                    ImGui::AlignTextToFramePadding();
                    double est = estimate_seconds(lyrics, f.instrumental);
                    ImGui::TextColored(P.muted, "≈ %s", fmt_time(est).c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(f.instrumental ? tr("Estimée d'après la structure.") : tr("Estimée d'après les paroles : le modèle termine seul."));
                    }
                } else {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputInt("##duration", &f.duration, 10, 60)) f.duration = std::clamp(f.duration, 10, 360);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Plafond en secondes : atteint, il déclenche un fondu de fin."));
                }
                if (f.instrumental) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(P.muted, tr("Graine (-1 = aléatoire)"));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputScalar("##seed", ImGuiDataType_S64, &f.seed);
                    seed_context_menu(f.seed);
                }
                ImGui::EndTable();
            }
            // 1.0.48 : prototype « morceau long » (au-delà des 6 minutes du modèle)
            ImGui::BeginDisabled(!f.cover.empty());
            ImGui::Checkbox(tr("Morceau long : 2 ou 3 parties enchaînées"), &f.long_track);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Le modèle compose au plus 6 minutes d'un coup. Les paroles sont coupées en deux vers le milieu : la partie 1 est "
                                           "générée, puis la partie 2 la continue à partir de ses 25 dernières secondes, et les deux sont assemblées "
                                           "par un fondu enchaîné (jusqu'à environ 11 minutes). Résultat dans la partie 2 ; la partie 1 reste dans la "
                                           "bibliothèque. Les raccords sont à vérifier à l'oreille."));

            ImGui::Checkbox(tr("Fin en fondu sur la structure"), &f.fade_structure);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(tr("Si le morceau atteint le plafond de durée sans conclure (c'est le cas des instrumentaux), le fondu de fin\ndure comme la dernière section de la structure (outro : 20 s en instrumental) et suit une courbe douce,\nau lieu du fondu court du réglage général (%d s)."), settings.fade_seconds);
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, tr("Versions"));
            for (int v = 1; v <= 3; v++) {
                ImGui::SameLine(0, v == 1 ? 10 * g_dpi : 4 * g_dpi);
                char text[8];
                snprintf(text, sizeof(text), " %d ", v);
                if (f.versions == v ? accent_button(text, ImVec2(0, 0), &P.amber) : ImGui::Button(text)) f.versions = v;
            }
        }
        end_section();
    }

    void draw_form() {
        // Deux onglets (1.0.26) : le formulaire habituel et « Composer avec l'IA », qui le remplit.
        // Les deux onglets doivent être déclarés à chaque image AVANT de fermer la barre : ImGui n'affiche que les
        // onglets déclarés (1.0.27 : en 1.0.26, la barre était fermée dès le premier onglet, le second n'apparaissait pas).
        int shown = 0;  // 0 Nouveau morceau, 1 Composer avec l'IA
        poll_ai_job();  // la réponse de l'IA est reçue même si l'on est revenu sur « Nouveau morceau »
        poll_ai_album();  // morceaux d'album mis en file au fur et à mesure (1.0.33)
        poll_covers();    // pochettes générées rangées au fur et à mesure (1.0.36)
        poll_descriptions();
        auto_album_step();  // essai automatique (variable STUDIO_TEST_AUTO_ALBUM) ; sans effet sinon
        if (ImGui::BeginTabBar("##form_tabs")) {
            bool to_form = form_tab_request == 0, to_ai = form_tab_request == 1;
            form_tab_request = -1;
            if (ImGui::BeginTabItem(tr("Nouveau morceau"), nullptr, to_form ? ImGuiTabItemFlags_SetSelected : 0)) {
                shown = 0;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Composer avec l'IA"), nullptr, to_ai ? ImGuiTabItemFlags_SetSelected : 0)) {
                shown = 1;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (shown == 1) {
            ImGui::BeginChild("##ai_body", ImVec2(0, 0), 0);
            draw_ai_tab();
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("##form_body", ImVec2(0, 0), 0);
            draw_form_body();
            ImGui::EndChild();
        }
    }

    void draw_form_body() {
        if (scroll_form_top) {
            ImGui::SetScrollY(0);
            scroll_form_top = false;
        }
        draw_form_toolbar();
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        draw_song_section();
        draw_style_section();
        draw_lyrics_section();
        draw_lora_section();
        draw_generation_section();
        if (g_collapsed_dirty) {
            g_collapsed_dirty = false;
            settings.collapsed.assign(g_collapsed.begin(), g_collapsed.end());
            save_settings(settings, settings_path);
        }
    }

    // ------------------------------------------------------------------ Composer avec l'IA (1.0.26)
    // Onglet à côté de « Nouveau morceau » : un formulaire oriente la demande, une IA (locale ou en ligne) écrit
    // titre, style, langue et paroles, et le résultat nettoyé remplit « Nouveau morceau ». Le moteur se choisit dans
    // Réglages › IA.

    struct AiJob {
        std::atomic<int>  state{ 0 };   // 0 en cours, 1 terminé
        std::atomic<int>  stage{ -1 };  // moteur intégré : 0 chargement du modèle, 1 écriture ; -1 service ou serveur
        std::atomic<bool> cancel{ false };
        AiCallResult      result;
        AiSong            song;
        bool              instrumental = false;
        bool              local        = false;  // moteur intégré (carte graphique ou processeur de ce PC)
        double            started      = 0;
    };
    std::shared_ptr<AiJob> ai_job;
    // Album (1.0.33) : plan puis écriture des morceaux dans un fil ; chaque morceau écrit est mis en file, en pause,
    // par l'interface (poll_ai_album), avec les réglages et les LoRA de « Nouveau morceau ».
    struct AiAlbumTrack {
        AiSong song;
        bool   instrumental = false;
        int    number       = 0;  // n° de piste
    };
    // 1.0.57 : descriptions (groupe, album, morceaux) d'un album rédigées par l'IA locale, dans un fil à part
    struct DescJob {
        std::atomic<bool> cancel{ false }, finished{ false };
        std::atomic<int>  done{ 0 }, written{ 0 };
        int               total = 0;
        std::mutex        mutex;
        std::string       current, error;
    };
    std::shared_ptr<DescJob> desc_job;

    struct AiAlbumJob {
        std::atomic<int>          state{ 0 };   // 0 en cours, 1 terminé
        std::atomic<int>          stage{ -1 };  // moteur intégré : 0 chargement du modèle ; -1 sinon
        std::atomic<bool>         cancel{ false };
        std::atomic<int>          current{ 0 };  // 0 plan de l'album, puis n° du morceau en cours d'écriture
        std::atomic<int>          total{ 0 };
        std::mutex                mutex;          // protège les champs ci-dessous
        std::string               band, album, sound, current_title, error;  // sound : son commun du plan (1.0.34)
        std::string               cover_prompt;                               // 1.0.36
        std::vector<AiAlbumTrack> ready;          // morceaux écrits, pas encore mis en file
        int                       failed = 0;     // morceaux illisibles (passés)
        bool                      local  = false;
        double                    started = 0;
        std::string               language_tag;   // balise de langue YuE2 des chansons
        int                       language_index = 0;
    };
    std::shared_ptr<AiAlbumJob> ai_album;
    int                         ai_album_sent = 0;  // morceaux de l'album en cours déjà mis en file
    struct AiAlbumLine {
        std::string label, style;  // « 01. Titre » et style du morceau (bouton « Enregistrer comme style », 1.0.34)
        AiSong      song;          // 1.0.36 : titre, style, langue, paroles (affichés dans « Résultat » au clic)
        bool        instrumental = false;
        int         number       = 0;
    };
    std::vector<AiAlbumLine>    ai_album_titles;    // morceaux déjà mis en file (affichés dans l'onglet)
    std::string                 ai_album_band, ai_album_name, ai_album_sound;
    std::string                 ai_album_cover_prompt;   // 1.0.36 : description de la pochette écrite dans le plan
    int                         ai_album_selected = -1;  // morceau de l'album affiché dans « Résultat »
    int                         submit_track = 0;      // submit() : n° de piste imposé (album IA)
    int                         ai_album_offset = 0;   // 1.0.47 : album complété, n° de la dernière piste existante
    bool                        submit_quiet = false;  // submit() : pas de message (album IA)
    std::string            ai_response;          // réponse brute de l'IA (modifiable, copiable)
    AiSong                 ai_song;              // dernière réponse lue et nettoyée
    bool                   ai_song_instrumental = false;
    bool                   ai_confirm_send      = false;  // « Remplacer le contenu de Nouveau morceau ? »
    int                    form_tab_request     = -1;     // onglet du formulaire à sélectionner (0 Nouveau morceau, 1 IA)
    int                    open_settings_cat    = -1;     // Réglages ouverts sur une catégorie précise

    // Recherche des serveurs locaux et liste des modèles d'un service (tâches de fond).
    struct AiServerScan {
        std::atomic<int>      state{ 0 };  // 0 en cours, 1 terminé
        std::vector<AiServer> servers;
    };
    std::shared_ptr<AiServerScan> ai_scan;
    struct AiModelList {
        std::atomic<int>         state{ 0 };
        std::string              target;  // service ou serveur interrogé
        std::vector<std::string> models;
        std::string              error;
    };
    std::shared_ptr<AiModelList> ai_model_list;
    struct AiTest {
        std::atomic<int> state{ 0 };
        AiCallResult     result;
    };
    std::shared_ptr<AiTest> ai_test;
    std::string             ai_key_edit;         // clé en cours de saisie (jamais affichée en clair une fois enregistrée)
    std::string             pending_ai_delete;   // modèle IA à supprimer (confirmation)
    std::shared_ptr<std::atomic<int>> llama_extract_state = std::make_shared<std::atomic<int>>(0);  // 0 rien, 1 en cours, 2 fini, 3 échec
    std::shared_ptr<std::string>      llama_extract_error = std::make_shared<std::string>();
    LocalLlm                local_llm;
    double                  local_llm_scan_at = -100;

    fs::path ai_models_dir() const { return studio->paths().models_dir / "ia"; }
    // Cible du téléchargement de llama.cpp Vulkan : toujours outils\llama-vulkan (1.0.42).
    fs::path llama_dir() const { return app_dir() / "outils" / "llama-vulkan"; }

    void refresh_local_llm(bool force = false) {
        if (!force && ImGui::GetTime() - local_llm_scan_at < 5) return;
        local_llm         = find_local_llm(app_dir());
        local_llm_scan_at = ImGui::GetTime();
    }

    // Modèles GGUF présents dans models\ia (catalogue ou déposés à la main).
    std::vector<std::string> ai_local_files() const {
        std::vector<std::string> out;
        std::error_code          ec;
        for (const auto & e : fs::directory_iterator(ai_models_dir(), ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == ".gguf" && e.file_size(ec) > 100ull * 1048576) out.push_back(narrow(e.path().filename().wstring()));
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    bool ai_model_installed(const AiModelFile & m) const {
        std::error_code ec;
        auto            n = fs::file_size(ai_models_dir() / widen(m.file), ec);
        return !ec && n >= m.bytes;
    }
    std::string ai_model_name(const std::string & file) const {
        for (const AiModelFile & m : ai_model_catalog()) if (file == m.file) return m.name;
        return file;
    }
    AiFit ai_fit(unsigned long long bytes) const {
        // 1.0.28 : la carte graphique est toujours utilisée quand il y en a une (demande de Herve).
        return ai_model_fit(bytes, gpu_profile().gpu.vram, sysinfo.ram_total, gpu_profile().gpu.vram > 0);
    }

    // Moteur choisi dans Réglages › IA et prêt à servir ? why : raison sinon. label : nom affiché.
    bool ai_engine_ready(std::string & why, std::string & label) {
        const std::string & e = settings.ai_engine;
        if (e == "local") {
            refresh_local_llm();
            std::error_code ec;
            label = trf("IA locale · %s", settings.ai_local_model.empty() ? "?" : ai_model_name(settings.ai_local_model).c_str());
            if (!local_llm.ready()) why = tr("llama.cpp est introuvable : voir Réglages › IA.");
            else if (gpu_profile().gpu.vram > 0 && !local_llm.vulkan)  // 1.0.28 : pas d'IA locale sur le processeur seul
                why = tr("Il manque llama.cpp pour la carte graphique : sans lui, l'IA locale tournerait sur le processeur (très lent).");
            else if (settings.ai_local_model.empty() || !fs::exists(ai_models_dir() / widen(settings.ai_local_model), ec))
                why = tr("Aucun modèle IA local choisi : téléchargez-en un dans Réglages › IA.");
            else if (studio->local_active() || (cover_job && cover_job->local))
                why = tr("L'IA locale ne travaille qu'au repos : attendez la fin de la génération en cours.");
            return why.empty();
        }
        if (e == "server") {
            label = trf("Serveur local · %s", settings.ai_server_name.empty() ? settings.ai_server_url.c_str() : settings.ai_server_name.c_str());
            if (settings.ai_server_url.empty()) why = tr("Aucun serveur local connecté : voir Réglages › IA.");
            else if (settings.ai_server_model.empty()) why = tr("Choisissez le modèle du serveur local dans Réglages › IA.");
            return why.empty();
        }
        const AiService * s = find_ai_service(e);
        if (!s) {
            label = tr("Aucun moteur");
            why   = tr("Choisissez un moteur d'IA dans Réglages › IA.");
            return false;
        }
        label = tr(s->name);
        if (settings.ai_keys[e].empty() && e != "custom") why = trf("Clé d'API de %s manquante : voir Réglages › IA.", tr(s->name));
        else if (e == "custom" && settings.ai_custom_url.empty()) why = tr("Adresse du service manquante : voir Réglages › IA.");
        else if (ai_service_model(e).empty()) why = tr("Choisissez un modèle dans Réglages › IA.");
        return why.empty();
    }
    std::string ai_service_model(const std::string & id) {
        auto it = settings.ai_models.find(id);
        if (it != settings.ai_models.end() && !it->second.empty()) return it->second;
        const AiService * s = find_ai_service(id);
        return s ? s->model : "";
    }
    // Point d'accès d'un service en ligne ou du serveur local choisi.
    AiEndpoint ai_endpoint(const std::string & id) {
        AiEndpoint ep;
        if (id == "server") {
            ep.base_url = settings.ai_server_url;
            ep.model    = settings.ai_server_model;
            return ep;
        }
        const AiService * s = find_ai_service(id);
        if (!s) return ep;
        ep.kind     = s->kind;
        ep.base_url = id == "custom" ? settings.ai_custom_url : s->base_url;
        ep.key      = ai_unprotect_key(settings.ai_keys[id]);
        ep.model    = ai_service_model(id);
        return ep;
    }

    // Demande construite à partir du formulaire de l'onglet.
    AiRequest ai_request() const {
        AiRequest r;
        int       li   = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        r.language     = LANGUAGES[li].tag[0] ? LANGUAGES[li].tag : "English";
        r.language_tag = LANGUAGES[li].tag;
        r.influence    = trim(settings.ai_influence);
        r.theme        = trim(settings.ai_theme);
        r.mood         = trim(settings.ai_mood);
        r.title        = trim(settings.ai_title);
        r.extra        = trim(settings.ai_extra);
        r.instrumental = settings.ai_instrumental;
        r.length       = settings.ai_length;
        r.avoid        = ai_avoid_words();
        r.negative     = join_style(split_style(settings.ai_negative));  // 1.0.34
        return r;
    }

    // ---- Anti-répétition (1.0.33)
    // IA locale (texte) ou pochettes locales en cours : carte graphique occupée, pas de génération de morceau (1.0.36).
    bool ai_local_busy() const { return (ai_job && ai_job->local) || (ai_album && ai_album->local) || (cover_job && cover_job->local) || desc_job; }
    // Mots évités automatiquement : présents dans au moins 3 des derniers morceaux écrits par l'IA.
    std::vector<std::string> ai_auto_words() const {
        if (!settings.ai_auto_avoid || settings.ai_recent.size() < 3) return {};
        return ai_frequent_words(settings.ai_recent, 3, 20);
    }
    // Liste envoyée à l'IA : celle de Réglages › IA, puis les mots automatiques (sans doublon).
    std::vector<std::string> ai_avoid_words() const {
        std::vector<std::string> out;
        std::set<std::string>    seen;
        auto add = [&](const std::string & w) {
            std::string t = trim(w);
            if (!t.empty() && seen.insert(fold(t)).second) out.push_back(t);
        };
        for (const std::string & w : settings.ai_avoid) add(w);
        for (const std::string & w : ai_auto_words()) add(w);
        return out;
    }
    // Retient un morceau écrit par l'IA (titre + paroles) pour l'évitement automatique ; 15 derniers.
    void remember_ai_song(const AiSong & s, bool instrumental) {
        if (instrumental || !s.ok) return;
        settings.ai_recent.push_back(s.title + "\n" + s.lyrics);
        while (settings.ai_recent.size() > 15) settings.ai_recent.erase(settings.ai_recent.begin());
        save_settings(settings, settings_path);
    }

    // Phrase affichée sous le formulaire : ce qui sera demandé, en clair.
    std::string ai_summary() const {
        const AiRequest r    = ai_request();
        int             li   = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        std::string     lang = tr(LANGUAGES[li].label);
        std::string     s    = r.instrumental ? std::string(tr("Créer un morceau instrumental"))
                                              : trf("Créer un morceau en %s", lang.c_str());
        if (!r.influence.empty()) s += trf(" inspiré par le groupe ou chanteur %s", r.influence.c_str());
        if (!r.theme.empty()) s += trf(" sur le thème %s", r.theme.c_str());
        return s + ".";
    }

    // Phrase affichée en mode album (1.0.33).
    std::string ai_album_summary() const {
        const AiAlbumRequest r    = ai_album_request();
        int                  li   = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        std::string          lang = tr(LANGUAGES[li].label);
        // 1.0.37 : vrais pluriels (« 1 morceau / 10 morceaux », « 1 instrumental / 2 instrumentaux »)
        std::string s;
        if (r.kind == 1) s = r.count == 1 ? std::string(tr("Créer un album instrumental d'un morceau")) : trf("Créer un album instrumental de %d morceaux", r.count);
        else s = r.count == 1 ? trf("Créer un album d'un morceau en %s", lang.c_str()) : trf("Créer un album de %d morceaux en %s", r.count, lang.c_str());
        if (r.kind == 2) s += r.instrumentals == 1 ? std::string(tr(", dont 1 instrumental"))
                            : r.instrumentals > 1  ? trf(", dont %d instrumentaux", r.instrumentals)
                                                   : std::string(tr(", avec quelques instrumentaux"));
        if (!r.band.empty()) s += trf(" pour le groupe %s", r.band.c_str());
        if (!r.influence.empty()) s += trf(" inspiré par le groupe ou chanteur %s", r.influence.c_str());
        if (!r.theme.empty()) s += trf(" sur le thème %s", r.theme.c_str());
        return s + ".";
    }

    void start_ai_compose() {
        std::string why, label;
        if (!ai_engine_ready(why, label)) {
            notify(why);
            return;
        }
        if (settings.ai_engine == "local" && other_synth_running()) {  // moteur de synthèse lancé par ailleurs (essai, autre Studio)
            notify(tr("L'IA locale ne travaille qu'au repos : attendez la fin de la génération en cours."));
            return;
        }
        auto job          = std::make_shared<AiJob>();
        job->instrumental = settings.ai_instrumental;
        job->local        = settings.ai_engine == "local";
        job->started      = ImGui::GetTime();
        ai_job            = job;
        const AiRequest req    = ai_request();
        std::string     system = ai_system_prompt(req.instrumental), user = ai_user_prompt(req);
        AiEndpoint      ep     = job->local ? AiEndpoint{} : ai_endpoint(settings.ai_engine);
        LocalLlm        llm    = local_llm;
        fs::path        model  = ai_models_dir() / widen(settings.ai_local_model);
        std::error_code ec;
        auto            size   = fs::file_size(model, ec);
        // Carte graphique forcée (1.0.28) : tout le modèle sur la carte s'il y tient, sinon le plus possible.
        int             layers = llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
        std::vector<std::string> avoid = req.avoid;
        const bool very_long = req.length == 3 && !req.instrumental;
        std::thread([job, system, user, ep, llm, model, layers, avoid, very_long] {
            auto ask = [&](const std::string & u) {
                return job->local ? ai_local_chat(llm, model, layers, system, u, &job->stage, &job->cancel) : ai_chat(ep, system, u, 180000);
            };
            job->result = ask(user);
            if (job->result.ok) {
                job->song = ai_parse_song(job->result.text, job->instrumental);
                // Réponse illisible : une seule nouvelle demande, avec un rappel du format.
                if (!job->song.ok && !job->cancel) {
                    AiCallResult again = ask(user + "\n\n" + ai_retry_prompt());
                    if (again.ok) {
                        AiSong s2 = ai_parse_song(again.text, job->instrumental);
                        if (s2.ok || !job->song.ok) {
                            job->result = again;
                            job->song   = s2;
                        }
                    }
                }
                // Mots interdits dans le titre ou les paroles (1.0.33) : une seule nouvelle demande ; gardée si elle
                // est lisible et contient moins de mots interdits.
                // Instrumental : seul le titre est vérifié (1.0.34 : « Whispers in the Shadows » passait).
                std::vector<std::string> found = job->song.ok ? ai_forbidden_found(job->instrumental ? job->song.title : job->song.title + "\n" + job->song.lyrics, avoid)
                                                              : std::vector<std::string>{};
                if (!found.empty() && !job->cancel) {
                    AiCallResult again = ask(user + "\n\n" + ai_avoid_retry_prompt(found));
                    if (again.ok) {
                        AiSong s2 = ai_parse_song(again.text, job->instrumental);
                        if (s2.ok && ai_forbidden_found(job->instrumental ? s2.title : s2.title + "\n" + s2.lyrics, avoid).size() < found.size()) {
                            job->result = again;
                            job->song   = s2;
                        }
                    }
                }
                // 1.0.37 : titre encore fautif → un titre seul est redemandé
                std::vector<std::string> bad_title = job->song.ok ? ai_forbidden_found(job->song.title, avoid) : std::vector<std::string>{};
                if (!bad_title.empty() && !job->cancel) {
                    AiCallResult b  = ask(user + "\n\n" + ai_title_retry_prompt(job->song.title, bad_title, avoid));
                    std::string  nt = b.ok ? ai_parse_title(b.text) : std::string();
                    if (!nt.empty() && ai_forbidden_found(nt, avoid).empty()) job->song.title = nt;
                }
                if (job->song.ok && !job->instrumental) {  // 1.0.73 : couplets recopiés retirés
                    std::set<std::string> known;
                    ai_drop_repeated_lines(job->song.lyrics, known);
                }
                if (very_long)
                    ai_lengthen_song(job->song, AI_LONG_LINES, avoid, [&](const std::string & sy, const std::string & u) {
                        return job->local ? ai_local_chat(llm, model, layers, sy, u, &job->stage, &job->cancel) : ai_chat(ep, sy, u, 180000);
                    }, &job->cancel);
            }
            job->state = 1;
        }).detach();
    }

    // Fin d'une demande : la réponse brute et le morceau lu s'affichent dans l'onglet.
    void poll_ai_job() {
        if (!ai_job || ai_job->state != 1) return;
        auto job = ai_job;
        ai_job.reset();
        if (!job->result.ok) {
            if (!job->cancel) notify(trf(tr(job->result.error.c_str()), job->result.error_arg.c_str()));
            return;
        }
        ai_song              = job->song;
        ai_song_instrumental = job->instrumental;
        remember_ai_song(ai_song, job->instrumental);
        // Réponse présentée en texte lisible (1.0.28) ; brute (JSON) seulement si elle n'a pas pu être lue.
        ai_response          = ai_song.ok ? ai_song_text(ai_song, job->instrumental) : job->result.text;
        // Rangé d'office dans « Nouveau morceau », sauf s'il contient déjà des paroles écrites dans le même mode
        // (chanson ou instrumental) : on demande alors avant de les remplacer (1.0.28 : un titre seul ne bloque plus).
        if (ai_song.ok && !form_has_content(job->instrumental)) send_ai_song();  // annonce « … est dans Nouveau morceau »
        else if (ai_song.ok) {
            ai_confirm_send  = true;
            form_tab_request = 1;  // la question s'affiche dans l'onglet « Composer avec l'IA »
        } else notify(tr("Réponse de l'IA illisible : copiez les éléments utiles à la main depuis le champ « Réponse de l'IA »."));
    }

    // Range le morceau dans « Nouveau morceau » (titre, style avec la langue, paroles) et y bascule.
    // Titre, style (avec la langue et le profil d'influence connu) et paroles d'un morceau écrit par l'IA dans le
    // formulaire (« Envoyer dans Nouveau morceau », et chaque morceau d'un album, 1.0.33).
    // Style d'un morceau écrit par l'IA tel qu'il part dans « Nouveau morceau » (sans la langue) : complété par le
    // profil d'influence connu, sans les balises des styles à éviter (1.0.34).
    std::string ai_song_style(const std::string & raw) const {
        std::string style = raw;
        // Profil d'influence connu (Réglages : liste des groupes et chanteurs) : ses balises complètent le style.
        if (settings.ai_profile) {
            std::string want = fold(trim(settings.ai_influence));
            for (const Influence & inf : INFLUENCES) {
                if (!want.empty() && fold(inf.name) == want) style = merge_style(style, split_style(influence_style(inf)));
            }
        }
        return ai_strip_negative(style, join_style(split_style(settings.ai_negative)));
    }

    void fill_form_with(Form & f, const AiSong & s, bool inst) {
        set_mode(f, inst);
        if (!s.title.empty()) f.title = s.title;
        std::string style = ai_song_style(s.style);
        // Styles à éviter de l'onglet (1.0.34) : appliqués au « Style à éviter » du morceau ; vides = celui du formulaire.
        if (!trim(settings.ai_negative).empty()) f.negative_style = join_style(split_style(settings.ai_negative));
        // 1.0.37 : instrumental sans voix quoi qu'ait écrit l'IA (style sans balise vocale + « instrumental, no vocals »,
        // voix ajoutées au style à éviter)
        if (inst) {
            style            = ai_force_instrumental_style(style);
            f.negative_style = ai_instrumental_negative(f.negative_style);
        }
        int li = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        f.style    = set_language(style, inst ? "" : LANGUAGES[li].tag);
        f.language = inst ? f.language : li;
        (inst ? f.lyrics_inst : f.lyrics_song) = s.lyrics;
        // 1.0.49 : la durée suit les paroles écrites (sinon une durée fixe du formulaire, souvent 3 min, coupe le morceau) ;
        // « Très longue » : morceau en 2 parties enchaînées (découpage des paroles de la 1.0.48 à l'envoi).
        f.auto_duration = true;
        f.long_track    = settings.ai_length == 3;
        sync_lora_triggers();  // les LoRA cochées gardent leur mot déclencheur
    }

    void send_ai_song() {
        Form & f = form;
        fill_form_with(f, ai_song, ai_song_instrumental);
        f.cursor = -1;
        f.dirty  = true;
        ImGui::ClearActiveID();
        scroll_form_top  = true;
        form_tab_request = 0;
        notify(trf("« %s » est dans Nouveau morceau.", ai_song.title.empty() ? tr("Sans titre") : ai_song.title.c_str()));
    }

    // ---- Enregistrer un style écrit par l'IA dans « Mes styles » (1.0.34)
    // Fenêtre : nom (proposé : groupe ou titre), LoRA cochées de « Nouveau morceau » et styles à éviter de l'onglet en
    // option ; si un de vos styles porte déjà ce nom : Remplacer ou Garder les deux (« Nom (2) »).
    bool        ai_style_open = false, ai_style_conflict = false, ai_style_with_loras = true, ai_style_with_negative = true;
    std::string ai_style_text, ai_style_name;

    // 1.0.46 : la même fenêtre sert aussi pour un morceau de la bibliothèque (ses LoRA et son style à éviter, au lieu de
    // ceux de « Nouveau morceau » et de l'onglet IA).
    bool                 ai_style_from_song = false;
    std::vector<LoraUse> ai_style_song_loras;
    std::string          ai_style_song_negative;
    float                ai_style_song_scale = 1.5f;
    void open_ai_style_save(const std::string & style, const std::string & name) {
        ai_style_text      = style;
        ai_style_name      = trim(name);
        ai_style_conflict  = false;
        ai_style_from_song = false;
        ai_style_open      = true;
    }
    void open_song_style_save(const Song & s) {
        open_ai_style_save(s.style, s.artist.empty() ? display_title(s.title) : s.artist);
        ai_style_from_song = true;
        ai_style_song_loras.clear();
        for (const auto & [name, scale] : s.adapters) ai_style_song_loras.push_back({ name, (float) scale, true });
        ai_style_song_negative = s.negative_style;
        ai_style_song_scale    = (float) s.negative_scale;
    }
    std::string ai_style_negative() const {
        return join_style(split_style(ai_style_from_song ? ai_style_song_negative : settings.ai_negative));
    }
    std::vector<LoraUse> ai_style_loras() const {
        std::vector<LoraUse> chosen;
        for (const LoraUse & u : ai_style_from_song ? ai_style_song_loras : form.loras) {
            if (u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE) chosen.push_back(u);
        }
        return chosen;
    }
    bool personal_preset_exists(const std::string & name) const {
        for (const Preset & p : settings.presets) if (p.pack.empty() && fold(p.name) == fold(name)) return true;
        return false;
    }
    void store_ai_style(bool replace) {
        Preset np;
        np.name  = trim(ai_style_name);
        np.style = ai_style_text;
        if (ai_style_with_loras) np.loras = ai_style_loras();
        std::string negative = ai_style_negative();
        if (ai_style_with_negative && !negative.empty()) {
            np.has_negative   = true;
            np.negative_style = negative;
            np.negative_scale = ai_style_from_song ? ai_style_song_scale : form.negative_scale;
        }
        if (replace) {
            for (Preset & p : settings.presets) {
                if (p.pack.empty() && fold(p.name) == fold(np.name)) {
                    np.name           = p.name;
                    np.structure_song = p.structure_song;  // la structure enregistrée avant est gardée
                    np.structure_inst = p.structure_inst;
                    p                 = np;
                    replace           = false;  // remplacé
                    break;
                }
            }
            if (replace) settings.presets.push_back(np);
        } else {
            std::string base = np.name;
            for (int n = 2; personal_preset_exists(np.name); n++) np.name = base + " (" + std::to_string(n) + ")";
            settings.presets.push_back(np);
        }
        save_settings(settings, settings_path);
        notify(trf("Style « %s » enregistré dans vos styles.", np.name.c_str()));
    }
    void draw_ai_style_modal() {
        const char * title = tr("Enregistrer comme style personnalisé");
        if (ai_style_open) {
            ImGui::OpenPopup(title);
            ai_style_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480 * g_dpi, 0));
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) return;
        muted_wrapped(ai_style_text);
        if (!ai_style_conflict) {
            std::vector<LoraUse> chosen = ai_style_loras();
            if (!chosen.empty()) {
                std::string names;
                for (const LoraUse & u : chosen) names += (names.empty() ? "" : ", ") + lora_label(u.file);
                ImGui::Checkbox((ai_style_from_song ? trf("Inclure les LoRA du morceau et leur force (%s)", names.c_str())
                                                    : trf("Inclure les LoRA cochées et leur force (%s)", names.c_str())).c_str(),
                                &ai_style_with_loras);
            }
            std::string negative = ai_style_negative();
            if (!negative.empty()) ImGui::Checkbox(trf("Inclure le style à éviter (%s)", negative.c_str()).c_str(), &ai_style_with_negative);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool enter = ImGui::InputTextWithHint("##ai_style_name", tr("Nom du préréglage (ex. : Cold wave 80s)"), &ai_style_name, ImGuiInputTextFlags_EnterReturnsTrue);
            text_context_menu(ai_style_name);
            bool valid = !trim(ai_style_name).empty() && !trim(ai_style_text).empty();
            if (modal_buttons(tr("Enregistrer"), valid) || (enter && valid)) {
                if (personal_preset_exists(ai_style_name)) ai_style_conflict = true;  // question ci-dessous
                else {
                    store_ai_style(false);
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            ImGui::TextWrapped("%s", trf("Un de vos styles s'appelle déjà « %s ». Remplacer : l'ancien style est remplacé par celui-ci. Garder les deux : celui-ci est ajouté sous un autre nom.",
                                         trim(ai_style_name).c_str()).c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button(tr("Garder les deux"))) {
                store_ai_style(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Remplacer"), ImVec2(0, 0), &P.accent)) {
                store_ai_style(true);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // ---- Dernier album écrit par l'IA, gardé dans donnees\dernier_album.json (1.0.36) et relu au démarrage
    fs::path last_album_file() const { return settings_path.parent_path() / "dernier_album.json"; }
    void save_last_album() const {
        yyjson_mut_doc * doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val * o   = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, o);
        yyjson_mut_obj_add_strcpy(doc, o, "band", ai_album_band.c_str());
        yyjson_mut_obj_add_strcpy(doc, o, "album", ai_album_name.c_str());
        yyjson_mut_obj_add_strcpy(doc, o, "sound", ai_album_sound.c_str());
        yyjson_mut_obj_add_strcpy(doc, o, "cover_prompt", ai_album_cover_prompt.c_str());
        yyjson_mut_val * arr = yyjson_mut_obj_add_arr(doc, o, "tracks");
        for (const AiAlbumLine & l : ai_album_titles) {
            yyjson_mut_val * t = yyjson_mut_arr_add_obj(doc, arr);
            yyjson_mut_obj_add_strcpy(doc, t, "label", l.label.c_str());
            yyjson_mut_obj_add_strcpy(doc, t, "style", l.style.c_str());
            yyjson_mut_obj_add_int(doc, t, "number", l.number);
            yyjson_mut_obj_add_bool(doc, t, "instrumental", l.instrumental);
            yyjson_mut_obj_add_strcpy(doc, t, "title", l.song.title.c_str());
            yyjson_mut_obj_add_strcpy(doc, t, "language", l.song.language.c_str());
            yyjson_mut_obj_add_strcpy(doc, t, "song_style", l.song.style.c_str());
            yyjson_mut_obj_add_strcpy(doc, t, "lyrics", l.song.lyrics.c_str());
        }
        write_text(last_album_file(), write_json(doc));
    }
    void load_last_album() {
        std::string  text = read_text(last_album_file());
        yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
        if (!doc) return;
        yyjson_val * r        = yyjson_doc_get_root(doc);
        ai_album_band         = json_str(r, "band");
        ai_album_name         = json_str(r, "album");
        ai_album_sound        = json_str(r, "sound");
        ai_album_cover_prompt = json_str(r, "cover_prompt");
        ai_album_titles.clear();
        size_t       i, n;
        yyjson_val * t;
        yyjson_arr_foreach(yyjson_obj_get(r, "tracks"), i, n, t) {
            AiAlbumLine l;
            l.label        = json_str(t, "label");
            l.style        = json_str(t, "style");
            l.number       = (int) yyjson_get_int(yyjson_obj_get(t, "number"));
            l.instrumental = yyjson_is_true(yyjson_obj_get(t, "instrumental"));
            l.song.title    = json_str(t, "title");
            l.song.language = json_str(t, "language");
            l.song.style    = json_str(t, "song_style");
            l.song.lyrics   = json_str(t, "lyrics");
            l.song.ok       = !l.song.lyrics.empty();
            ai_album_titles.push_back(l);
        }
        yyjson_doc_free(doc);
    }

    // ---- Pochettes générées (1.0.36)
    // Une tâche = un emplacement d'image (pochette 1:1 = clé d'album, « #youtube » 16:9, « #tiktok » 9:16) et sa description.
    struct CoverTask {
        std::string slot, prompt, label;
        int         w = 768, h = 768;
        std::string model;  // 1.0.51 : modèle local choisi pour cette fois (« klein », « zimage », « moss ») ; vide = Réglages
    };
    struct CoverJob {
        std::atomic<int>                                 state{ 0 };  // 0 en cours, 1 terminé
        std::atomic<bool>                                cancel{ false };
        std::atomic<int>                                 done{ 0 };
        int                                              total = 0;
        std::mutex                                       mutex;
        std::vector<std::pair<std::string, std::string>> ready;  // emplacement → image écrite (à ranger dans « pochettes »)
        std::string                                      error, current;
        bool                                             local   = false;
        double                                           started = 0;
    };
    std::shared_ptr<CoverJob> cover_job;
    CoverTask                 pending_regen;          // « Régénérer » : confirmation avant de remplacer une image
    bool                      pending_regen_open = false;

    fs::path    image_models_dir() const { return studio->paths().models_dir / "images"; }
    std::string image_service_model(const std::string & id) const {
        auto it = settings.ai_image_models.find(id);
        if (it != settings.ai_image_models.end() && !trim(it->second).empty()) return trim(it->second);
        const ImageService * s = find_image_service(id);
        return s ? s->model : "";
    }
    AiEndpoint image_endpoint(const std::string & id) {
        AiEndpoint           ep;
        const ImageService * s = find_image_service(id);
        if (!s) return ep;
        ep.base_url = id == "custom" ? settings.ai_custom_url : s->base_url;
        ep.key      = ai_unprotect_key(settings.ai_keys[s->key_id]);
        ep.model    = image_service_model(id);
        return ep;
    }
    // Moteur d'images prêt ? why : raison sinon ; label : nom affiché.
    bool image_engine_ready(std::string & why, std::string & label) {
        const std::string &  id = settings.ai_image_engine;
        const ImageService * s  = find_image_service(id);
        label = s ? tr(s->name) : tr("Aucun moteur");
        if (!s) why = tr("Choisissez un service d'images dans Réglages › IA › Pochettes.");
        else if (id == "local") {
            if (find_sd_cli(app_dir()).empty()) why = tr("Moteur d'images introuvable (dossier « outils\\sd-vulkan »).");
            else if (!image_model_ready(image_models_dir(), local_image_model(settings.ai_image_local)))
                why = tr("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes.");
            else if (studio->local_active() || ai_local_busy()) why = tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours.");
        } else if (id == "custom" && trim(settings.ai_custom_url).empty()) why = tr("Adresse du service manquante : voir Réglages › IA.");
        else if (settings.ai_keys[s->key_id].empty()) why = trf("Clé d'API de %s manquante : voir Réglages › IA.", tr(s->name));
        return why.empty();
    }
    // 1.0.50 : MOSS-Image garde ses poids en mémoire vive sur les cartes de 12 Go ou moins (sinon la mémoire vidéo manque).
    // 1.0.58 : FLUX.2 klein aussi : en 1344 × 768 sur 12 Go, la mémoire vidéo déborde sans cela (mesuré : 81 s par image contre 27 s).
    // 1.0.72 : Z-Image aussi : 60 s par image avec, 176 s sans.
    static bool image_offload(const LocalImageModel & m) {
        return (std::string(m.id) == "moss" || std::string(m.id) == "klein" || std::string(m.id) == "zimage") && gpu_profile().gpu.vram <= 12.5 * 1024.0 * 1024 * 1024;
    }
    // 1.0.51 : menu « ▾ » de « Générer », « Régénérer » et « Toutes les pochettes » (à appeler dans un BeginPopup).
    // Renvoie vrai quand un choix est fait : model = "" (service des Réglages) ou l'identifiant d'un modèle local installé.
    bool image_model_menu(std::string & model) {
        std::string why, label;
        bool        def_ready = image_engine_ready(why, label);
        bool        idle      = !studio->local_active() && !ai_local_busy();
        bool        sd        = !find_sd_cli(app_dir()).empty();
        bool        picked    = false;
        ImGui::TextColored(P.muted, "%s", tr("Créer avec…"));
        std::string def = trf("Réglage par défaut (%s)", settings.ai_image_engine == "local" ? local_image_model(settings.ai_image_local).name : label.c_str());
        if (ImGui::MenuItem(def.c_str(), nullptr, false, def_ready)) {
            model.clear();
            picked = true;
        }
        if (!def_ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
        ImGui::Separator();
        for (const LocalImageModel & m : local_image_models()) {
            bool        ok   = sd && idle && image_model_ready(image_models_dir(), m);
            std::string time = m.seconds >= 120 ? trf("≈ %d min", m.seconds / 60) : trf("≈ %d s", m.seconds);
            if (ImGui::MenuItem(m.name, ok ? time.c_str() : tr("non installé"), false, ok)) {
                model  = m.id;
                picked = true;
            }
            if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", !idle ? tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours.")
                                              : tr("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes."));
        }
        return picked;
    }
    static void cover_size(const std::string & slot, int & w, int & h) {
        bool yt = slot.size() > 8 && slot.compare(slot.size() - 8, 8, "#youtube") == 0;
        bool tt = slot.size() > 7 && slot.compare(slot.size() - 7, 7, "#tiktok") == 0;
        w       = yt ? 1024 : tt ? 576 : 768;  // ~0,6 mégapixel : 768×768 ≈ 42 s sur RX 6700 XT
        h       = yt ? 576 : tt ? 1024 : 768;
    }
    // Description d'image par défaut d'un album sans description écrite par l'IA : son style, sans texte.
    std::string default_cover_prompt(const std::string & album, const std::string & style) const {
        std::vector<std::string> keep;
        for (const std::string & tg : split_tags(style)) {
            bool lang = false;
            for (const auto & L : LANGUAGES) lang = lang || (L.tag[0] && lower(L.tag) == lower(tg));
            if (!lang && lower(tg).find("bpm") == std::string::npos) keep.push_back(tg);
        }
        return "Album cover art in the mood of the album \"" + album + "\", music style: " + join_style(keep) +
               ". Striking, artistic composition. No text, no letters, no logo.";
    }
    std::string cover_prompt_for(const std::string & key, const std::string & album, const std::string & style) const {
        auto it = settings.cover_prompts.find(key);
        return it != settings.cover_prompts.end() && !trim(it->second).empty() ? it->second : default_cover_prompt(album, style);
    }

    // 1.0.43 : dossier et nom (sans extension) de l'image d'un emplacement. Album : son dossier sous Audios
    // (pochette, pochette_youtube, pochette_tiktok) ; morceau sans album (« titre:<id> ») : son dossier ; sinon
    // donnees\pochettes avec un nom tiré de l'emplacement (album pas encore connu).
    std::map<std::string, fs::path> album_dirs;  // clé d'album -> dossier, mis à jour à chaque image (frame)
    void refresh_album_dirs(const std::vector<Song> & songs) {
        album_dirs.clear();
        std::map<std::string, double> first;  // clé d'album -> création de son plus ancien morceau
        for (const Song & s : songs) {
            if (s.album.empty()) continue;
            std::string key = album_key(s.artist, s.album);
            album_dirs.emplace(key, album_folder(s.artist, s.album));
            auto it = first.find(key);
            if (it == first.end() || s.created < it->second) first[key] = s.created;
        }
        // 1.0.46 : couleurs d'album sans doublon. Les plus anciens albums choisissent d'abord (leur couleur ne change pas
        // quand un album est créé) : case tirée de la clé, sinon la suivante libre de la palette.
        std::vector<std::pair<double, std::string>> order;
        for (const auto & [k, t] : first) order.push_back({ t, k });
        std::sort(order.begin(), order.end());
        g_album_slots.clear();
        std::vector<bool> used(ALBUM_COLORS, false);
        int               taken = 0;
        for (const auto & [t, k] : order) {
            int slot = (int) (std::hash<std::string>{}(k) % ALBUM_COLORS);
            if (taken < ALBUM_COLORS)
                while (used[slot]) slot = (slot + 1) % ALBUM_COLORS;
            used[slot] = true;
            taken++;
            g_album_slots[k] = slot;
        }
    }
    fs::path cover_dir(const std::string & slot, std::wstring & name) const {
        size_t      hash_at = slot.find('#');
        std::string key     = slot.substr(0, hash_at);
        name                = hash_at == std::string::npos ? L"pochette" : L"pochette_" + widen(slot.substr(hash_at + 1));
        if (key.rfind("titre:", 0) == 0) return song_folder(key.substr(6));
        auto it = album_dirs.find(key);
        if (it != album_dirs.end()) return it->second;
        char h[40];
        snprintf(h, sizeof(h), "%016llx", (unsigned long long) std::hash<std::string>{}(slot));
        name = widen(h);
        return pochettes_dir();
    }

    // Pochettes de donnees\pochettes (versions précédentes) rangées dans le dossier de leur album (1.0.43), avec journal.
    void migrate_covers() {
        bool            changed = false;
        std::error_code ec;
        for (auto & [slot, path] : settings.covers) {
            fs::path p = widen(path);
            if (!fs::exists(p, ec)) p = pochettes_dir() / p.filename();  // chemin d'une version antérieure à la 1.0.30
            if (!fs::exists(p, ec) || p.parent_path() != pochettes_dir()) continue;
            std::wstring name;
            fs::path     dir = cover_dir(slot, name);
            if (dir == pochettes_dir()) continue;
            fs::path dst = dir / (name + p.extension().wstring());
            if (fs::exists(dst, ec)) continue;
            fs::create_directories(dir, ec);
            fs::rename(p, dst, ec);
            std::ofstream log(studio->paths().cache / L"migration.log", std::ios::binary | std::ios::app);
            log << narrow(p.wstring()) << " -> " << narrow(dst.wstring()) << (ec ? " : ECHEC " + ec.message() : std::string()) << "\n";
            if (ec) continue;
            path    = narrow(dst.wstring());
            changed = true;
        }
        if (changed) save_settings(settings, settings_path);
    }

    // Image (fichier) rangée comme pochette d'un emplacement, dans le dossier donné par cover_dir (1.0.43).
    bool install_cover(const std::string & slot, const fs::path & img) {
        std::wstring    stem;
        fs::path        dir = cover_dir(slot, stem);
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::vector<fs::path> old;
        for (auto & e : fs::directory_iterator(dir, ec)) {
            if (e.is_regular_file() && e.path().stem() == stem) old.push_back(e.path());
        }
        fs::path dst = dir / (stem + img.extension().wstring());
        for (auto & p : old) if (p != img) fs::remove(p, ec);
        if (img != dst) fs::copy_file(img, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            notify(tr("Copie de l'image impossible."));
            return false;
        }
        settings.covers[slot] = narrow(dst.wstring());
        exists_cache.clear();  // nouvelle image : visible tout de suite (les vignettes suivent la date du fichier)
        save_settings(settings, settings_path);
        return true;
    }

    bool local_text_ready(LocalLlm & llm, fs::path & model, int & layers) {
        std::error_code ec;
        llm    = find_local_llm(app_dir());
        model  = ai_models_dir() / widen(settings.ai_local_model);
        auto size = fs::file_size(model, ec);
        layers = llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
        return llm.ready() && !ec && !settings.ai_local_model.empty();
    }

    void start_descriptions(const std::string & key, const std::vector<Song> & all) {
        LocalLlm llm;
        fs::path model;
        int      layers = 0;
        if (studio->local_active() || ai_local_busy()) {
            notify(tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours."));
            return;
        }
        if (!local_text_ready(llm, model, layers)) {
            notify(tr("IA locale de texte non prête (Réglages › IA)."));
            return;
        }
        std::vector<Song> songs;
        for (const Song & s : album_songs(all, key, false))
            if (s.long_rest.empty()) songs.push_back(with_full_lyrics(s, all));  // partie 1 d'un morceau long : pas de description
        if (songs.empty()) return;
        auto job   = std::make_shared<DescJob>();
        job->total = (int) songs.size() + 2;
        desc_job   = job;
        std::string lang = i18n::current();
        std::thread([job, songs, lang, llm, model, layers] {
            std::atomic<int> stage{ 0 };
            std::string      err;
            AiCallResult     r = ai_local_session(llm, model, layers, &stage, &job->cancel, [&](const AiAsk & ask) {
                write_descriptions(songs, lang, ask, [&](const std::string & what) {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->current = what;
                    job->done++;
                }, job->cancel, job->written, err);
            });
            std::lock_guard<std::mutex> lock(job->mutex);
            job->error = !err.empty() ? err : r.ok ? std::string() : r.error;
            job->finished = true;
        }).detach();
    }

    void poll_descriptions() {
        if (!desc_job || !desc_job->finished) return;
        auto job = desc_job;
        desc_job.reset();
        if (!job->error.empty()) notify(trf("Descriptions : %s", job->error.c_str()));
        else notify(trf("%d description(s) écrite(s) (fichiers « description.txt », modifiables).", job->written.load()));
    }

    void start_covers(std::vector<CoverTask> tasks) {
        std::string why, label;
        if (tasks.empty()) return;
        if (cover_job) {
            notify(tr("Des pochettes sont déjà en cours de création."));
            return;
        }
        // 1.0.51 : un modèle local choisi dans le menu « ▾ » passe avant le service des Réglages (toutes les tâches ont le même)
        const std::string forced = tasks[0].model;
        if (forced.empty() && !image_engine_ready(why, label)) {
            notify(why);
            return;
        }
        if (!forced.empty()) {
            if (find_sd_cli(app_dir()).empty() || !image_model_ready(image_models_dir(), local_image_model(forced))) {
                notify(tr("Modèle d'images absent : téléchargez-le dans Réglages › IA › Pochettes."));
                return;
            }
            if (studio->local_active() || ai_local_busy()) {
                notify(tr("Les pochettes locales se font au repos : attendez la fin de la génération en cours."));
                return;
            }
        }
        auto job     = std::make_shared<CoverJob>();
        job->local   = !forced.empty() || settings.ai_image_engine == "local";
        job->total   = (int) tasks.size();
        job->started = ImGui::GetTime();
        cover_job    = job;
        const std::string service = settings.ai_image_engine;
        AiEndpoint        ep      = job->local ? AiEndpoint{} : image_endpoint(service);
        fs::path          sd      = find_sd_cli(app_dir()), dir = image_models_dir(), tmp = app_temp_dir() / L"pochettes";
        const LocalImageModel & model   = local_image_model(forced.empty() ? settings.ai_image_local : forced);
        const bool              offload = image_offload(model);
        std::thread([job, tasks, service, ep, sd, dir, tmp, &model, offload] {
            std::error_code ec;
            fs::create_directories(tmp, ec);
            for (const CoverTask & t : tasks) {
                if (job->cancel) break;
                {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->current = t.label;
                }
                char name[40];
                snprintf(name, sizeof(name), "%016llx", (unsigned long long) std::hash<std::string>{}(t.slot));
                AiCallResult r = job->local ? ai_image_local(sd, dir, model, t.prompt, t.w, t.h, tmp / (widen(name) + L".png"), &job->cancel, offload)
                                            : ai_image_online(service, ep, t.prompt, t.w, t.h, tmp / widen(name));
                std::lock_guard<std::mutex> lock(job->mutex);
                if (r.ok) job->ready.push_back({ t.slot, r.text });
                else if (job->error.empty() && !job->cancel) job->error = trf(tr(r.error.c_str()), r.error_arg.c_str());
                job->done++;
            }
            job->state = 1;
        }).detach();
    }

    // Images terminées : rangées comme pochettes au fur et à mesure ; message à la fin.
    void poll_covers() {
        if (!cover_job) return;
        auto                                             job = cover_job;
        std::vector<std::pair<std::string, std::string>> todo;
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            todo.swap(job->ready);
        }
        for (auto & [slot, file] : todo) {
            std::error_code ec;
            install_cover(slot, widen(file));
            fs::remove(widen(file), ec);  // copie temporaire (donnees\temp\pochettes)
        }
        if (job->state != 1) return;
        cover_job.reset();
        if (job->cancel) notify(tr("Création des pochettes arrêtée."));
        else if (!job->error.empty()) notify(job->error);
        else notify(trf("%d pochette(s) créée(s).", job->done.load()));
    }
    void draw_cover_progress() {
        if (!cover_job) return;
        std::string current;
        {
            std::lock_guard<std::mutex> lock(cover_job->mutex);
            current = cover_job->current;
        }
        int secs = (int) (ImGui::GetTime() - cover_job->started);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.pink, "%s", trf("Pochettes %d/%d : %s… %d s", std::min(cover_job->done.load() + 1, cover_job->total), cover_job->total,
                                             current.c_str(), secs).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Arrêter")) + "##covers").c_str())) cover_job->cancel = true;
    }
    // Fenêtre « splash » (1.0.37) : modale centrée sur fond assombri pendant toute création de pochettes ; se ferme
    // seule à la fin (le message de fin ou d'erreur passe par notify). Appelée au niveau principal et depuis les
    // fenêtres « Albums… » et Réglages : une modale ne peut s'ouvrir qu'au-dessus de la fenêtre modale déjà ouverte.
    void draw_cover_splash() {
        const char * id = tr("Création des pochettes");
        if (cover_job && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) return;
        if (!cover_job) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        std::string current;
        {
            std::lock_guard<std::mutex> lock(cover_job->mutex);
            current = cover_job->current;
        }
        int   done = cover_job->done, total = std::max(1, cover_job->total);
        int   secs = (int) (ImGui::GetTime() - cover_job->started);
        ImGui::PushFont(g_header);
        ImGui::TextColored(P.pink, "%s", tr("Création des pochettes…"));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        colored_wrapped(P.text, trf("Image %d sur %d : %s", std::min(done + 1, total), total, current.c_str()));
        // barre : images terminées + avancée lente de l'image en cours (≈ 45 s par image en local), pour montrer l'activité
        float part = std::min(0.95f, (float) std::fmod(ImGui::GetTime() - cover_job->started, 45.0) / 45.f);
        progress_bar(((float) done + (cover_job->local ? part : 0.5f)) / (float) total, ImVec2(-FLT_MIN, 0), fmt_time(secs).c_str());
        muted_wrapped(cover_job->local ? tr("Sur la carte graphique : environ 40 s par image. La génération de musique attend la fin.")
                                       : tr("Service en ligne : quelques secondes par image."));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) cover_job->cancel = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Arrête la création ; les images déjà terminées sont gardées."));
        ImGui::EndPopup();
    }

    // 1.0.78 : description d'image de l'album ; sans description, une description simple (titre, groupe, son de l'album) est
    // proposée et mémorisée, pour qu'un bouton « Générer » marche sans passer par l'onglet Pochettes.
    std::string album_image_prompt(const std::string & key, const std::string & album, const std::string & artist) {
        std::string & p = settings.cover_prompts[key];
        if (trim(p).empty()) {
            auto        snd   = settings.album_sounds.find(key);
            std::string sound = snd != settings.album_sounds.end() ? trim(snd->second) : std::string();
            p = "Album cover art for \"" + album + "\"" + (artist.empty() ? std::string() : " by the band " + artist) +
                (sound.empty() ? std::string() : ", music style: " + sound) + ". Striking, atmospheric illustration, no text, no letters, no logo.";
            save_settings(settings, settings_path);
        }
        return p;
    }

    // Tâches d'un album : pochette 1:1 et, si demandé, images YouTube et TikTok. skip_existing : une image déjà là est gardée.
    std::vector<CoverTask> album_cover_tasks(const std::string & key, const std::string & album, const std::string & prompt, bool youtube,
                                             bool tiktok, bool skip_existing, int & skipped) {
        std::vector<CoverTask> out;
        skipped = 0;
        const std::pair<std::string, const char *> slots[] = { { key, N_("Pochette 1:1") }, { slot_youtube(key), "YouTube 16:9" }, { slot_tiktok(key), "TikTok 9:16" } };
        for (int i = 0; i < 3; i++) {
            if ((i == 1 && !youtube) || (i == 2 && !tiktok)) continue;
            if (skip_existing && !cover_for(slots[i].first).empty()) {
                skipped++;
                continue;
            }
            CoverTask t;
            t.slot   = slots[i].first;
            t.prompt = prompt;
            t.label  = album + " · " + tr(slots[i].second);
            cover_size(t.slot, t.w, t.h);
            out.push_back(t);
        }
        return out;
    }

    // ---- Album (1.0.33)

    // Demande de l'album, d'après l'onglet.
    AiAlbumRequest ai_album_request() const {
        AiAlbumRequest r;
        int            li = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        r.language        = LANGUAGES[li].tag[0] ? LANGUAGES[li].tag : "English";
        r.influence       = trim(settings.ai_influence);
        r.theme           = trim(settings.ai_theme);
        r.mood            = trim(settings.ai_mood);
        r.extra           = trim(settings.ai_extra);
        r.band            = trim(settings.ai_band);
        r.album           = trim(settings.ai_album_title);
        r.count           = std::clamp(settings.ai_count, 1, 50);
        r.kind            = settings.ai_mixed ? 2 : settings.ai_instrumental ? 1 : 0;
        r.max_instrumentals = std::clamp(settings.ai_max_instrumentals, 0, 50);  // 1.0.35 : Réglages › IA
        r.instrumentals   = std::clamp(settings.ai_instrumentals, 0, r.max_instrumentals);
        r.avoid           = ai_avoid_words();
        r.negative        = join_style(split_style(settings.ai_negative));  // 1.0.34
        add_band_history(r);
        return r;
    }

    // 1.0.47 (b2, b3) : groupe déjà présent dans la bibliothèque. Ses titres ne sont jamais repris ; son son est gardé
    // (album_sounds, sinon les balises de style présentes dans au moins la moitié de ses morceaux) ; si l'album existe
    // déjà, les nouveaux morceaux le complètent (numéros de piste à la suite) ; sinon l'IA doit inventer un autre titre
    // d'album que ceux du groupe.
    static std::string bare_title(std::string t) {
        if (t.size() > 4 && isdigit((unsigned char) t[0]) && isdigit((unsigned char) t[1]) && t[2] == '.' && t[3] == ' ') t = t.substr(4);
        return trim(t);
    }
    int band_album_last_track(const std::string & band, const std::string & album) const {
        int last = 0;
        if (band.empty() || album.empty()) return 0;
        std::string key = album_key(band, album);
        for (const Song & s : studio->snapshot())
            if (!s.album.empty() && album_key(s.artist, s.album) == key) last = std::max(last, track_number(s));
        return last;
    }
    void add_band_history(AiAlbumRequest & r) const {
        if (r.band.empty()) return;
        std::string              key = r.album.empty() ? std::string() : album_key(r.band, r.album);
        std::set<std::string>    titles, albums;
        std::map<std::string, int> tags;
        int                      count = 0;
        double                   newest = -1;
        std::string              newest_sound;
        for (const Song & s : studio->snapshot()) {
            if (fold(s.artist) != fold(r.band) || s.status == "error" || s.status == "cancelled") continue;
            bool in_album = !key.empty() && !s.album.empty() && album_key(s.artist, s.album) == key;
            r.complete |= in_album;
            if (!s.title.empty()) titles.insert(bare_title(s.title));
            if (!s.album.empty()) albums.insert(s.album);
            if (!key.empty() && r.complete && !in_album) continue;  // compléter : le son de cet album seulement
            count++;
            std::set<std::string> once;
            for (const std::string & t : split_style(s.style)) once.insert(fold(t));
            for (const std::string & t : once) tags[t]++;
            if (!s.album.empty() && s.created > newest) {
                auto it = settings.album_sounds.find(album_key(s.artist, s.album));
                if (it != settings.album_sounds.end() && !trim(it->second).empty()) {
                    newest       = s.created;
                    newest_sound = it->second;
                }
            }
        }
        r.used_titles.assign(titles.begin(), titles.end());
        for (const std::string & a : albums) if (!r.complete) r.used_albums.push_back(a);
        auto known = key.empty() ? settings.album_sounds.end() : settings.album_sounds.find(key);
        if (r.complete && known != settings.album_sounds.end() && !trim(known->second).empty()) r.sound = known->second;
        else if (!newest_sound.empty()) r.sound = newest_sound;
        else if (count > 0) {
            std::vector<std::string> common;
            for (const auto & [t, n] : tags)
                if (n * 2 >= count && t.find("bpm") == std::string::npos && t != "instrumental") common.push_back(t);
            r.sound = join_style(common);
        }
    }

    void start_ai_album() {
        std::string why, label;
        if (!ai_engine_ready(why, label)) {
            notify(why);
            return;
        }
        if (settings.ai_engine == "local" && other_synth_running()) {
            notify(tr("L'IA locale ne travaille qu'au repos : attendez la fin de la génération en cours."));
            return;
        }
        auto job            = std::make_shared<AiAlbumJob>();
        job->local          = settings.ai_engine == "local";
        job->started        = ImGui::GetTime();
        int li              = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
        job->language_tag   = LANGUAGES[li].tag;
        job->language_index = li;
        ai_album            = job;
        ai_album_sent       = 0;
        ai_album_titles.clear();
        ai_album_band.clear();
        ai_album_name.clear();
        ai_album_sound.clear();
        ai_album_cover_prompt.clear();
        ai_album_selected = -1;
        const AiAlbumRequest req = ai_album_request();
        job->total               = req.count;
        ai_album_offset          = req.complete ? band_album_last_track(req.band, req.album) : 0;  // 1.0.47 : pistes à la suite
        AiRequest base           = ai_request();  // langue, influence, thème général, longueur, souhaits, mots à éviter
        base.title.clear();                        // le « Titre imposé » ne sert qu'à un morceau seul
        AiEndpoint ep            = job->local ? AiEndpoint{} : ai_endpoint(settings.ai_engine);
        LocalLlm   llm           = local_llm;
        fs::path   model         = ai_models_dir() / widen(settings.ai_local_model);
        std::error_code ec;
        auto       size          = fs::file_size(model, ec);
        int        layers        = llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
        std::thread([job, req, base, ep, llm, model, layers] {
            auto work = [&](const AiAsk & ask) {
                // 1. Plan de l'album (une nouvelle demande si illisible)
                job->stage                = -1;
                const std::string system  = ai_album_system_prompt(), user = ai_album_user_prompt(req);
                AiCallResult      res     = ask(system, user);
                AiAlbumPlan       plan    = res.ok ? ai_parse_album_plan(res.text, req) : AiAlbumPlan{};
                if (res.ok && !plan.ok && !job->cancel) {
                    res  = ask(system, user + "\n\nYour previous answer could not be read. Answer again with ONE valid JSON object only.");
                    plan = res.ok ? ai_parse_album_plan(res.text, req) : AiAlbumPlan{};
                }
                if (!plan.ok) {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->error = res.ok ? tr("Plan de l'album illisible : réessayez, ou choisissez un autre modèle d'IA.")
                                        : trf(tr(res.error.c_str()), res.error_arg.c_str());
                    return;
                }
                // 1.0.34 : nom du groupe ou titre de l'album choisis par l'IA avec un mot interdit → un nouveau plan
                // (ceux tapés par l'utilisateur ne sont jamais vérifiés ni changés). Le plan qui en contient le moins est gardé.
                std::vector<std::string> bad = ai_plan_forbidden(plan, req);
                if (!bad.empty() && !job->cancel) {
                    AiCallResult again = ask(system, user + "\n\n" + ai_plan_retry_prompt(bad));
                    AiAlbumPlan  p2    = again.ok ? ai_parse_album_plan(again.text, req) : AiAlbumPlan{};
                    if (p2.ok && ai_plan_forbidden(p2, req).size() < bad.size()) plan = p2;
                }
                {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->band  = plan.band;
                    job->album = plan.album;
                    job->sound = plan.sound;
                    job->cover_prompt = plan.cover_prompt;
                }
                // 2. Un morceau à la fois ; les titres déjà écrits sont transmis pour ne pas les répéter.
                std::vector<std::string> titles, album_lines;  // 1.0.73 : lignes déjà chantées dans l'album
                for (int i = 0; i < (int) plan.tracks.size() && !job->cancel; i++) {
                    const AiTrackPlan & tp = plan.tracks[i];
                    job->current           = i + 1;
                    {
                        std::lock_guard<std::mutex> lock(job->mutex);
                        job->current_title = tp.title;
                    }
                    AiRequest r     = base;
                    r.instrumental  = tp.instrumental;
                    r.title         = tp.title;
                    if (!tp.theme.empty()) r.theme = tp.theme;
                    r.used_titles   = titles;
                    r.used_lines    = album_lines;
                    r.album_context = ai_track_context(plan, i);
                    const std::string sys = ai_system_prompt(r.instrumental), u = ai_user_prompt(r);
                    AiCallResult      a   = ask(sys, u);
                    AiSong            s   = a.ok ? ai_parse_song(a.text, r.instrumental) : AiSong{};
                    if (a.ok && !s.ok && !job->cancel) {
                        AiCallResult b = ask(sys, u + "\n\n" + ai_retry_prompt());
                        if (b.ok) s = ai_parse_song(b.text, r.instrumental);
                    }
                    std::vector<std::string> found = s.ok ? ai_forbidden_found(r.instrumental ? s.title : s.title + "\n" + s.lyrics, r.avoid) : std::vector<std::string>{};  // instrumental : titre seul (1.0.34)
                    if (!found.empty() && !job->cancel) {
                        AiCallResult b = ask(sys, u + "\n\n" + ai_avoid_retry_prompt(found));
                        AiSong       s2 = b.ok ? ai_parse_song(b.text, r.instrumental) : AiSong{};
                        if (s2.ok && ai_forbidden_found(r.instrumental ? s2.title : s2.title + "\n" + s2.lyrics, r.avoid).size() < found.size()) s = s2;
                    }
                    // 1.0.37 : titre encore fautif (ex. « Echoes of the Ancients » malgré « echo ») → un titre seul est redemandé
                    std::vector<std::string> bad_title = s.ok && tp.title.empty() ? ai_forbidden_found(s.title, r.avoid) : std::vector<std::string>{};
                    if (!bad_title.empty() && !job->cancel) {
                        AiCallResult b  = ask("You are a songwriter. You only answer with a JSON object.", ai_title_retry_prompt(s.title, bad_title, r.avoid));
                        std::string  nt = b.ok ? ai_parse_title(b.text) : std::string();
                        if (!nt.empty() && ai_forbidden_found(nt, r.avoid).empty()) s.title = nt;
                    }
                    if (s.ok && !r.instrumental) {  // 1.0.73 : lignes déjà chantées dans l'album ou plus haut retirées
                        std::set<std::string> known = ai_known_lines(album_lines);
                        ai_drop_repeated_lines(s.lyrics, known);
                    }
                    if (r.length == 3 && !r.instrumental) ai_lengthen_song(s, AI_LONG_LINES, r.avoid, ask, &job->cancel, album_lines);
                    if (s.ok && !r.instrumental)
                        for (const std::string & l : ai_song_lines(s.lyrics))
                            if (std::find(album_lines.begin(), album_lines.end(), l) == album_lines.end()) album_lines.push_back(l);
                    if (job->cancel) break;
                    std::lock_guard<std::mutex> lock(job->mutex);
                    if (!s.ok) {
                        job->failed++;
                        if (!a.ok && job->error.empty()) job->error = trf(tr(a.error.c_str()), a.error_arg.c_str());
                        continue;
                    }
                    if (!tp.title.empty()) s.title = tp.title;  // titre du plan (déjà vérifié) prioritaire
                    if (s.title.empty()) s.title = trf("Piste %d", i + 1);
                    titles.push_back(s.title);
                    job->ready.push_back({ s, r.instrumental, i + 1 });
                }
            };
            if (job->local) {
                AiCallResult start = ai_local_session(llm, model, layers, &job->stage, &job->cancel, work);
                if (!start.ok) {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->error = trf(tr(start.error.c_str()), start.error_arg.c_str());
                }
            } else {
                work([&](const std::string & system, const std::string & user) { return ai_chat(ep, system, user, 180000); });
            }
            job->state = 1;
        }).detach();
    }

    // Morceaux de l'album écrits : mis en file, en pause, comme « Ajouter en pause » de « Nouveau morceau » (mêmes
    // réglages, mêmes LoRA cochées), avec le groupe, l'album et le n° de piste. Le formulaire est rendu tel quel.
    // Essai automatique de bout en bout (1.0.53), sans clic : STUDIO_TEST_AUTO_ALBUM="Influence|nombre de titres|type de rendu|rapport.txt".
    // Écrit l'album avec l'IA locale (plan, paroles, pochettes), lance la génération des morceaux, puis crée les clips YouTube
    // (scénario, images par scène, montage), et écrit le compte rendu. Appelé à chaque image ; ne fait rien sans la variable.
    int         auto_state = 0;
    double      auto_t0 = 0;
    std::string auto_infl, auto_style, auto_key, auto_formats = "yt";
    bool        skip_existing_clips = false;  // essai automatique : ne refait pas un clip déjà présent
    bool        video_per_album     = false;  // 1.0.82 : Shorts en série : dossier et pochette de l'album de chaque morceau
    std::wstring auto_report;
    int         auto_count = 8;
    void auto_log(const std::string & line) {
        if (auto_report.empty()) return;
        if (FILE * f = _wfopen(auto_report.c_str(), L"ab")) {
            fprintf(f, "[%5.0f s] %s\n", ImGui::GetTime() - auto_t0, line.c_str());
            fclose(f);
        }
    }
    void auto_album_step() {
        static const wchar_t * env = _wgetenv(L"STUDIO_TEST_AUTO_ALBUM");
        if (!env || auto_state >= 9) return;
        if (auto_state == 0) {
            std::vector<std::string> p;
            std::string              s = narrow(env);
            for (size_t b = 0;;) {
                size_t e = s.find('|', b);
                p.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
                if (e == std::string::npos) break;
                b = e + 1;
            }
            if (p.size() < 4) { auto_state = 9; return; }
            // textes de l'essai (exemples de la demande de Hervé) s'ils ne sont pas déjà réglés ; non enregistrés
            if (trim(settings.clip_text_start).empty()) settings.clip_text_start = "Rock Ballads Fr";
            if (trim(settings.clip_text_end).empty()) settings.clip_text_end = "Abonnez-vous\nRock Ballads Fr";
            if (p.size() >= 8) auto_formats = p[7];
            const bool clips_only = p.size() >= 7 && p[4] == "clips";  // « …|rapport|clips|Groupe|Album » : album déjà généré, seulement les clips
            auto_infl   = p[0];
            auto_count  = std::max(1, atoi(p[1].c_str()));
            auto_style  = p[2];
            auto_report = widen(p[3]);
            auto_t0     = ImGui::GetTime();
            if (clips_only) {
                auto_key        = album_key(p[5], p[6]);
                ai_album_name   = p[6];
                ai_album_sent   = 0;
                for (const Song & s : studio->snapshot())
                    if (!s.album.empty() && album_key(s.artist, s.album) == auto_key) ai_album_sent++;
                auto_log("Reprise : clips seulement, album « " + p[6] + " » (" + std::to_string(ai_album_sent) + " titres connus)");
                auto_state = 3;
                return;
            }
            settings.ai_influence = auto_infl;
            settings.ai_count = auto_count;
            settings.ai_instrumental = false;
            settings.ai_mixed = false;
            settings.ai_band.clear();
            settings.ai_album_title.clear();
            auto_log("Album demandé : influence « " + auto_infl + " », " + std::to_string(auto_count) + " titres");
            start_ai_album();
            if (!ai_album) { auto_log("ECHEC : l'album n'a pas pu démarrer (voir notifications)"); auto_state = 9; PostQuitMessage(0); return; }
            auto_state = 1;
        } else if (auto_state == 1) {  // écriture de l'album, puis pochettes
            if (ai_album) return;
            if (ai_album_sent == 0) { auto_log("ECHEC : aucun morceau écrit"); auto_state = 9; PostQuitMessage(0); return; }
            auto_key = album_key(ai_album_band, ai_album_name);
            auto_log("Album écrit : « " + ai_album_name + " » de " + ai_album_band + ", " + std::to_string(ai_album_sent) + " morceaux en file");
            auto_state = 2;
        } else if (auto_state == 2) {  // pochettes finies (carte graphique libre) avant de lancer les morceaux
            if (cover_job && cover_job->state == 0) return;
            auto_log("Pochettes terminées ; lancement de la génération des morceaux");
            studio->resume_all();
            auto_state = 3;
        } else if (auto_state == 3) {  // génération des morceaux
            int done = 0, bad = 0;
            for (const Song & s : studio->snapshot())
                if (!s.album.empty() && album_key(s.artist, s.album) == auto_key) {
                    if (s.status == "done") done++;
                    else if (s.status == "error" || s.status == "cancelled") bad++;
                }
            if (done + bad < ai_album_sent) return;
            if (studio->local_active() || ai_local_busy()) return;  // les images du clip ne se font qu'au repos
            auto_log(std::to_string(done) + " morceau(x) terminé(s), " + std::to_string(bad) + " en échec");
            std::vector<Song> songs = album_songs(studio->snapshot(), auto_key, false);
            if (songs.empty()) { auto_state = 9; PostQuitMessage(0); return; }
            open_video_dialog(songs, ai_album_name, auto_key);
            video_yt = auto_formats.find("yt") != std::string::npos;  // formats demandés (« yt,tt,ig »)
            video_tt = auto_formats.find("tt") != std::string::npos;
            video_ig = auto_formats.find("ig") != std::string::npos;
            video_clip = true;
            skip_existing_clips = true;  // reprise après interruption : un clip déjà fait n'est pas refait
            for (size_t i = 0; i < clip_styles().size(); i++)
                if (auto_style == clip_styles()[i].id) clip_style = (int) i;
            open_video = false;
            auto jobs_before = jobs.size();
            create_videos();
            if (jobs.size() == jobs_before) { auto_log("ECHEC : aucun clip n'a été lancé"); auto_state = 9; PostQuitMessage(0); return; }
            auto_log("Clips lancés (style « " + auto_style + " »)");
            auto_state = 4;
        } else if (auto_state == 4) {  // clips
            if (jobs.empty() || jobs.back()->state == 0) return;
            std::lock_guard<std::mutex> lock(jobs.back()->mutex);
            auto_log(std::string("Clips terminés : ") + jobs.back()->message);
            auto_state = 9;
            PostQuitMessage(0);
        }
    }

    void poll_ai_album() {
        if (!ai_album) return;
        auto                      job = ai_album;
        std::vector<AiAlbumTrack> todo;
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            todo.swap(job->ready);
            if (ai_album_band.empty()) ai_album_band = job->band;
            if (ai_album_name.empty()) ai_album_name = job->album;
            if (ai_album_sound.empty()) ai_album_sound = job->sound;
            if (ai_album_cover_prompt.empty()) ai_album_cover_prompt = job->cover_prompt;
        }
        // 1.0.47 : son commun gardé par album (pour le compléter ou faire un autre album du même groupe plus tard)
        if (!ai_album_band.empty() && !ai_album_name.empty() && !trim(ai_album_sound).empty()) {
            std::string & known = settings.album_sounds[album_key(ai_album_band, ai_album_name)];
            if (trim(known).empty()) {
                known = ai_album_sound;
                save_settings(settings, settings_path);
            }
        }
        if (!todo.empty()) {
            const Form saved = form;
            for (const AiAlbumTrack & t : todo) {
                Form & f = form;
                f.cover.clear();
                f.cover_seconds = 0;
                f.seed          = -1;
                // 1.0.40 : titre « 01. Titre » (numéro de piste sur 2 chiffres), sans doubler un préfixe déjà présent ;
                // les mots à éviter ont été vérifiés plus tôt, sur le titre sans numéro.
                AiSong numbered = t.song;
                const int number = t.number + ai_album_offset;
                {
                    char prefix[16];
                    snprintf(prefix, sizeof(prefix), "%02d. ", number);
                    if (numbered.title.rfind(prefix, 0) != 0) numbered.title = prefix + numbered.title;
                }
                fill_form_with(f, numbered, t.instrumental);
                f.artist     = ai_album_band;
                f.album      = ai_album_name;
                submit_track = number;
                submit_quiet = true;
                submit(true);
                submit_track = 0;
                submit_quiet = false;
                form         = saved;
                remember_ai_song(t.song, t.instrumental);
                char num[16];
                snprintf(num, sizeof(num), "%02d. ", number);
                ai_album_titles.push_back({ num + t.song.title + (t.instrumental ? std::string(" · ") + tr("Instrumental") : std::string()),
                                            ai_song_style(t.song.style), t.song, t.instrumental, number });
                ai_album_sent++;
            }
            scan_loras(true);
            sync_lora_triggers();  // style du formulaire rendu : mots déclencheurs comme avant
            save_last_album();     // 1.0.36 : retrouvé au prochain lancement
        }
        if (job->state != 1) return;
        ai_album.reset();
        std::string error;
        int         failed;
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            error  = job->error;
            failed = job->failed;
        }
        if (ai_album_sent > 0) {
            notify(trf("Album « %s » (%s) : %d morceau(x) ajouté(s) à la file, en pause.", ai_album_name.c_str(), ai_album_band.c_str(), ai_album_sent));
        }
        if (job->cancel) notify(tr("Album arrêté : les morceaux déjà écrits restent dans la file."));
        else if (!error.empty()) notify(error);
        if (failed > 0) notify(trf("%d morceau(x) de l'album illisible(s) : non ajouté(s).", failed));
        // 1.0.36 : pochettes de l'album, après l'écriture de tous les morceaux et leur mise en file (en pause).
        // Une image déjà présente n'est jamais remplacée ici (« Régénérer » de la fenêtre Albums, avec confirmation).
        if (ai_album_sent > 0 && !job->cancel && settings.ai_covers) {
            std::string key    = album_key(ai_album_band, ai_album_name);
            std::string prompt = trim(ai_album_cover_prompt).empty() ? default_cover_prompt(ai_album_name, ai_album_sound) : ai_album_cover_prompt;
            if (trim(settings.cover_prompts[key]).empty()) settings.cover_prompts[key] = prompt;
            save_settings(settings, settings_path);
            int  skipped = 0;
            auto tasks   = album_cover_tasks(key, ai_album_name, settings.cover_prompts[key], settings.ai_cover_youtube, settings.ai_cover_tiktok, true, skipped);
            if (skipped > 0) notify(trf("%d image(s) déjà présente(s) pour cet album : gardée(s).", skipped));
            start_covers(tasks);
        }
        save_last_album();
    }

    // « Nouveau morceau » contient-il déjà des paroles écrites (chanson) ou une structure modifiée (instrumental) ?
    bool form_has_content(bool instrumental) const {
        const Form & f = form;
        if (instrumental) return trim(f.lyrics_inst) != trim(Form{}.lyrics_inst) && !trim(f.lyrics_inst).empty();
        return has_lyrics_text(f.lyrics_song);
    }

    // Morceau écrit par l'IA, présenté pour la lecture (1.0.28) : une ligne vide entre titre, style, langue et
    // paroles. « Relire la réponse » sait relire ce texte (voir ai_text_for_parse).
    std::string ai_song_text(const AiSong & s, bool instrumental) const {
        std::string t = std::string(tr("Titre")) + " : " + (s.title.empty() ? "—" : s.title) + "\n\n";
        t += std::string(tr("Style")) + " : " + s.style + "\n\n";
        if (!instrumental && !s.language.empty()) t += std::string(tr("Langue")) + " : " + s.language + "\n\n";
        t += std::string(instrumental ? tr("Structure") : tr("Paroles")) + " :\n\n" + s.lyrics;
        return t;
    }

    // Les intitulés traduits (« Titre : », « Titel: »…) redeviennent ceux que la lecture connaît (« Title: »…).
    std::string ai_text_for_parse(const std::string & text) const {
        const std::pair<const char *, const char *> keys[] = {
            { N_("Titre"), "Title" }, { N_("Style"), "Style" }, { N_("Langue"), "Language" }, { N_("Structure"), "Structure" }, { N_("Paroles"), "Lyrics" } };
        std::string out;
        for (size_t start = 0; start < text.size();) {
            size_t      end  = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start            = end == std::string::npos ? text.size() : end + 1;
            for (const auto & [fr, en] : keys) {
                std::string label = tr(fr);
                if (line.rfind(label, 0) == 0) {
                    size_t c = line.find_first_not_of(" \t\xc2\xa0", label.size());
                    if (c != std::string::npos && line[c] == ':') line = std::string(en) + ":" + line.substr(c + 1);
                    break;
                }
            }
            out += line + "\n";
        }
        return out;
    }

    void draw_ai_tab() {
        poll_ai_job();
        poll_ai_album();
        if (settings.ai_language < 0) settings.ai_language = std::max(0, language_index_for_code("fr"));  // premier affichage : français
        ImGui::PushFont(g_header);
        ImGui::TextUnformatted(tr("Composer avec l'IA"));
        ImGui::PopFont();
        // 1.0.78 : tous les champs texte de la demande remis à zéro (choix, cases et réglages gardés)
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(tr("Vider les champs")).x - ImGui::GetStyle().FramePadding.x * 2);
        ImGui::BeginDisabled(ai_local_busy() || ai_job != nullptr);
        if (soft_button(tr("Vider les champs"), P.danger, false)) {
            for (std::string * f : { &settings.ai_influence, &settings.ai_theme, &settings.ai_mood, &settings.ai_title, &settings.ai_extra,
                                     &settings.ai_band, &settings.ai_album_title, &settings.ai_negative })
                f->clear();
            save_settings(settings, settings_path);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Efface influence, thème, ambiance, titre, souhaits, groupe, titre de l'album et styles à éviter."));
        draw_ai_tab_inner();
    }

    void draw_ai_tab_inner() {
        muted_wrapped(tr("Décrivez le morceau voulu : l'IA écrit le titre, le style, la langue et les paroles avec leur structure. Vérifiez le résultat, puis envoyez-le dans « Nouveau morceau »."));
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));

        // Moteur
        std::string why, label;
        bool        ready = ai_engine_ready(why, label);
        begin_card(P.violet, "ai_engine");
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Moteur"));
        ImGui::SameLine();
        ImGui::TextColored(ready ? P.ok : P.amber, "● %s", label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Réglages › IA…"))) {
            open_settings     = true;
            open_settings_cat = 6;
        }
        if (!why.empty()) colored_wrapped(P.amber, why);
        if (settings.ai_engine == "local" && local_llm.ready() && !local_llm.vulkan && gpu_profile().gpu.vram > 0) draw_llama_download();
        if (settings.ai_engine != "local" && settings.ai_engine != "server") {
            colored_wrapped(P.muted, tr("Service en ligne : la langue, l'influence et le thème sont envoyés à ce service."));
        }
        end_card();

        // Demande
        bool changed = false;
        begin_card(P.teal, "ai_request");
        const float label_w = 150 * g_dpi;
        auto        row     = [&](const char * text) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", text);
            ImGui::SameLine(label_w);
            ImGui::SetNextItemWidth(-FLT_MIN);
        };
        // Un morceau ou un album (1.0.33)
        const bool album = settings.ai_album_mode;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Créer"));
        ImGui::SameLine(label_w);
        if (ImGui::RadioButton(tr("Un morceau"), !album)) {
            settings.ai_album_mode = false;
            changed                = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Un album"), album)) {
            settings.ai_album_mode = true;
            changed                = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("L'IA conçoit l'album (groupe, titre, morceaux), puis écrit chaque morceau ; le modèle n'est chargé qu'une fois. Les morceaux sont ajoutés à la file, en pause, avec les réglages et les LoRA de « Nouveau morceau »."));
        // Chanté, instrumental ou (album) mélangé
        const bool mixed = album && settings.ai_mixed;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Type"));
        ImGui::SameLine(label_w);
        if (ImGui::RadioButton(tr("Chanson"), !settings.ai_instrumental && !mixed)) {
            settings.ai_instrumental = false;
            settings.ai_mixed        = false;
            changed                  = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Instrumental"), settings.ai_instrumental && !mixed)) {
            settings.ai_instrumental = true;
            settings.ai_mixed        = false;
            changed                  = true;
        }
        if (album) {
            ImGui::SameLine();
            if (ImGui::RadioButton(tr("Mélangé"), mixed)) {
                settings.ai_mixed = true;
                changed           = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Chansons et instrumentaux dans le même album."));
        }
        if (album) {
            row(tr("Nombre de morceaux"));
            changed |= ImGui::SliderInt("##ai_count", &settings.ai_count, 1, 50);
            settings.ai_count = std::clamp(settings.ai_count, 1, 50);
            if (mixed) {
                row(tr("Dont instrumentaux"));
                ImGui::SetNextItemWidth(120 * g_dpi);
                changed |= ImGui::InputInt("##ai_inst_n", &settings.ai_instrumentals);
                // 1.0.35 : instrumentaux au plus par album, réglés dans Réglages › IA (2 par défaut)
                int cap = std::max(0, std::min(settings.ai_max_instrumentals, settings.ai_count - 1));
                settings.ai_instrumentals = std::clamp(settings.ai_instrumentals, 0, cap);
                ImGui::SameLine();
                ImGui::TextColored(P.muted, "%s", trf("0 = aucun, %d au plus", cap).c_str());
            }
            row(tr("Titre de l'album"));
            changed |= tx::InputTextWithHint("##ai_album", tr("facultatif : sinon l'IA le choisit"), &settings.ai_album_title);
            row(tr("Nom du groupe"));
            // 1.0.47 (b3) : liste des groupes déjà dans la bibliothèque, à droite du champ
            {
                std::set<std::string> bands;
                for (const Song & s : studio->snapshot())
                    if (!trim(s.artist).empty() && s.status == "done") bands.insert(trim(s.artist));
                float pick_w = ImGui::CalcTextSize(tr("Groupe existant…")).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
                ImGui::SetNextItemWidth(std::max(120 * g_dpi, ImGui::GetContentRegionAvail().x - pick_w - ImGui::GetStyle().ItemSpacing.x));
                changed |= tx::InputTextWithHint("##ai_band", tr("facultatif : sinon l'IA l'invente"), &settings.ai_band);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(pick_w);
                ImGui::BeginDisabled(bands.empty());
                if (ImGui::BeginCombo("##ai_band_pick", tr("Groupe existant…"), ImGuiComboFlags_HeightLarge)) {
                    for (const std::string & b : bands) {
                        if (ImGui::Selectable(b.c_str(), fold(b) == fold(settings.ai_band))) {
                            settings.ai_band = b;
                            settings.ai_album_title.clear();  // nouvel album (pour compléter : « Albums… › Compléter… »)
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                AiAlbumRequest probe;
                probe.band  = trim(settings.ai_band);
                probe.album = trim(settings.ai_album_title);
                add_band_history(probe);
                if (!probe.used_titles.empty())
                    colored_wrapped(P.blue, probe.complete
                                                ? trf("Album existant : les nouveaux morceaux le complètent à partir de la piste %d, avec son son ; ses %d titres ne seront pas repris.",
                                                      band_album_last_track(probe.band, probe.album) + 1, (int) probe.used_titles.size())
                                                : trf("Groupe existant : son son est repris, et ses %d titres et %d album(s) ne seront pas réutilisés.",
                                                      (int) probe.used_titles.size(), (int) probe.used_albums.size()));
            }
            // Pochettes (1.0.36) : créées après l'écriture des morceaux et leur mise en file
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", tr("Pochettes"));
            ImGui::SameLine(label_w);
            changed |= ImGui::Checkbox(tr("Créer les pochettes"), &settings.ai_covers);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Une image carrée par album, créée après l'écriture de tous les morceaux et leur mise en file, en pause. Le service se choisit dans Réglages › IA › Pochettes."));
            ImGui::BeginDisabled(!settings.ai_covers);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("YouTube 16:9", &settings.ai_cover_youtube);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("TikTok 9:16", &settings.ai_cover_tiktok);
            ImGui::EndDisabled();
            if (settings.ai_covers) {
                std::string why, label;
                if (!image_engine_ready(why, label) && !cover_job) colored_wrapped(P.amber, why);  // sous les cases, dans la carte
            }
        }
        const bool sung = mixed || !settings.ai_instrumental;
        // Langue
        if (sung) {
            row(tr("Langue"));
            int li = std::clamp(settings.ai_language, 0, (int) LANGUAGES.size() - 1);
            if (ImGui::BeginCombo("##ai_lang", tr(LANGUAGES[li].label))) {
                for (int i = 0; i < (int) LANGUAGES.size(); i++) {
                    if (ImGui::Selectable(tr(LANGUAGES[i].label), i == li)) {
                        settings.ai_language = i;
                        changed              = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        // Influence, avec suggestions tirées des profils d'influence du Studio
        row(tr("Influence"));
        ImGui::SetNextItemWidth(-FLT_MIN - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        changed |= tx::InputTextWithHint("##ai_infl", tr("groupe ou chanteur (ex. AC/DC, Édith Piaf)"), &settings.ai_influence);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##ai_infl_list", ImGuiDir_Down)) ImGui::OpenPopup("ai_infl_pop");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Choisir parmi les profils d'influence du Studio"));
        if (ImGui::BeginPopup("ai_infl_pop")) {
            std::string want = fold(trim(settings.ai_influence));
            ImGui::BeginChild("##ai_infl_scroll", ImVec2(280 * g_dpi, 300 * g_dpi));
            for (const Influence & inf : INFLUENCES) {
                if (!want.empty() && fold(inf.name).find(want) == std::string::npos) continue;
                if (ImGui::Selectable(inf.name)) {
                    settings.ai_influence = inf.name;
                    changed               = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        row(tr("Thème"));
        changed |= tx::InputTextWithHint("##ai_theme", tr("facultatif (ex. la mer, un amour perdu)"), &settings.ai_theme);
        row(tr("Ambiance"));
        changed |= tx::InputTextWithHint("##ai_mood", tr("facultatif (ex. joyeuse, sombre, épique)"), &settings.ai_mood);
        if (!album) {
            row(tr("Titre imposé"));
            changed |= tx::InputTextWithHint("##ai_title", tr("facultatif : sinon l'IA le choisit"), &settings.ai_title);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Longueur"));
        ImGui::SameLine(label_w);
        // 1.0.49 : durées revues (les consignes imposent aussi le nombre de lignes) et « Très longue » = morceau en 2 parties
        const char * lengths[] = { tr("Courte (≈ 2-3 min)"), tr("Normale (≈ 3-4 min)"), tr("Longue (≈ 5-6 min)"), tr("Très longue (≈ 9-11 min, 3 parties)") };
        for (int i = 0; i < 4; i++) {
            if (i == 3) ImGui::NewLine(), ImGui::SameLine(label_w);
            else if (i) ImGui::SameLine();
            if (ImGui::RadioButton(lengths[i], settings.ai_length == i)) {
                settings.ai_length = i;
                changed            = true;
            }
            if (i == 3 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("L'IA écrit une structure assez longue pour 9 à 11 minutes ; à l'envoi, la case « Morceau long : 2 ou 3 parties enchaînées » est cochée toute seule et le morceau est généré en deux parties assemblées."));
        }
        row(tr("Autres souhaits"));
        changed |= tx::InputTextMultiline("##ai_extra", &settings.ai_extra, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2.6f));
        // Styles à éviter (1.0.34), comme dans « Nouveau morceau » : transmis à l'IA, retirés du style écrit, et
        // appliqués au « Style à éviter » de chaque morceau envoyé. Repris de « Nouveau morceau » au premier affichage.
        {
            static bool seeded = false;
            if (!seeded) {
                seeded = true;
                if (trim(settings.ai_negative).empty() && !trim(form.negative_style).empty()) {
                    settings.ai_negative = form.negative_style;
                    changed              = true;
                }
            }
            row(tr("Styles à éviter"));
            const char * take  = tr("Reprendre de « Nouveau morceau »");
            float        btn_w = ImGui::CalcTextSize(take).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(std::max(120 * g_dpi, ImGui::GetContentRegionAvail().x - btn_w));
            changed |= ImGui::InputTextWithHint("##ai_neg", tr("Ex. : vocoder, autotune, synthesizer"), &settings.ai_negative);
            if (ImGui::IsItemDeactivatedAfterEdit()) remember_negative_tags(settings.ai_negative);
            changed |= text_context_menu(settings.ai_negative);
            ImGui::SameLine();
            if (ImGui::SmallButton(take)) {
                settings.ai_negative = form.negative_style;
                changed              = true;
            }
            changed |= draw_negative_chips(&settings.ai_negative, 2);
        }
        {   // Profil d'influence connu : ses balises peuvent compléter le style
            std::string want  = fold(trim(settings.ai_influence));
            bool        known = false;
            for (const Influence & inf : INFLUENCES) known |= !want.empty() && fold(inf.name) == want;
            if (known) changed |= ImGui::Checkbox(tr("Compléter le style avec le profil d'influence du Studio"), &settings.ai_profile);
        }
        // Mots évités (1.0.33) : liste de Réglages › IA + mots fréquents des derniers morceaux
        {
            size_t auto_n = ai_auto_words().size();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.muted, "%s", tr("Mots à éviter"));
            ImGui::SameLine(label_w);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(trf("%d dans la liste, %d automatique(s)", (int) settings.ai_avoid.size(), (int) auto_n).c_str());
            if (ImGui::IsItemHovered()) {
                std::string list;
                for (const std::string & w : ai_avoid_words()) list += (list.empty() ? "" : ", ") + w;
                ImGui::SetTooltip("%s", list.empty() ? tr("Aucun") : list.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(tr("Modifier…")) + "##avoid").c_str())) {
                open_settings     = true;
                open_settings_cat = 6;
            }
        }
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        colored_wrapped(P.accent, album ? ai_album_summary().c_str() : ai_summary().c_str());
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        bool running = ai_job != nullptr || ai_album != nullptr;
        ImGui::BeginDisabled(running || !ready);
        if (accent_button(album ? tr("Composer l'album") : tr("Composer"), ImVec2(0, 0), &P.violet)) {
            if (album) start_ai_album();
            else start_ai_compose();
        }
        ImGui::EndDisabled();
        if (!ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
        if (ai_album) {
            ImGui::SameLine();
            int         secs = (int) (ImGui::GetTime() - ai_album->started);
            int         cur  = ai_album->current, total = ai_album->total;
            std::string state;
            if (ai_album->stage == 0) state = trf("Chargement du modèle… %d s", secs);
            else if (cur == 0) state = trf("Plan de l'album… %d s", secs);
            else {
                std::string title;
                {
                    std::lock_guard<std::mutex> lock(ai_album->mutex);
                    title = ai_album->current_title;
                }
                state = title.empty() ? trf("Morceau %d/%d… %d s", cur, total, secs) : trf("Morceau %d/%d : « %s »… %d s", cur, total, title.c_str(), secs);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.violet, "%s", state.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(tr("Arrêter")) + "##album").c_str())) ai_album->cancel = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Arrête l'album : les morceaux déjà écrits restent dans la file."));
            char done[32];
            snprintf(done, sizeof(done), "%d / %d", ai_album_sent, total);
            progress_bar(total > 0 ? (float) std::max(0, cur - 1) / (float) total : 0.0f, ImVec2(-FLT_MIN, 0), done);
        }
        if (ai_job) {
            ImGui::SameLine();
            int         secs  = (int) (ImGui::GetTime() - ai_job->started);
            int         stage = ai_job->stage;
            std::string state = stage == 0 ? trf("Chargement du modèle… %d s", secs) : trf("L'IA écrit… %d s", secs);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.violet, "%s", state.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Arrêter"))) ai_job->cancel = true;
            if (ai_job->local && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Arrête le moteur local et libère la mémoire."));
        }
        end_card();
        if (changed) save_settings(settings, settings_path);

        // LoRA (1.0.35) : le même bloc que dans « Nouveau morceau », sur les mêmes données (form.loras) : cocher,
        // décocher ou changer une force ici se voit aussitôt dans l'autre onglet, et inversement.
        draw_lora_section();

        // Album en cours ou terminé : morceaux mis en file (1.0.33)
        if (album || !ai_album_titles.empty()) {
            begin_card(P.green, "ai_album_result");
            ImGui::PushFont(g_bold);
            ImGui::TextColored(P.green, "%s", ai_album_name.empty() ? tr("Album") : trf("Album « %s »", ai_album_name.c_str()).c_str());
            ImGui::PopFont();
            if (!ai_album_band.empty()) colored_wrapped(P.muted, trf("Groupe : %s", ai_album_band.c_str()).c_str());
            if (ai_album_titles.empty()) muted_wrapped(tr("Les morceaux de l'album apparaîtront ici à mesure qu'ils sont écrits ; chacun est ajouté à la file, en pause : lancez-les depuis la file quand vous voulez."));
            if (!ai_album_sound.empty()) {
                colored_wrapped(P.muted, trf("Son de l'album : %s", ai_album_sound.c_str()).c_str());
                if (ImGui::SmallButton((std::string(tr("Enregistrer comme style personnalisé…")) + "##album_sound").c_str()))
                    open_ai_style_save(ai_album_sound, ai_album_band.empty() ? ai_album_name : ai_album_band);
            }
            for (size_t i = 0; i < ai_album_titles.size(); i++) {
                const AiAlbumLine & t = ai_album_titles[i];
                ImGui::PushID((int) i);
                // 1.0.36 : un clic affiche le morceau (style, structure, paroles) dans « Résultat »
                if (ImGui::Selectable(t.label.c_str(), ai_album_selected == (int) i, ImGuiSelectableFlags_None,
                                      ImVec2(ImGui::CalcTextSize(t.label.c_str()).x + 8 * g_dpi, 0)) && t.song.ok) {
                    ai_album_selected    = (int) i;
                    ai_song              = t.song;
                    ai_song_instrumental = t.instrumental;
                    ai_response          = ai_song_text(t.song, t.instrumental);
                }
                if (!t.style.empty()) {
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.style.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Enregistrer le style…"))) {
                        std::string name = t.label.size() > 4 ? t.label.substr(4) : t.label;  // sans « 01. »
                        size_t      dot  = name.find(" · ");
                        open_ai_style_save(t.style, dot == std::string::npos ? name : name.substr(0, dot));
                    }
                }
                ImGui::PopID();
            }
            // Pochettes (1.0.36) : progression, ou bouton pour les créer après coup (album déjà écrit)
            if (cover_job) draw_cover_progress();
            else if (!ai_album_titles.empty() && !ai_album_name.empty() && !ai_album) {
                std::string key = album_key(ai_album_band, ai_album_name);
                if (!cover_for(key).empty()) ImGui::TextColored(P.ok, "%s", tr("● pochette de l'album présente (fenêtre « Albums… »)"));
                else if (ImGui::SmallButton(tr("Créer la pochette de l'album"))) {
                    std::string prompt = trim(ai_album_cover_prompt).empty() ? default_cover_prompt(ai_album_name, ai_album_sound) : ai_album_cover_prompt;
                    if (trim(settings.cover_prompts[key]).empty()) settings.cover_prompts[key] = prompt;
                    int skipped = 0;
                    start_covers(album_cover_tasks(key, ai_album_name, settings.cover_prompts[key], settings.ai_cover_youtube, settings.ai_cover_tiktok, true, skipped));
                }
            }
            end_card();
        }

        // Résultat
        begin_card(P.accent, "ai_result");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.accent, "%s", tr("Résultat"));
        ImGui::PopFont();
        if (ai_song.ok) {
            ImGui::TextColored(P.muted, "%s", tr("Titre"));
            ImGui::SameLine(label_w);
            ImGui::TextWrapped("%s", ai_song.title.empty() ? "—" : ai_song.title.c_str());
            ImGui::TextColored(P.muted, "%s", tr("Style"));
            ImGui::SameLine(label_w);
            ImGui::TextWrapped("%s", ai_song.style.c_str());
            if (!ai_song.language.empty()) {
                ImGui::TextColored(P.muted, "%s", tr("Langue"));
                ImGui::SameLine(label_w);
                ImGui::TextUnformatted(ai_song.language.c_str());
            }
            ImGui::TextColored(P.muted, "%s", tr("Structure"));
            ImGui::SameLine(label_w);
            ImGui::TextWrapped("%s", arrow_text(structure_tags(ai_song.lyrics)).c_str());
            if (ai_song.removed > 0) colored_wrapped(P.muted, trf("%d description(s) ou indication(s) retirée(s) par le nettoyage.", ai_song.removed).c_str());
            if (accent_button(tr("Envoyer dans Nouveau morceau"), ImVec2(0, 0), &P.accent)) {
                if (form_has_content(ai_song_instrumental)) ai_confirm_send = true;
                else send_ai_song();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Remplit le titre, le style (avec la langue) et les paroles de « Nouveau morceau »."));
            ImGui::SameLine();
            if (ImGui::Button(tr("Enregistrer comme style personnalisé…"))) open_ai_style_save(ai_song_style(ai_song.style), ai_song.title);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ajoute ce style à vos styles (liste « Préréglages… » du bloc Style)."));
            ImGui::SameLine();
        } else if (!ai_response.empty()) {
            colored_wrapped(P.amber, tr("Réponse illisible : corrigez-la ci-dessous puis « Relire la réponse », ou copiez les éléments à la main."));
        } else {
            muted_wrapped(tr("Le morceau écrit par l'IA apparaîtra ici."));
        }
        ImGui::BeginDisabled(ai_response.empty());
        if (ImGui::Button(tr("Relire la réponse"))) {
            ai_song = ai_parse_song(ai_text_for_parse(ai_response), settings.ai_instrumental);
            ai_song_instrumental = settings.ai_instrumental;
            if (!ai_song.ok) notify(tr("Réponse toujours illisible : il faut au moins un style et des paroles avec des balises [Verse], [Chorus]…"));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Relit et nettoie le texte du champ ci-dessous (après une correction à la main, ou une réponse collée depuis un autre outil)."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Copier"))) ImGui::SetClipboardText(ai_response.c_str());
        ImGui::EndDisabled();
        ImGui::TextColored(P.muted, "%s", tr("Réponse de l'IA"));
        tx::InputTextMultiline("##ai_response", &ai_response, ImVec2(-FLT_MIN, 260 * g_dpi));
        end_card();

        // Confirmation : le formulaire contient déjà un morceau
        if (ai_confirm_send) {
            ImGui::OpenPopup(tr("Remplacer le morceau en cours ?"));
            ai_confirm_send = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Remplacer le morceau en cours ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", tr("« Nouveau morceau » contient déjà un titre ou des paroles. Les remplacer par le morceau écrit par l'IA ?"));
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (accent_button(tr("Remplacer"), ImVec2(0, 0), &P.accent)) {
                send_ai_song();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        draw_ai_style_modal();  // 1.0.34
    }

    // ---- Réglages › IA

    void start_ai_model_download(const AiModelFile & m) {
        std::error_code ec;
        fs::create_directories(ai_models_dir(), ec);
        auto job = downloader.start(std::string("ia:") + m.id, m.name, ai_models_dir(), { { ai_model_url(m), m.file, m.bytes } });
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(trf("Téléchargement du modèle IA « %s » lancé.", m.name));
    }
    void start_llama_download() {
        const LlamaPackage & p = llama_vulkan_package();
        std::error_code      ec;
        fs::create_directories(llama_dir(), ec);
        auto job = downloader.start("llama_vulkan", "llama.cpp (Vulkan)", llama_dir(), { { p.url, p.file, p.bytes } });
        if (std::find(tracked_downloads.begin(), tracked_downloads.end(), job) == tracked_downloads.end()) tracked_downloads.push_back(job);
        announced_downloads.erase(std::remove(announced_downloads.begin(), announced_downloads.end(), job.get()), announced_downloads.end());
        notify(tr("Téléchargement de llama.cpp pour la carte graphique lancé."));
    }
    // Fin d'un téléchargement de l'IA (appelé par update_downloads). Renvoie vrai si la clé lui appartenait.
    bool finish_ai_download(DownloadJob & j) {
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
    void poll_llama_extract() {
        int s = *llama_extract_state;
        if (s < 2) return;
        *llama_extract_state = 0;
        refresh_local_llm(true);
        if (s == 2) notify(tr("llama.cpp pour la carte graphique est prêt : l'IA locale utilisera la carte graphique au repos."));
        else notify(trf("Installation de llama.cpp impossible : %s", tr(llama_extract_error->c_str())));
    }

    // Choix du moteur : bouton radio d'une ligne.
    void ai_engine_radio(const char * label, const std::string & id) {
        if (ImGui::RadioButton(label, settings.ai_engine == id)) {
            settings.ai_engine = id;
            save_settings(settings, settings_path);
        }
    }

    // Bouton « Télécharger llama.cpp pour la carte graphique » (avec sa barre de progression), dans Réglages › IA et,
    // depuis 1.0.28, dans l'onglet « Composer avec l'IA » quand il manque. Le téléchargement ne part qu'au clic.
    void draw_llama_download() {
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

    void draw_ai_settings() {
        draw_ai_settings_inner();
    }

    void draw_ai_settings_inner() {
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
                    if (std::string(m.id) == "klein") muted_wrapped(tr("Le plus rapide, très bon rendu, titre bien écrit dans l'image ; tient sur une carte de 8 Go."));
                    else if (std::string(m.id) == "zimage") muted_wrapped(tr("Rendu proche de la photo ; il faut environ 9 Go de mémoire vidéo."));
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
                muted_wrapped(tr("Modèles sous licence Apache 2.0 (FLUX.2 klein : Black Forest Labs ; Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS)."));
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
    bool draw_ai_model_picker(const std::string & target, std::string & model) {
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

    void draw_player(const Song & s) {
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

    // Boutons à la ligne : passe à la ligne suivante quand la largeur manque.
    struct Flow {
        float avail, x = 0;
        bool  first = true;
        explicit Flow() : avail(ImGui::GetContentRegionAvail().x) {}
        void next(const char * text) {
            float w = ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2;
            if (!first && x + ImGui::GetStyle().ItemSpacing.x + w <= avail) {
                ImGui::SameLine();
                x += ImGui::GetStyle().ItemSpacing.x + w;
            } else {
                x = w;
            }
            first = false;
        }
    };

    static ImVec4 status_color(const Song & s) {
        if (s.status == "running") return P.accent;
        if (s.status == "queued") return s.held ? P.amber : P.blue;
        if (s.status == "done") return is_new(s) ? P.pink : P.ok;  // rose : en attente d'écoute et de vérification
        if (s.status == "interrupted") return P.orange;
        return P.danger;
    }

    static const char * status_label(const Song & s) {
        if (s.status == "running") return tr("En cours");
        if (s.status == "queued") return s.held ? tr("En pause") : tr("En attente");
        if (s.status == "done") return tr("Terminée");
        if (s.status == "cancelled") return tr("Annulée");
        if (s.status == "interrupted") return tr("Interrompue");
        return tr("Erreur");
    }

    // Statistiques d'un morceau (durée, temps, graine, modèle, solveur, LoRA) ; message d'erreur éventuel.
    void draw_song_stats(const Song & s, const ImVec4 & color) {
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

    void draw_song(const Song & s, int queue_pos, bool collapsible = false) {
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
        if (s.kind == "extend") tag(tr("Suite"), P.violet);
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

    // ------------------------------------------------------------------ Affichages de carte : compacte, pochette

    std::set<std::string> expanded_cards;  // cartes dépliées en affichage compact

    static std::string ellipsize(const std::string & text, float width) {
        if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
        std::string cut = text;
        while (!cut.empty() && ImGui::CalcTextSize((cut + "…").c_str()).x > width) {
            do cut.pop_back(); while (!cut.empty() && (cut.back() & 0xC0) == 0x80);  // ne coupe pas un caractère UTF-8
        }
        return cut + "…";
    }

    // Lecture / pause d'un morceau (bouton des affichages compact et pochette).
    void toggle_play(const Song & s) {
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

    void export_song(const Song & s, const char * format) {
        std::wstring target = save_dialog(hwnd, s, format);
        if (target.empty()) return;
        fs::path src = song_folder(s.id) / (std::string("audio.") + format);
        notify(CopyFileW(src.c_str(), target.c_str(), FALSE) ? tr("Exporté : ") + narrow(target) : tr("Échec de l'export vers ") + narrow(target));
    }

    // Menu « Actions » d'un morceau terminé.
    void draw_song_menu(const Song & s) {
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

    std::string song_summary(const Song & s, bool with_lang = true) const {
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
        return out;
    }

    // 1.0.62 : logos des plateformes sur la ligne de titre : gris, en couleur quand le clip du morceau existe. Largeur 0 si le morceau
    // ne peut pas avoir de clip (pas terminé, ou partie 1 d'un morceau long). Fichiers cherchés comme les écrit la fenêtre « Créer des vidéos ».
    float clip_logos_width(const Song & s) const {
        return s.status == "done" && s.long_rest.empty() ? (4 * 17 + 3 * 5) * g_dpi : 0;  // 1.0.78 : 4e logo, les Shorts
    }
    void draw_clip_logos(const Song & s) {
        if (clip_logos_width(s) <= 0) return;
        struct Net { const char * name; const wchar_t * dir, * suffix; };
        static const Net NETS[3] = { { "YouTube", L"YouTube", L"YouTube" }, { "TikTok", L"TikTok", L"TikTok" }, { "Instagram", L"IG", L"Instagram" } };
        const fs::path   videos = album_folder(s.artist, s.album) / L"videos";
        const std::wstring base = widen(safe_filename(display_title(s.title)));
        const float      h = 17 * g_dpi, step = 22 * g_dpi;
        ImVec2           p0 = ImGui::GetCursorScreenPos();
        ImDrawList *     dl = ImGui::GetWindowDrawList();
        const ImU32      grey = ImGui::GetColorU32(alpha(P.muted, 0.45f)), hole = ImGui::GetColorU32(P.card), white = IM_COL32_WHITE;
        for (int i = 0; i < 3; i++) {
            const fs::path stem = videos / NETS[i].dir / (base + L"_" + NETS[i].suffix);
            bool   clip  = cached_exists(fs::path(stem.wstring() + L"_Clip.mp4"));
            bool   fixed = !clip && cached_exists(fs::path(stem.wstring() + L".mp4"));  // 1.0.63 : vidéo fixe : couleur atténuée
            bool   done  = clip || fixed;
            auto   tone  = [&](ImU32 c) { return clip ? c : (c & 0x00FFFFFFu) | (0x70u << 24); };
            ImVec2 o    = p0 + ImVec2(step * i, 0);
            auto   at   = [&](float x, float y) { return o + ImVec2(x * h, y * h); };
            if (i == 0) {  // écran arrondi et triangle de lecture
                dl->AddRectFilled(at(0, .16f), at(1, .84f), done ? tone(IM_COL32(255, 0, 0, 255)) : grey, h * .2f);
                dl->AddTriangleFilled(at(.38f, .32f), at(.38f, .68f), at(.70f, .50f), done ? tone(white) : hole);
            } else if (i == 1) {  // note de musique, doublée en bleu et rouge quand le clip existe
                auto note = [&](float dx, float dy, ImU32 c) {
                    dl->AddCircleFilled(at(.36f + dx, .72f + dy), h * .20f, c);
                    dl->AddLine(at(.53f + dx, .72f + dy), at(.53f + dx, .10f + dy), c, h * .15f);
                    dl->AddBezierQuadratic(at(.53f + dx, .12f + dy), at(.60f + dx, .34f + dy), at(.86f + dx, .36f + dy), c, h * .13f);
                };
                if (done) {
                    note(-.05f, -.03f, tone(IM_COL32(37, 244, 238, 255)));
                    note(.05f, .03f, tone(IM_COL32(254, 44, 85, 255)));
                }
                note(0, 0, done ? tone(white) : grey);
            } else {  // appareil photo : carré arrondi, objectif et point
                ImU32 back = done ? tone(IM_COL32(214, 36, 120, 255)) : grey, line = done ? tone(white) : hole;
                dl->AddRectFilled(at(.04f, .04f), at(.96f, .96f), back, h * .26f);
                dl->AddRect(at(.18f, .18f), at(.82f, .82f), line, h * .17f, 0, h * .08f);
                dl->AddCircle(at(.5f, .5f), h * .17f, line, 0, h * .08f);
                dl->AddCircleFilled(at(.68f, .32f), h * .045f, line);
            }
            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(o, o + ImVec2(h, h)))
                ImGui::SetTooltip("%s", (clip ? trf("%s : clip créé", NETS[i].name) : fixed ? trf("%s : vidéo fixe, pas de clip", NETS[i].name) : trf("%s : pas encore de clip", NETS[i].name)).c_str());
        }
        {  // 1.0.78 : Short (9:16) : rectangle vertical rouge et triangle blanc, fichier videos\Shorts\<titre>_Short.mp4
            bool   done = cached_exists(videos / L"Shorts" / (base + L"_Short.mp4"));
            ImVec2 o    = p0 + ImVec2(step * 3, 0);
            auto   at   = [&](float x, float y) { return o + ImVec2(x * h, y * h); };
            dl->AddRectFilled(at(.22f, 0), at(.78f, 1), done ? IM_COL32(255, 0, 51, 255) : grey, h * .16f);
            dl->AddTriangleFilled(at(.40f, .34f), at(.40f, .66f), at(.66f, .50f), done ? white : hole);
            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(o, o + ImVec2(h, h)))
                ImGui::SetTooltip("%s", done ? tr("Short : créé") : tr("Short : pas encore créé"));
        }
        ImGui::Dummy(ImVec2(clip_logos_width(s), h));
    }

    // Une ligne par morceau terminé ; « ▾ » déplie la carte complète.
    // Badge « (disque) Album · n° piste » d'un morceau d'album (1.0.37) : disque dessiné (pas de police d'emoji),
    // survol = groupe et album, clic = la bibliothèque n'affiche plus que cet album (champ de recherche).
    void album_badge(const Song & s) {
        if (s.album.empty()) return;
        ImVec4      c    = album_color(album_key(s.artist, s.album));
        std::string text = "     " + (s.track.empty() ? std::string(tr("Album")) : trf("Album · n° %s", s.track.c_str()));
        ImVec2      p    = ImGui::GetCursorScreenPos();
        float       r    = ImGui::GetTextLineHeight() * 0.34f;
        pill(text.c_str(), c);
        ImVec2       mid(p.x + 7 * g_dpi + r, p.y + 1 * g_dpi + ImGui::GetTextLineHeight() * 0.5f);
        ImDrawList * dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(mid, r, ImGui::GetColorU32(c));
        dl->AddCircleFilled(mid, r * 0.32f, ImGui::GetColorU32(P.card));
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            std::string who = s.artist + (!s.artist.empty() ? "  —  " : "") + s.album;
            ImGui::SetTooltip("%s\n%s", who.c_str(), tr("Cliquer pour n'afficher que cet album."));
        }
        if (ImGui::IsItemClicked()) filter_text = s.album;
    }
    // Liseré de la couleur de l'album sur le bord DROIT de la carte en cours (1.0.38, demande de Herve : plus visible,
    // et distinct des couleurs d'état déjà présentes à gauche).
    void album_stripe(const Song & s) {
        if (s.album.empty()) return;
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp + ImVec2(ws.x - 6 * g_dpi, 0), wp + ws, ImGui::GetColorU32(album_color(album_key(s.artist, s.album))),
                                                  ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersRight);
    }

    void draw_song_compact(const Song & s) {
        ImGui::PushID(s.id.c_str());
        ImVec4 color = status_color(s);
        bool fresh = is_new(s);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, fresh ? tint(P.card, color, 0.13f) : P.card);
        ImGui::PushStyleColor(ImGuiCol_Border, fresh ? alpha(color, 0.65f) : P.line);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 6 * g_dpi));
        ImGui::BeginChild("compact", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        album_stripe(s);  // 1.0.37 ; à droite depuis la 1.0.38
        bool playing = player.id() == s.id && player.playing();
        float thumb_sz = 22 * g_dpi, arrow_w = 30 * g_dpi, gap = 6 * g_dpi;
        std::string summary = song_summary(s);
        float logos_w = clip_logos_width(s);
        float right_w = ImGui::CalcTextSize(summary.c_str()).x + gap + marks_width(settings.mark_style, thumb_sz) + (logos_w > 0 ? logos_w + 10 * g_dpi : 0);
        if (accent_button(playing ? "⏸" : "▶", ImVec2(32 * g_dpi, 0), &P.green)) toggle_play(s);
        ImGui::SameLine(0, 10 * g_dpi);
        ImGui::AlignTextToFramePadding();
        float new_w   = fresh ? pill_width(tr("À écouter")) + 8 * g_dpi : 0;
        float title_w = ImGui::GetContentRegionAvail().x - right_w - 10 * g_dpi - new_w;
        if (fresh) {
            pill(tr("À écouter"), P.pink);
            ImGui::SameLine(0, 8 * g_dpi);
        }
        ImGui::PushFont(g_bold);
        std::string title = ellipsize(s.title, std::max(60.f * g_dpi, title_w));
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        {   // un clic sur le titre déplie la carte
            ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + std::max(60.f * g_dpi, title_w), ImGui::GetItemRectMax().y);
            if (title_zone(a, b, s.title + "\n" + s.style + tr("\n\nCliquer pour déplier"))) expanded_cards.insert(s.id);
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - right_w);
        if (logos_w > 0) {
            float y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(y + (ImGui::GetFrameHeight() - 17 * g_dpi) / 2);
            draw_clip_logos(s);
            ImGui::SameLine(0, 10 * g_dpi);
            ImGui::SetCursorPosY(y);
            ImGui::AlignTextToFramePadding();
        }
        ImGui::TextColored(P.muted, "%s", summary.c_str());
        ImGui::SameLine(0, gap);
        apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(4 * g_dpi, ws.y), ImGui::GetColorU32(color),
                                                  ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    // Clé de la pochette d'un morceau : celle de son album (partagée par ses morceaux), sinon propre au morceau.
    static std::string cover_key(const std::string & id, const std::string & artist, const std::string & album) {
        return album.empty() ? "titre:" + id : album_key(artist, album);
    }

    // Pochette carrée (image rognée au centre) ou, à défaut, un aplat dérivé du titre avec son initiale.
    void draw_cover_art(const std::string & key, const std::string & title, float side) {
        ImVec2       p0 = ImGui::GetCursorScreenPos();
        ImDrawList * dl = ImGui::GetWindowDrawList();
        ImTextureID  tex = 0;
        ImVec2       tsize;
        std::string  cover_path = cover_for(key);
        // Sans pochette carrée, on se rabat sur l'image YouTube ou TikTok de l'album (rognée au centre)
        if (cover_path.empty()) cover_path = cover_for(slot_youtube(key));
        if (cover_path.empty()) cover_path = cover_for(slot_tiktok(key));
        float        r = ImGui::GetStyle().ChildRounding * 0.6f;
        if (!cover_path.empty() && thumb_texture(fs::path(widen(cover_path)), tex, tsize, 256)) {
            float k = std::max(side / tsize.x, side / tsize.y);  // remplit le carré, rogné au centre
            float u = (1 - side / (tsize.x * k)) / 2, v = (1 - side / (tsize.y * k)) / 2;
            dl->AddImageRounded(tex, p0, p0 + ImVec2(side, side), ImVec2(u, v), ImVec2(1 - u, 1 - v), IM_COL32_WHITE, r);
        } else {
            unsigned h = 2166136261u;
            for (char c : title) h = (h ^ (unsigned char) c) * 16777619u;
            const ImVec4 * palette[] = { &P.violet, &P.pink, &P.teal, &P.orange, &P.blue, &P.green, &P.amber };
            ImVec4         base      = *palette[h % 7];
            dl->AddRectFilled(p0, p0 + ImVec2(side, side), ImGui::GetColorU32(alpha(base, 0.35f)), r);
            dl->AddRect(p0, p0 + ImVec2(side, side), ImGui::GetColorU32(alpha(base, 0.8f)), r);
            std::string t = clean_title(title), initial = "♪";
            for (char c : t) {
                if (isalnum((unsigned char) c)) {
                    initial = std::string(1, (char) toupper((unsigned char) c));
                    break;
                }
            }
            ImFont * font = side > 60 * g_dpi ? g_header : g_bold;
            ImGui::PushFont(font);
            ImVec2 ts = ImGui::CalcTextSize(initial.c_str());
            dl->AddText(p0 + ImVec2((side - ts.x) / 2, (side - ts.y) / 2), ImGui::GetColorU32(base), initial.c_str());
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(side, side));
    }

    // Une ligne par morceau terminé, avec une petite pochette ; « ▾ » déplie la carte complète avec tous ses détails.
    void draw_song_cover_compact(const Song & s) {
        ImGui::PushID(s.id.c_str());
        ImVec4 color = status_color(s);
        bool   fresh = is_new(s);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, fresh ? tint(P.card, color, 0.13f) : P.card);
        ImGui::PushStyleColor(ImGuiCol_Border, fresh ? alpha(color, 0.65f) : P.line);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpi, 7 * g_dpi));
        ImGui::BeginChild("covercompact", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        album_stripe(s);  // 1.0.37
        float side = 46 * g_dpi, gap = 8 * g_dpi, thumb_sz = 22 * g_dpi, play_w = 34 * g_dpi;
        float row_y = ImGui::GetCursorPosY();
        draw_cover_art(cover_key(s.id, s.artist, s.album), s.title, side);
        ImGui::SameLine(0, 10 * g_dpi);
        std::string summary = song_summary(s);
        float       logos_w = clip_logos_width(s);
        float       right_w = play_w + gap + ImGui::CalcTextSize(summary.c_str()).x + gap + marks_width(settings.mark_style, thumb_sz) + (logos_w > 0 ? logos_w + gap : 0);
        float       title_w = ImGui::GetContentRegionAvail().x - right_w - 14 * g_dpi;
        ImGui::BeginGroup();
        ImGui::PushFont(g_bold);
        float pill_w = fresh ? pill_width(tr("À écouter")) + 8 * g_dpi : 0;
        if (fresh) {
            pill(tr("À écouter"), P.pink);
            ImGui::SameLine(0, 8 * g_dpi);
        }
        std::string title = ellipsize(s.title, std::max(60.f * g_dpi, title_w - pill_w));
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        {   // un clic sur le titre déplie la carte
            ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + std::max(60.f * g_dpi, title_w - pill_w), ImGui::GetItemRectMax().y);
            if (title_zone(a, b, s.title + "\n" + s.style + tr("\n\nCliquer pour déplier"))) expanded_cards.insert(s.id);
        }
        std::string who = s.artist + (!s.artist.empty() && !s.album.empty() ? "  —  " : "") + s.album;
        if (!who.empty()) ImGui::TextColored(P.blue, "%s", ellipsize(who, std::max(60.f * g_dpi, title_w)).c_str());
        ImGui::EndGroup();
        // commandes alignées au centre de la pochette
        float center_y = row_y + (side - ImGui::GetFrameHeight()) / 2;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - right_w);
        if (logos_w > 0) {
            ImGui::SetCursorPosY(center_y + (ImGui::GetFrameHeight() - 17 * g_dpi) / 2);
            draw_clip_logos(s);
            ImGui::SameLine(0, gap);
        }
        ImGui::SetCursorPosY(center_y);
        bool playing = player.id() == s.id && player.playing();
        if (accent_button(playing ? "⏸" : "▶", ImVec2(play_w, 0), &P.green)) toggle_play(s);
        ImGui::SameLine(0, gap);
        ImGui::SetCursorPosY(center_y);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", summary.c_str());
        ImGui::SameLine(0, gap);
        ImGui::SetCursorPosY(center_y + (ImGui::GetFrameHeight() - thumb_sz) / 2);
        apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(4 * g_dpi, ws.y), ImGui::GetColorU32(color), ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    // Carte avec pochette à gauche ; les actions sont regroupées dans le menu « Actions ».
    void draw_song_cover(const Song & s, bool details = false, bool collapsible = false) {
        ImGui::PushID(s.id.c_str());
        ImVec4 color = status_color(s);
        bool fresh = is_new(s);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, fresh ? tint(P.card, color, 0.13f) : P.card);
        ImGui::PushStyleColor(ImGuiCol_Border, fresh ? alpha(color, 0.65f) : P.line);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 12 * g_dpi));
        ImGui::BeginChild("cover", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        album_stripe(s);  // 1.0.37
        float  side = 84 * g_dpi;
        ImDrawList * dl = ImGui::GetWindowDrawList();
        draw_cover_art(cover_key(s.id, s.artist, s.album), s.title, side);
        ImGui::SameLine(0, 14 * g_dpi);
        ImGui::BeginGroup();
        // titre, statut et pouces
        float        line_x = ImGui::GetCursorPosX(), avail = ImGui::GetContentRegionAvail().x;
        const char * status = status_label(s);
        float        status_w = pill_width(status), thumb_sz = 24 * g_dpi, thumb_w = marks_width(settings.mark_style, thumb_sz);
        float        title_y  = ImGui::GetCursorPosY();
        float        logos_w  = clip_logos_width(s);
        float        new_w    = (fresh ? pill_width(tr("À écouter")) + 6 * g_dpi : 0) + (logos_w > 0 ? logos_w + 8 * g_dpi : 0);  // 1.0.62 : place des logos
        ImGui::PushFont(g_bold);
        ImGui::PushTextWrapPos(line_x + avail - status_w - thumb_w - new_w - 20 * g_dpi);
        ImGui::TextWrapped("%s", s.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (collapsible) {  // un clic sur le titre replie la carte
            ImVec2 a = ImGui::GetItemRectMin(), b = ImVec2(a.x + (avail - status_w - thumb_w - new_w - 20 * g_dpi), ImGui::GetItemRectMax().y);
            if (title_zone(a, b, tr("Cliquer pour replier"))) expanded_cards.erase(s.id);
        }
        float after_y = ImGui::GetCursorPosY();
        if (fresh) {
            ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - new_w - 8 * g_dpi, title_y));
            pill(tr("À écouter"), P.pink);
        }
        ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - logos_w - 16 * g_dpi, title_y));
        draw_clip_logos(s);
        ImGui::SetCursorPos(ImVec2(line_x + avail - status_w - thumb_w - 8 * g_dpi, title_y));
        pill(status, color);
        ImGui::SetCursorPos(ImVec2(line_x + avail - thumb_w, title_y - 3 * g_dpi));
        apply_mark(s, draw_marks(settings.mark_style, s.validated, s.flagged, thumb_sz));
        ImGui::SetCursorPos(ImVec2(line_x, after_y));
        if (!s.instrumental && !s.lyrics_lang.empty()) {  // langue des paroles (déclarée ou détectée)
            int li = language_index_for_code(s.lyrics_lang);
            if (li > 0) {
                pill(trf("Paroles : %s", tr(LANGUAGES[li].label)).c_str(), P.blue);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", s.lyrics_lang_declared ? tr("Langue des paroles : déclarée par la balise de langue du style.") : tr("Langue des paroles : détectée d'après le texte."));
                }
            }
        }
        if (!s.album.empty()) {  // 1.0.37 : badge de l'album (clic = filtre)
            album_badge(s);
            ImGui::SameLine(0, 8 * g_dpi);
        }
        if (!s.artist.empty() || !s.album.empty()) {
            std::string who = s.artist + (!s.artist.empty() && !s.album.empty() ? "  —  " : "") + s.album;
            ImGui::TextColored(P.blue, "%s", who.c_str());
        }
        ImGui::TextColored(P.muted, "%s", song_summary(s, false).c_str());
        if (ImGui::IsItemHovered()) {
            if (s.negative_style.empty()) ImGui::SetTooltip("%s", s.style.c_str());
            else ImGui::SetTooltip("%s\n%s", s.style.c_str(), trf("À éviter : %s (force %.1f)", s.negative_style.c_str(), s.negative_scale).c_str());
        }
        ImGui::Dummy(ImVec2(0, 3 * g_dpi));
        draw_player(s);
        ImGui::EndGroup();
        // menu d'actions, sous la pochette et le texte
        if (soft_button(tr("Actions ▾"), P.accent)) ImGui::OpenPopup("##actions");
        ImGui::SameLine();
        ImGui::TextColored(P.muted, tr("export, vidéo, re-rendu, recomposer, prolonger…"));
        draw_song_menu(s);
        if (details) {
            ImGui::Dummy(ImVec2(0, 3 * g_dpi));
            muted_wrapped(s.style);
            draw_song_stats(s, color);
        }
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ImVec2(5 * g_dpi, ws.y), ImGui::GetColorU32(color), ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    bool passes_filters(const Song & s) const {
        if (s.active()) return true;  // la file reste toujours visible
        if (!filter_text.empty()) {
            std::string hay = lower(s.title + " " + s.style + " " + s.parent_title + " " + s.artist + " " + s.album);
            if (hay.find(lower(filter_text)) == std::string::npos) return false;
        }
        switch (filter_type) {
            case 1: if (s.instrumental) return false; break;
            case 2: if (!s.instrumental) return false; break;
            case 3: if (s.kind != "cover") return false; break;
            case 4: if (s.kind != "rerender" && s.kind != "extend") return false; break;
            case 5: if (s.status == "done") return false; break;
            case 6: if (!s.validated) return false; break;
            case 7: if (s.status != "done" || s.validated) return false; break;
            case 8: if (!s.flagged) return false; break;
            case 9: if (!is_new(s)) return false; break;
        }
        double              d        = s.audio_seconds > 0 ? s.audio_seconds : s.duration;
        static const double bounds[][2] = { { 0, 1e9 }, { 0, 60 }, { 60, 120 }, { 120, 180 }, { 180, 240 }, { 240, 1e9 } };
        if (d < bounds[filter_duration][0] || d >= bounds[filter_duration][1]) return false;
        if (!filter_style.empty()) {
            bool found = false;
            for (auto & t : split_tags(s.style)) found |= lower(t) == filter_style;
            if (!found) return false;
        }
        return true;
    }

    void draw_queue_bar(const std::vector<Song> & songs) {
        int running = 0, waiting = 0, held = 0;
        for (const Song & s : songs) {
            running += s.status == "running";
            waiting += s.waiting();
            held += s.status == "queued" && s.held;
        }
        ImVec4 color = running ? P.accent : waiting ? P.blue : held ? P.amber : P.muted;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.panel, color, 0.07f));
        ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpi, 8 * g_dpi));
        ImGui::BeginChild("queue", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(color, tr("File de génération"));
        ImGui::PopFont();
        auto count = [&](int n, const char * text, const ImVec4 & c) {
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(n ? c : P.muted, "● %d %s", n, text);
        };
        count(running, tr("en cours"), P.accent);
        count(waiting, tr("en attente"), P.blue);
        count(held, tr("en pause"), P.amber);
        if (studio->gate_closed()) {
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(P.orange, tr("● jeu en cours : file suspendue"));
        }
        const char * a = tr("▶ Tout lancer");
        const char * b = tr("⏸ Tout en pause");
        float w = ImGui::CalcTextSize(a).x + ImGui::CalcTextSize(b).x + ImGui::GetStyle().FramePadding.x * 4 +
                  ImGui::GetStyle().ItemSpacing.x;
        {   // à droite de la ligne ; sous les compteurs quand la place manque (langues à textes longs)
            float end_x   = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
            float want_x  = ImGui::GetContentRegionMax().x - w;
            if (want_x >= end_x + 10 * g_dpi) ImGui::SameLine(want_x);
        }
        ImGui::BeginDisabled(held == 0);
        if (soft_button(a, P.green, false)) studio->resume_all();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tr("Relance tous les morceaux en pause."));
        ImGui::SameLine();
        ImGui::BeginDisabled(running + waiting == 0);
        if (soft_button(b, P.amber, false)) studio->hold_all();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(tr("Met toute la file en pause et libère la carte graphique.\nLa génération en cours reprendra depuis le début."));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    void draw_filters(const std::vector<Song> & songs) {
        input_line("##search", tr("Rechercher un titre, un groupe, un album ou un style…"), filter_text);
        float               w       = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
        static const char * types[] = { N_("Tous les types"), N_("Chansons"), N_("Instrumentaux"), N_("Reprises"), N_("Re-rendus et suites"),
                                        N_("Non terminés"), N_("Validés"), N_("Non validés"), N_("À problème"), N_("Nouveaux (à écouter)") };
        ImGui::SetNextItemWidth(w);
        ImGui::Combo("##ftype", &filter_type, tr_list(types, (int) IM_ARRAYSIZE(types)).data(), IM_ARRAYSIZE(types));
        static const char * durations[] = { N_("Toutes durées"), N_("Moins d'1 min"), N_("1 à 2 min"), N_("2 à 3 min"), N_("3 à 4 min"), N_("4 min et plus") };
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        ImGui::Combo("##fduration", &filter_duration, tr_list(durations, (int) IM_ARRAYSIZE(durations)).data(), IM_ARRAYSIZE(durations));
        std::map<std::string, int> counts;
        for (const Song & s : songs) {
            for (auto & t : split_tags(s.style)) counts[lower(t)]++;
        }
        std::vector<std::pair<std::string, int>> tags(counts.begin(), counts.end());
        std::sort(tags.begin(), tags.end(), [](auto & a, auto & b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        std::string preview = filter_style.empty() ? tr("Tous les styles") : filter_style;
        if (ImGui::BeginCombo("##fstyle", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(tr("Tous les styles"), filter_style.empty())) filter_style.clear();
            for (auto & [tag, n] : tags) {
                std::string item = tag + "  (" + std::to_string(n) + ")";
                if (ImGui::Selectable(item.c_str(), filter_style == tag)) filter_style = tag;
            }
            ImGui::EndCombo();
        }
    }

    void draw_library(const std::vector<Song> & songs) {
        std::vector<const Song *> shown;
        for (const Song & s : songs) {
            if (passes_filters(s)) shown.push_back(&s);
        }
        // En-tête : titre, compteur, volume et dossier
        ImGui::PushFont(g_header);
        ImGui::TextUnformatted(tr("Bibliothèque"));
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("%zu sur %zu morceaux"), shown.size(), songs.size());
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const float count_x = ImGui::GetCursorPosX();
        char        count[64];
        if (shown.size() == songs.size()) snprintf(count, sizeof(count), songs.size() > 1 ? tr("%zu morceaux") : tr("%zu morceau"), songs.size());
        else snprintf(count, sizeof(count), tr("%zu sur %zu morceaux"), shown.size(), songs.size());
        float folder_w = ImGui::CalcTextSize(tr("Ouvrir le dossier")).x + ImGui::GetStyle().FramePadding.x * 2;
        float albums_w = ImGui::CalcTextSize(tr("Albums…")).x + ImGui::GetStyle().FramePadding.x * 2;
        float vol_w    = 130 * g_dpi;
        float label_w  = ImGui::CalcTextSize(tr("Volume")).x;
        // 1.0.46 : « Lire les nouveaux » = lecture continue des morceaux terminés pas encore écoutés (ordre de la liste)
        int unheard = 0;
        for (const Song * s : shown) unheard += s->status == "done" && !s->heard;
        std::string new_label = new_mode ? std::string(tr("Arrêter les nouveaux")) : trf("Lire les nouveaux (%d)", unheard);
        float       new_w     = ImGui::CalcTextSize(new_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 +
                          (new_mode ? ImGui::CalcTextSize(tr("Suivant")).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x : 0);
        // 1.0.78 : le compteur n'est écrit que s'il tient avant les boutons (sinon il chevauchait « Lire les nouveaux ») ; il reste
        // affiché au survol du titre « Bibliothèque »
        const float buttons_x = ImGui::GetContentRegionMax().x - new_w - folder_w - albums_w - vol_w - label_w - ImGui::GetStyle().ItemSpacing.x * 5;
        if (count_x + ImGui::CalcTextSize(count).x + ImGui::GetStyle().ItemSpacing.x <= buttons_x) {
            ImGui::TextColored(P.muted, "%s", count);
            ImGui::SameLine(buttons_x);
        } else {
            ImGui::SetCursorPosX(std::max(count_x, buttons_x));
        }
        ImGui::BeginDisabled(!new_mode && unheard == 0);
        if (soft_button(new_label.c_str(), new_mode ? P.danger : P.green, false)) {
            if (new_mode) {
                new_mode = false;
                player.close();
            } else {
                start_new_playback(shown);
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Écoute à la suite tous les morceaux terminés que vous n'avez pas encore écoutés, dans l'ordre de la liste."));
        if (new_mode) {
            ImGui::SameLine();
            if (soft_button(tr("Suivant"), P.green, false)) next_new_song();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, tr("Volume"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(vol_w);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, P.green);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, P.green);
        if (ImGui::SliderInt("##volume", &settings.volume, 0, 100, "%d %%")) player.set_volume(settings.volume);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
        ImGui::SameLine();
        if (ImGui::Button(tr("Albums…"))) open_albums = true;
        ImGui::SameLine();
        if (ImGui::Button(tr("Ouvrir le dossier"))) {
            ShellExecuteW(nullptr, L"open", studio->paths().library.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        sort_shown(shown);
        play_queue.clear();
        for (const Song * s : shown) {
            if (s->status == "done") play_queue.push_back(s->id);
        }
        draw_queue_bar(songs);
        draw_jobs();
        draw_filters(songs);
        draw_sort_bar();
        ImGui::Dummy(ImVec2(0, 2 * g_dpi));
        if (songs.empty()) {
            ImGui::Dummy(ImVec2(0, 40 * g_dpi));
            muted_wrapped(tr("Aucun morceau pour l'instant. Remplissez le formulaire pour lancer le premier."));
        } else if (shown.empty()) {
            ImGui::Dummy(ImVec2(0, 20 * g_dpi));
            muted_wrapped(tr("Aucun morceau ne correspond aux filtres."));
        }
        // Position dans la file : ordre de passage (le plus ancien d'abord).
        std::vector<const Song *> waiting;
        for (const Song & s : songs) {
            if (s.waiting()) waiting.push_back(&s);
        }
        std::sort(waiting.begin(), waiting.end(), [](const Song * a, const Song * b) { return a->created < b->created; });
        // Seule la liste défile : en-tête, file et filtres restent visibles.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("songs", ImVec2(0, 0), ImGuiChildFlags_None);
        // 1.0.39 (demande de Herve) : cartes de la bibliothèque à angles droits (fond, bordure, liserés, pochette) ;
        // le reste de l'interface garde ses arrondis.
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
        for (const Song * s : shown) {
            int pos = 0;
            for (size_t i = 0; i < waiting.size(); i++) {
                if (waiting[i] == s) pos = (int) i + 1;
            }
            bool finished = s->status == "done";
            if (finished && settings.card_mode == 1 && !expanded_cards.count(s->id)) draw_song_compact(*s);
            else if (finished && settings.card_mode == 2) draw_song_cover(*s);
            else if (finished && settings.card_mode == 3) {
                if (expanded_cards.count(s->id)) draw_song_cover(*s, true, true);
                else draw_song_cover_compact(*s);
            }
            else draw_song(*s, pos, finished && settings.card_mode == 1);
            ImGui::Dummy(ImVec2(0, 1 * g_dpi));
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ------------------------------------------------------------------ Jeux, lecture continue, tâches

    // Suspend la file tant qu'un jeu tourne ; la reprend 10 s après sa fermeture (évite les à-coups).
    void update_games() {
        double now = now_seconds();
        if (now - last_game_check < 2.0) return;
        last_game_check = now;
        std::string name;
        bool        found = settings.game_pause && detect_game(settings.game_list, name);
        if (found) {
            game_clear_since = 0;
            game_name        = name;
            if (!game_active) {
                game_active = true;
                studio->set_gate(true);
                notify(trf("Jeu détecté (%s) : la file de génération est suspendue.", name.c_str()));
            }
        } else if (game_active) {
            if (!settings.game_pause) {
                game_active = false;
                studio->set_gate(false);
            } else if (game_clear_since == 0) {
                game_clear_since = now;
            } else if (now - game_clear_since >= 10.0) {
                game_active = false;
                studio->set_gate(false);
                notify(tr("Jeu terminé : la file de génération reprend."));
            }
        }
    }

    // Fichier lu par le lecteur : le WAV (sortie brute du moteur) ; à défaut le MP3 (morceaux livrés sans WAV, 1.0.30).
    fs::path playable_file(const std::string & id) const {
        fs::path dir = song_folder(id);
        return !cached_exists(dir / "audio.wav") && cached_exists(dir / "audio.mp3") ? dir / "audio.mp3" : dir / "audio.wav";
    }

    void play_song(const std::string & id) {
        fs::path wav = playable_file(id);
        if (player.open(id, wav.wstring())) {
            player.set_volume(settings.volume);
            player.play();
            studio->set_heard(id, true);
        }
    }

    // Enchaîne le morceau suivant de la liste affichée quand celui en cours se termine.
    // 1.0.46 : lecture des nouveaux (morceaux terminés pas encore écoutés), dans l'ordre affiché.
    bool                     new_mode = false;
    std::vector<std::string> new_queue;
    void start_new_playback(const std::vector<const Song *> & shown) {
        new_queue.clear();
        for (const Song * s : shown)
            if (s->status == "done" && !s->heard) new_queue.push_back(s->id);
        new_mode = !new_queue.empty();
        if (new_mode) play_song(new_queue.front());
    }
    void next_new_song() {
        auto it = std::find(new_queue.begin(), new_queue.end(), player.id());
        if (it != new_queue.end() && ++it != new_queue.end()) play_song(*it);
        else {
            new_mode = false;  // fin de la liste
            player.close();
            notify(tr("Tous les nouveaux morceaux ont été écoutés."));
        }
    }

    void update_autoplay() {
        bool playing = player.playing();
        // Un autre morceau lancé à la main : la lecture des nouveaux s'arrête.
        if (new_mode && !player.id().empty() && std::find(new_queue.begin(), new_queue.end(), player.id()) == new_queue.end()) new_mode = false;
        if (playing) {
            last_pos = player.position();
        } else if (was_playing && new_mode && !player.id().empty() && player.length() > 0 && player.stopped() &&
                   last_pos >= player.length() - 2500) {
            next_new_song();
        } else if (was_playing && settings.autoplay && !player.id().empty() && player.length() > 0 && player.stopped() &&
                   last_pos >= player.length() - 2500) {
            auto it = std::find(play_queue.begin(), play_queue.end(), player.id());
            if (it != play_queue.end() && ++it != play_queue.end()) play_song(*it);
        }
        was_playing = playing;
    }

    // 1.0.83 : contrôle qualité (un album, ou toute la bibliothèque si key est vide)
    std::shared_ptr<Job>                          quality_job;
    std::shared_ptr<std::vector<QualityFinding>> quality_found;
    void start_quality_check(const std::vector<Song> & all, const std::string & key, const std::string & label) {
        if (quality_job && quality_job->state == 0) {
            notify(tr("Un contrôle qualité est déjà en cours."));
            return;
        }
        std::vector<Song> songs;
        for (const Song & s : all) {
            if (s.status != "done") continue;
            if (!key.empty() && (s.album.empty() || album_key(s.artist, s.album) != key)) continue;
            songs.push_back(s.long_join > 0 ? with_full_lyrics(s, all) : s);  // morceau long : paroles de toutes ses parties
        }
        if (songs.empty()) {
            notify(tr("Aucun morceau terminé à contrôler."));
            return;
        }
        const fs::path report = (key.empty() ? studio->paths().library : album_folder(songs[0].artist, songs[0].album)) / L"controle_qualite.txt";
        quality_found = std::make_shared<std::vector<QualityFinding>>();
        quality_job   = new_job(tr("Contrôle qualité — ") + label);
        start_quality(quality_job, songs, studio->paths().whisper, studio->paths().models_dir / "voix" / whisper_file().name, report, quality_found);
        notify(trf("Contrôle qualité lancé (%d morceau(x)) : les morceaux à refaire recevront un pouce rouge.", (int) songs.size()));
    }
    void update_quality() {
        if (!quality_job || quality_job->state == 0 || !quality_found) return;
        std::vector<QualityFinding> found;
        {
            std::lock_guard<std::mutex> lock(quality_job->mutex);
            found = *quality_found;
        }
        quality_found.reset();
        const std::vector<Song> all = studio->snapshot();
        for (const QualityFinding & f : found)
            for (const Song & s : all)
                if (s.id == f.id && !s.validated && !s.flagged && !quality_no_flag) studio->set_flagged(s.id, true);
    }

    void update_jobs() {
        update_quality();
        for (auto & j : jobs) {
            if (j->state != 0 && !j->announced) {
                j->announced = true;
                notify(j->name + (j->state == 1 ? tr(" : terminé.") : tr(" : échec.")));
            }
        }
    }

    // Résultat du test de santé : le pilote testé devient la référence.
    void update_health() {
        HealthResult h = studio->health();
        if ((h.state == 2 || h.state == 3) && h.when != last_health_seen) {
            last_health_seen = h.when;
            if (!driver_version.empty()) {
                settings.gpu_driver = driver_version;
                save_settings(settings, settings_path);
            }
            driver_changed = false;
            notify(tr("Test de santé : ") + h.message);
        }
    }

    // ------------------------------------------------------------------ Pochettes, exports et vidéos

    // Dossier des images d'albums : donnees\pochettes (1.0.30 ; à la racine du programme avant).
    fs::path pochettes_dir() const { return studio->paths().data / "pochettes"; }

    // Existence d'un fichier, mémorisée 3 secondes (1.0.30) : les cartes de la bibliothèque la demandent à chaque image
    // (pochettes, codes du re-rendu) ; sans ce cache, plusieurs accès disque par carte et par image.
    mutable std::map<std::wstring, std::pair<bool, double>> exists_cache;
    bool cached_exists(const fs::path & p) const {
        double now = ImGui::GetTime();
        auto   it  = exists_cache.find(p.native());
        if (it != exists_cache.end() && now - it->second.second < 3.0) return it->second.first;
        std::error_code ec;
        bool            e = fs::exists(p, ec);
        if (exists_cache.size() > 4096) exists_cache.clear();
        exists_cache[p.native()] = { e, now };
        return e;
    }

    // 1.0.51 : nombre de vidéos (.mp4) d'un dossier, mémorisé 3 secondes comme cached_exists (voyants de la fenêtre Albums).
    mutable std::map<std::wstring, std::pair<int, double>> video_cache;
    int video_count(const fs::path & dir) const {
        double now = ImGui::GetTime();
        auto   it  = video_cache.find(dir.native());
        if (it != video_cache.end() && now - it->second.second < 3.0) return it->second.first;
        int             n = 0;
        std::error_code ec;
        for (fs::directory_iterator d(dir, ec), e; !ec && d != e; d.increment(ec)) n += _wcsicmp(d->path().extension().c_str(), L".mp4") == 0;
        if (video_cache.size() > 1024) video_cache.clear();
        video_cache[dir.native()] = { n, now };
        return n;
    }
    std::string album_open;          // 1.0.51 : album déplié dans la fenêtre Albums (un seul à la fois)
    int         album_tab     = 0;   // onglet de la fiche dépliée : 0 = Pochettes, 1 = Vidéos
    bool        album_scroll  = false;  // essai : faire défiler la liste jusqu'à l'album déplié
    int         album_tab_request = -1; // essai : onglet à sélectionner ; album_menu_test : ouvrir le menu des modèles
    bool        album_menu_test   = false;
    float       prompt_wrap_w = 0;   // largeur de coupure de la description de la pochette

    std::string cover_for(const std::string & key) const {
        auto it = settings.covers.find(key);
        if (it == settings.covers.end()) return std::string();
        fs::path p = widen(it->second);
        if (cached_exists(p)) return it->second;
        // Album renommé, morceau déplacé ou dossier du programme déplacé : même image dans le dossier actuel (1.0.43).
        std::wstring stem;
        fs::path     dir  = cover_dir(key, stem);  // à part : stem n'est rempli qu'après cet appel
        fs::path     here = dir / (stem + p.extension().wstring());
        if (cached_exists(here)) return narrow(here.wstring());
        here = pochettes_dir() / p.filename();
        return cached_exists(here) ? narrow(here.wstring()) : std::string();
    }

    fs::path ask_image(const wchar_t * title) {
        wchar_t       file[MAX_PATH] = {};
        OPENFILENAMEW ofn{ sizeof(ofn) };
        ofn.hwndOwner   = hwnd;
        std::wstring filter = file_filter(wtr("Images"), L"*.jpg;*.jpeg;*.png;*.webp;*.bmp");
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile   = file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrTitle  = title;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
    }

    fs::path ask_save(const std::wstring & name, const wchar_t * label, const wchar_t * ext) {
        wchar_t file[MAX_PATH] = {};
        wcsncpy_s(file, name.c_str(), _TRUNCATE);
        std::wstring filter = std::wstring(label) + L" (*." + ext + L")";
        filter.push_back(L'\0');
        filter += std::wstring(L"*.") + ext;
        filter.push_back(L'\0');
        filter.push_back(L'\0');
        OPENFILENAMEW ofn{ sizeof(ofn) };
        ofn.hwndOwner   = hwnd;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile   = file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrDefExt = ext;
        ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        return GetSaveFileNameW(&ofn) ? fs::path(file) : fs::path();
    }

    // Emplacements d'images d'un album : la pochette (étiquettes des fichiers) et une image par réseau.
    static std::string slot_youtube(const std::string & key) { return key + "#youtube"; }
    static std::string slot_tiktok(const std::string & key) { return key + "#tiktok"; }

    // Choisit une image pour un emplacement ; la copie est conservée dans le dossier « pochettes ».
    bool set_image(const std::string & slot, const wchar_t * title) {
        fs::path img = ask_image(title);
        return !img.empty() && install_cover(slot, img);  // 1.0.43 : même rangement que les pochettes de l'IA
    }

    std::shared_ptr<Job> new_job(const std::string & name) {
        auto j  = std::make_shared<Job>();
        j->name = name;
        jobs.push_back(j);
        return j;
    }

    // Ouvre la fenêtre de création de vidéos pour un morceau ou un album (key = clé d'album, ou vide).
    // clip = true (bouton « Clip » d'un morceau) : le clip animé est déjà coché, format YouTube seul pour un essai rapide.
    void open_video_dialog(const std::vector<Song> & songs, const std::string & title, const std::string & key, bool clip = false) {
        if (songs.empty()) return;
        video_redo = false;
        video_songs = songs;
        compute_video_recs(songs);
        video_title = display_title(title);
        video_key   = key;
        open_video_dialog_images(songs, key, clip);
    }
    void compute_video_recs(const std::vector<Song> & songs) {
        {  // recommandation du nombre d'images de chaque morceau, d'après ses paroles (morceau long : les deux parties)
            video_lines.clear();
            video_rec.clear();
            const std::vector<Song> all = studio->snapshot();
            for (const Song & s0 : songs) {
                const Song  full = with_full_lyrics(s0, all);
                drop_stale_lyrics_file(s0, full, song_folder(s0.id));  // paroles.txt ancien (partie 2 seule) : relu depuis les deux parties
                std::string text = full.lyrics;
                std::ifstream pf(song_folder(s0.id) / L"paroles.txt", std::ios::binary);
                if (pf) text.assign((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
                const size_t n = full.instrumental ? 0 : clip_lyrics_lines(text).size();
                video_lines[s0.id] = (int) n;
                video_rec[s0.id]   = clip_recommend_images(n, s0.audio_seconds);
            }
        }
    }
    void open_video_dialog_images(const std::vector<Song> & songs, const std::string & key, bool clip) {
        auto pick   = [&](const std::string & slot) {  // image du réseau, à défaut la pochette de l'album
            std::string c = key.empty() ? std::string() : cover_for(slot);
            if (c.empty() && !key.empty()) c = cover_for(key);
            return c.empty() ? fs::path() : fs::path(widen(c));
        };
        img_yt     = pick(slot_youtube(key));
        img_tt     = pick(slot_tiktok(key));
        img_ig     = pick(key);  // 1:1 = la pochette carrée de l'album
        video_yt   = video_tt = video_ig = true;
        if (clip) {
            video_clip = true;
            video_tt = video_ig = false;
        }
        // 1.0.43 : Audios\<Groupe>\<Album>\videos (sans album : Audios\<Groupe>\videos) ; plus de dossier au choix
        video_dest = album_folder(songs[0].artist, key.empty() ? std::string() : songs[0].album) / L"videos";
        open_video = true;
    }

    // Une vidéo par réseau coché : <titre>_YouTube.mp4 (16:9), <titre>_TikTok.mp4 (9:16), <titre>_Instagram.mp4 (1:1).
    // 1.0.82 : morceaux sans Short (terminés, ni « partie 1 » ni « partie 2 » d'un morceau long) : de l'album « key », ou de
    // toute la bibliothèque si key est vide.
    std::vector<Song> songs_missing_short(const std::vector<Song> & all, const std::string & key) const {
        std::vector<Song> out;
        for (const Song & s : all) {
            if (s.status != "done" || !s.long_rest.empty()) continue;
            if (!key.empty() && (s.album.empty() || album_key(s.artist, s.album) != key)) continue;
            const fs::path f = album_folder(s.artist, s.album) / L"videos" / L"Shorts" / (widen(safe_filename(display_title(s.title))) + L"_Short.mp4");
            if (!cached_exists(f)) out.push_back(s);
        }
        return out;
    }
    // Une seule file de création (comme les clips) : Short seul, avec les réglages du clip (rendu, moteur d'images, durée) ;
    // les images d'un clip déjà fait sont reprises, sinon elles sont créées d'abord.
    void start_missing_shorts(const std::vector<Song> & songs, const std::string & label) {
        if (songs.empty()) return;
        const bool keep_skip = skip_existing_clips;
        video_songs     = songs;
        compute_video_recs(songs);  // nombre d'images conseillé par morceau, comme la fenêtre « Créer des vidéos »
        if (!clip_style_loaded) {   // type de rendu mémorisé dans les réglages
            clip_style_loaded = true;
            for (int i = 0; i < (int) clip_styles().size(); i++)
                if (settings.clip_style_id == clip_styles()[i].id) clip_style = i;
        }
        video_dest      = album_folder(songs[0].artist, songs[0].album) / L"videos";
        video_title     = label;
        video_key.clear();
        img_yt = img_tt = img_ig = fs::path();
        video_clip      = true;
        video_yt = video_tt = video_ig = false;
        video_short     = true;
        video_redo      = false;
        video_per_album = true;
        skip_existing_clips = true;
        create_videos();
        skip_existing_clips = keep_skip;
        video_per_album     = false;
    }
    std::vector<Song> shorts_pending;  // série en attente de confirmation
    std::string       shorts_label;
    bool              shorts_open = false;
    bool              shorts_test = false;
    std::string       quality_test;  // essai STUDIO_TEST_ALBUMS=qc:<clé>
    bool              quality_no_flag = false;  // essai STUDIO_TEST_ALBUMS=shorts
    void draw_shorts_confirm() {
        if (shorts_open) {
            ImGui::OpenPopup(tr("Shorts en série"));
            shorts_open = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(620 * g_dpi, 0));
        if (!ImGui::BeginPopupModal(tr("Shorts en série"), nullptr, ImGuiWindowFlags_NoResize)) return;
        ImGui::PushFont(g_bold);
        ImGui::TextUnformatted(shorts_label.c_str());
        ImGui::PopFont();
        colored_wrapped(P.text, trf("%d morceau(x) sans Short : un Short de %s s par morceau, centré sur le refrain, rangé dans le dossier « videos\\Shorts » de son album.",
                                    (int) shorts_pending.size(), settings.clip_short_seconds.c_str()));
        muted_wrapped(tr("Les images du clip d'un morceau sont reprises. Si un morceau n'a pas encore de clip, ses images sont créées d'abord (quelques minutes par morceau). Les Shorts déjà faits ne sont pas refaits. Type de rendu, moteur d'images et durée : ceux de la fenêtre « Créer des vidéos »."));
        int shown = 0;
        for (const Song & s : shorts_pending)
            if (shown++ < 6) ImGui::TextColored(P.muted, "  %s — %s", s.artist.c_str(), display_title(s.title).c_str());
        if (shown > 6) ImGui::TextColored(P.muted, "%s", trf("… et %d autre(s) morceau(x).", shown - 6).c_str());
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (modal_buttons(tr("Créer les Shorts"), !shorts_pending.empty(), &P.teal)) {
            start_missing_shorts(shorts_pending, shorts_label);
            shorts_pending.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    void ask_missing_shorts(const std::vector<Song> & all, const std::string & key, const std::string & label) {
        shorts_pending = songs_missing_short(all, key);
        shorts_label   = label;
        if (shorts_pending.empty()) notify(tr("Tous les morceaux ont déjà leur Short."));
        else shorts_open = true;
    }

    void create_videos() {
        std::error_code ec;
        fs::create_directories(video_dest, ec);
        struct Net { bool on; const char * name; const wchar_t * suffix; fs::path img; int w, h; };
        std::vector<VideoTask> tasks;
        std::vector<ClipTask>  clips;
        const bool             clip = video_clip;
        // 1.0.53 : scénario d'après les paroles et les fiches, puis images par scène, au repos seulement (carte graphique libre)
        SceneEngine scene_engine;
        bool        with_scenes = false;
        if (clip) {
            if (studio->local_active() || ai_local_busy()) {
                notify(tr("Les images du clip se font au repos : le clip sera fait avec la pochette tant qu'une génération est en cours."));
            } else {
                std::error_code ec2;
                scene_engine.llm        = find_local_llm(app_dir());  // cherché ici : « local_llm » n'est rempli que si l'onglet « Composer avec l'IA » a été ouvert
                scene_engine.llm_model  = ai_models_dir() / widen(settings.ai_local_model);
                auto size               = fs::file_size(scene_engine.llm_model, ec2);
                scene_engine.llm_layers = scene_engine.llm.vulkan ? ai_gpu_layers(ec2 ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
                scene_engine.sd_cli      = find_sd_cli(app_dir());
                scene_engine.image_root  = image_models_dir();
                // Moteur d'images choisi dans la fenêtre (FLUX.2 klein par défaut : ≈ 14 s par image, MOSS ≈ 3 min) ; s'il n'est pas
                // installé, FLUX.2 klein à défaut.
                {
                    const LocalImageModel & want = local_image_model(settings.clip_image_model.empty() ? "klein" : settings.clip_image_model);
                    scene_engine.image_model = &(image_model_ready(image_models_dir(), want) ? want : local_image_model("klein"));
                }
                scene_engine.offload     = image_offload(*scene_engine.image_model);
                const ClipStyle & cs     = clip_styles()[std::min<size_t>((size_t) clip_style, clip_styles().size() - 1)];
                scene_engine.scene_count = std::clamp(atoi(settings.clip_image_count.c_str()), 0, 60);
                for (const Song & sg : video_songs) scene_engine.counts[sg.id] = clip_images_for(sg);  // recommandation ou réglage, par morceau
                scene_engine.style_id    = cs.id;
                scene_engine.style_prompt = cs.prompt;
                with_scenes              = true;
            }
            if (video_redo && with_scenes) {  // « Refaire » : scénario et images de ce rendu repartent de zéro
                for (const Song & sg : video_songs) {
                    std::error_code ec6;
                    fs::remove_all(clip_scenes_dir(scene_engine, sg), ec6);
                }
            }
        }
        video_redo = false;
        auto clip_task = [&](const VideoTask & t) {
            ClipTask c;
            static_cast<VideoTask &>(c) = t;
            c.text_start    = trim(settings.clip_text_start);
            c.text_end      = trim(settings.clip_text_end);
            c.whisper_exe   = studio->paths().whisper;
            c.whisper_model = studio->paths().models_dir / "voix" / whisper_file().name;
            if (with_scenes) {
                c.make_scenes = [scene_engine](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out,
                                               std::string & note) { return make_clip_scenes(scene_engine, j, sg, lines, seconds, out, note); };
                c.make_scenario = [scene_engine](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out,
                                                 std::string & note) { return make_clip_scenes(scene_engine, j, sg, lines, seconds, out, note, true); };
            }
            return c;
        };
        for (const Net & n : { Net{ video_yt, "YouTube", L"YouTube", img_yt, 1920, 1080 },
                               Net{ video_tt, "TikTok", L"TikTok", img_tt, 1080, 1920 },
                               Net{ video_ig, "Instagram", L"Instagram", img_ig, 1080, 1080 } }) {
            if (!n.on || (n.img.empty() && !clip)) continue;  // un clip a ses propres images : la pochette ne sert qu'en secours
            for (const Song & s : video_songs) {
                if (clip && !s.long_rest.empty()) continue;  // partie 1 d'un morceau long : seul le morceau assemblé a son clip
                VideoTask t;
                t.song   = s;
                if (clip) {
                    // morceau long assemblé (partie 1 + partie 2) : le clip doit suivre les paroles des deux parties
                    const Song full = with_full_lyrics(s, studio->snapshot());
                    drop_stale_lyrics_file(s, full, song_folder(s.id));
                    t.song = full;
                }
                t.image  = n.img;
                t.width  = n.w;
                t.height = n.h;
                // 1.0.52 : un clip animé se range dans le même dossier de format que la vidéo fixe, suffixe « _Clip »
                t.output = video_dest / (n.w > n.h ? L"YouTube" : n.w == n.h ? L"IG" : L"TikTok") /
                           (widen(safe_filename(display_title(s.title))) + L"_" + n.suffix + (clip ? L"_Clip.mp4" : L".mp4"));
                if (clip && skip_existing_clips) {
                    std::error_code ec5;
                    if (fs::exists(t.output, ec5)) continue;
                }
                if (clip) {
                    clips.push_back(clip_task(t));
                } else {
                    tasks.push_back(std::move(t));
                }
            }
        }
        // 1.0.74 : Shorts après les clips : les images du clip sont alors déjà faites et sont reprises telles quelles
        if (clip && video_short)
            for (const Song & s : video_songs) {
                if (!s.long_rest.empty()) continue;
                VideoTask t;
                t.song   = with_full_lyrics(s, studio->snapshot());
                t.image  = img_tt.empty() ? img_yt : img_tt;
                t.width  = 1080;
                t.height = 1920;
                fs::path dest = video_dest;
                if (video_per_album) {  // 1.0.82 : série sur plusieurs albums
                    dest = album_folder(s.artist, s.album) / L"videos";
                    const std::string key = s.album.empty() ? std::string() : album_key(s.artist, s.album);
                    std::string       c   = key.empty() ? std::string() : cover_for(slot_tiktok(key));
                    if (c.empty() && !key.empty()) c = cover_for(key);
                    t.image = c.empty() ? fs::path() : fs::path(widen(c));
                }
                t.output = dest / L"Shorts" / (widen(safe_filename(display_title(s.title))) + L"_Short.mp4");
                std::error_code ec7;
                if (skip_existing_clips && fs::exists(t.output, ec7)) continue;
                ClipTask c      = clip_task(t);
                c.short_seconds = std::clamp(atoi(settings.clip_short_seconds.c_str()), 15, 90);
                clips.push_back(std::move(c));
            }
        if (!clips.empty()) {
            auto job = new_job(tr("Clips — ") + video_title + " (" + std::to_string(clips.size()) + ")");
            start_clips(job, studio->paths(), std::move(clips));
            notify(trf("%d titre(s) : création des clips en cours.", (int) video_songs.size()));
            return;
        }
        if (tasks.empty()) {
            if (clip) notify(tr("Aucun clip à créer : la partie 1 d'un morceau long n'a pas de clip (utilisez le morceau assemblé)."));
            return;
        }
        auto job = new_job(tr("Vidéos — ") + video_title + " (" + std::to_string(tasks.size()) + ")");
        start_videos(job, studio->paths(), std::move(tasks));
        notify(trf("%d titre(s) : création des vidéos en cours.", job ? (int) video_songs.size() : 0));
    }

    // Bloc d'un réseau dans la fenêtre vidéo : case, aperçu de l'image, choix de l'image.
    void draw_video_network(const char * label, const char * format, bool & enabled, fs::path & img, const std::string & slot,
                            float max_w, float max_h) {
        if (img.empty() && !slot.empty()) {  // 1.0.78 : image créée entre-temps (bouton « Générer »)
            std::string c = cover_for(slot);
            if (!c.empty()) img = fs::path(widen(c));
        }
        ImGui::BeginGroup();
        ImGui::Checkbox(label, &enabled);
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", format);
        ImTextureID tex = 0;
        ImVec2      tsize;
        ImVec2      p0 = ImGui::GetCursorScreenPos();
        float       box_w = max_w, box_h = max_h;
        if (!img.empty() && thumb_texture(img, tex, tsize, 512)) {
            float k = std::min(box_w / tsize.x, box_h / tsize.y);
            ImGui::Dummy(ImVec2(box_w, box_h));
            ImVec2 sz(tsize.x * k, tsize.y * k);
            ImVec2 a = p0 + ImVec2((box_w - sz.x) / 2, (box_h - sz.y) / 2);
            ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(box_w, box_h), ImGui::GetColorU32(P.bg), 6 * g_dpi);
            ImGui::GetWindowDrawList()->AddImage(tex, a, a + sz);
        } else {
            ImGui::Dummy(ImVec2(box_w, box_h));
            ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(box_w, box_h), ImGui::GetColorU32(P.chip), 6 * g_dpi);
            ImGui::GetWindowDrawList()->AddText(p0 + ImVec2(12 * g_dpi, box_h / 2 - 8 * g_dpi), ImGui::GetColorU32(P.muted),
                                                tr("Aucune image"));
        }
        if (soft_button(img.empty() ? tr("Choisir l'image…") : tr("Changer l'image…"), P.violet)) {
            if (!slot.empty()) {
                if (set_image(slot, wtr("Image de la vidéo").c_str())) img = fs::path(widen(settings.covers[slot]));  // mémorisée pour l'album
            } else {
                fs::path chosen = ask_image(wtr("Image de la vidéo").c_str());
                if (!chosen.empty()) img = chosen;
            }
        }
        if (img.empty() && !slot.empty() && !video_key.empty()) {  // 1.0.78 : créer l'image manquante avec le modèle d'images
            ImGui::SameLine();
            ImGui::BeginDisabled(cover_job != nullptr);
            if (soft_button(tr("Générer"), P.pink)) {
                CoverTask t;
                t.slot   = slot;
                t.prompt = album_image_prompt(video_key, video_title, video_songs.empty() ? std::string() : video_songs[0].artist);
                t.label  = video_title + " · " + label;
                cover_size(t.slot, t.w, t.h);
                start_covers({ t });
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Crée cette image avec le modèle d'images des Réglages, d'après la description de l'album."));
        }
        ImGui::EndGroup();
    }

    void draw_video_modal() {
        if (open_video) {
            ImGui::OpenPopup(tr("Créer des vidéos"));
            open_video = false;
        }
        // 1.0.76 : aussi large que la fenêtre du Studio (marge de 24 px de chaque côté) ; deux colonnes à partir de 1100 px :
        // formats et images à gauche, réglages du clip à droite
        const ImGuiViewport * vp    = ImGui::GetMainViewport();
        const float           win_w = std::max(760 * g_dpi, vp->WorkSize.x - 48 * g_dpi);
        const bool            two   = win_w >= 1100 * g_dpi;
        ImGui::SetNextWindowSize(ImVec2(win_w, 0));
        ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));  // suit la taille de la fenêtre du Studio
        if (vp->WorkSize.y > 300 * g_dpi) ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, vp->WorkSize.y - 48 * g_dpi));
        if (!ImGui::BeginPopupModal(tr("Créer des vidéos"), nullptr, ImGuiWindowFlags_NoResize)) return;
        double total = 0;
        for (const Song & s : video_songs) total += s.audio_seconds;
        ImGui::PushFont(g_bold);
        ImGui::TextUnformatted(video_title.c_str());
        ImGui::PopFont();
        muted_wrapped(trf("%d morceau(x) · %s", (int) video_songs.size(), fmt_time(total).c_str()) +
                      (video_songs.size() > 1 ? tr(" · une vidéo individuelle par titre et par réseau") : "") +
                      std::string(video_clip ? "." : tr(". Chaque vidéo est une image fixe avec le son en qualité maximale (AAC 320 kbit/s).")));
        auto files_block = [&] {  // dossier de destination et noms des fichiers créés
            ImGui::TextColored(P.muted, tr("Dossier de destination"));
            ImGui::SameLine();
            ImGui::TextWrapped("%s", narrow(video_dest.wstring()).c_str());  // 1.0.43 : dossier de l'album, fixe
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            std::string base = safe_filename(display_title(video_songs.empty() ? video_title : video_songs[0].title));
            for (const Song & sg : video_songs)  // exemple : un morceau qui a vraiment un clip (pas une « partie 1 »)
                if (sg.long_rest.empty()) {
                    base = safe_filename(display_title(sg.title));
                    break;
                }
            ImGui::TextColored(P.muted, tr("Fichiers créés :"));
            // 1.0.57 : avec le clip, les fichiers finissent par « _Clip » et aucune image n'est exigée
            const bool  clip_names = video_clip;
            const char * clip_end  = clip_names ? "_Clip" : "";
            if (video_yt) ImGui::Text("  YouTube\\%s_YouTube%s.mp4%s%s", base.c_str(), clip_end, video_songs.size() > 1 ? " …" : "", !clip_names && img_yt.empty() ? tr("   (image manquante)") : "");
            if (video_tt) ImGui::Text("  TikTok\\%s_TikTok%s.mp4%s%s", base.c_str(), clip_end, video_songs.size() > 1 ? " …" : "", !clip_names && img_tt.empty() ? tr("   (image manquante)") : "");
            if (video_ig) ImGui::Text("  IG\\%s_Instagram%s.mp4%s%s", base.c_str(), clip_end, video_songs.size() > 1 ? " …" : "", !clip_names && img_ig.empty() ? tr("   (image manquante)") : "");
            if (video_clip && video_short) ImGui::Text("  Shorts\\%s_Short.mp4%s", base.c_str(), video_songs.size() > 1 ? " …" : "");
        };
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (two) {
            ImGui::BeginTable("##video_cols", 2, ImGuiTableFlags_BordersInnerV);
            ImGui::TableSetupColumn("##formats", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("##clip", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextColumn();
        }
        // aperçus à l'échelle de la place disponible : YouTube 330 px et Instagram 186 px à l'origine ; TikTok garde la largeur de son titre (≈ 180 px) ; 2 espaces de 24 px
        auto        head = [](const char * name, const char * fmt) {  // case + nom + format : largeur du titre d'un bloc
            const ImGuiStyle & st = ImGui::GetStyle();
            return ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(name).x + st.ItemSpacing.x + ImGui::CalcTextSize(fmt).x;
        };
        const float k = std::clamp((ImGui::GetContentRegionAvail().x - 48 * g_dpi - head("TikTok", "9:16 · 1080 × 1920") - head("Instagram", "1:1 · 1080 × 1080")) /
                                       (330 * g_dpi), 0.6f, 1.3f);
        draw_video_network("YouTube", "16:9 · 1920 × 1080", video_yt, img_yt,
                           video_key.empty() ? std::string() : slot_youtube(video_key), 330 * g_dpi * k, 186 * g_dpi * k);
        ImGui::SameLine(0, 24 * g_dpi);
        draw_video_network("TikTok", "9:16 · 1080 × 1920", video_tt, img_tt,
                           video_key.empty() ? std::string() : slot_tiktok(video_key), 105 * g_dpi * k, 186 * g_dpi * k);
        ImGui::SameLine(0, 24 * g_dpi);
        draw_video_network("Instagram", "1:1 · 1080 × 1080", video_ig, img_ig, video_key, 186 * g_dpi * k, 186 * g_dpi * k);
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (!video_key.empty()) muted_wrapped(tr("Les images choisies sont mémorisées pour cet album."));
        if (two) {
            ImGui::Dummy(ImVec2(0, 12 * g_dpi));
            files_block();
            ImGui::TableNextColumn();
        }

        // 1.0.52 : clip animé
        ImGui::Checkbox(tr("Vrai clip : scénario d'après les paroles et les fiches, images par scène (IA locale), paroles en sous-titres"), &video_clip);
        if (video_clip) {
            // 1.0.53 : type de rendu des images (liste de choix)
            const auto & styles = clip_styles();
            if (!clip_style_loaded) {  // type de rendu mémorisé dans les réglages
                clip_style_loaded = true;
                for (int i = 0; i < (int) styles.size(); i++)
                    if (settings.clip_style_id == styles[i].id) clip_style = i;
            }
            clip_style = std::min<int>(clip_style, (int) styles.size() - 1);
            const float field_x = 130 * g_dpi;  // champs alignés (dans un tableau, SameLine compte déjà depuis le bord de la colonne)
            ImGui::TextColored(P.muted, "%s", tr("Type de rendu"));
            ImGui::SameLine(field_x);
            ImGui::SetNextItemWidth(330 * g_dpi);
            if (ImGui::BeginCombo("##clip_style", tr(styles[clip_style].label))) {
                for (int i = 0; i < (int) styles.size(); i++)
                    if (ImGui::Selectable(tr(styles[i].label), i == clip_style)) {
                        clip_style              = i;
                        settings.clip_style_id = styles[i].id;
                        save_settings(settings, settings_path);
                    }
                ImGui::EndCombo();
            }
            // 1.0.54 : moteur d'images local choisi avant la génération (temps estimé pour une image 16:9 de 1344 × 768)
            {
                const LocalImageModel & cur = local_image_model(settings.clip_image_model.empty() ? "klein" : settings.clip_image_model);
                ImGui::TextColored(P.muted, "%s", tr("Moteur d'images"));
                ImGui::SameLine(field_x);
                ImGui::SetNextItemWidth(330 * g_dpi);
                if (ImGui::BeginCombo("##clip_engine", cur.name)) {
                    for (const LocalImageModel & m : local_image_models()) {
                        const bool ready = image_model_ready(image_models_dir(), m);
                        char       label[160];
                        snprintf(label, sizeof(label), "%s · ≈ %d s %s%s", m.name, (int) (m.seconds * 1.75), tr("par image"), ready ? "" : tr(" · non installé"));
                        ImGui::BeginDisabled(!ready);
                        if (ImGui::Selectable(label, settings.clip_image_model == m.id)) {
                            settings.clip_image_model = m.id;
                            save_settings(settings, settings_path);
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::Checkbox(tr("Refaire le scénario et les images (sinon ceux déjà faits sont réutilisés : montage seul, plus rapide)"), &video_redo);
            ImGui::Checkbox(tr("Short (YouTube Shorts / TikTok, 9:16) centré sur le refrain, avec les images du clip"), &video_short);
            if (video_short) {  // 1.0.75 : durée au choix ; 30 s = durée conseillée pour faire découvrir un titre (TikTok : 21 à 34 s)
                static const int lengths[] = { 15, 30, 45, 60, 90 };
                const int        cur       = std::clamp(atoi(settings.clip_short_seconds.c_str()), 15, 90);
                ImGui::TextColored(P.muted, "%s", tr("Durée du Short"));
                ImGui::SameLine(field_x);
                ImGui::SetNextItemWidth(200 * g_dpi);
                if (ImGui::BeginCombo("##short_len", (std::to_string(cur) + " s" + (cur == 30 ? tr(" (conseillé)") : "")).c_str())) {
                    for (int n : lengths)
                        if (ImGui::Selectable((std::to_string(n) + " s" + (n == 30 ? tr(" (conseillé)") : "")).c_str(), n == cur)) {
                            settings.clip_short_seconds = std::to_string(n);
                            save_settings(settings, settings_path);
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("30 s : durée conseillée pour faire découvrir un titre (TikTok : 21 à 34 s ; YouTube Shorts accepte jusqu'à 3 min)."));
            }
            // 1.0.55 : nombre d'images du clip (0 = automatique : 6 à 12 selon les paroles), mémorisé dans les réglages
            // 1.0.56 : recommandation par morceau (d'après le nombre de lignes de paroles et la durée), que l'on suit ou que l'on
            // ajuste : plus (×1,5), moins (×0,5) ou un nombre fixe pour tous les morceaux
            {
                int mode = settings.clip_image_mode == "more" ? 1 : settings.clip_image_mode == "less" ? 2 : settings.clip_image_mode == "fixed" ? 3 : 0;
                int old  = mode;
                ImGui::TextColored(P.muted, "%s", tr("Nombre d'images"));
                ImGui::RadioButton(tr("Recommandé"), &mode, 0);
                ImGui::SameLine();
                ImGui::RadioButton(tr("Plus (× 1,5)"), &mode, 1);
                ImGui::SameLine();
                ImGui::RadioButton(tr("Moins (× 0,5)"), &mode, 2);
                ImGui::SameLine();
                ImGui::RadioButton(tr("Nombre fixe"), &mode, 3);
                if (mode != old) {
                    settings.clip_image_mode = mode == 1 ? "more" : mode == 2 ? "less" : mode == 3 ? "fixed" : "rec";
                    save_settings(settings, settings_path);
                }
                if (mode == 3) {
                    int n = std::clamp(atoi(settings.clip_image_count.c_str()), 0, 60);
                    ImGui::SetNextItemWidth(330 * g_dpi);
                    if (ImGui::SliderInt("##clip_n", &n, 0, 60, n == 0 ? tr("Auto") : "%d")) settings.clip_image_count = std::to_string(n);
                    if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
                }
                // une ligne par morceau : lignes de paroles, recommandation, nombre retenu
                const LocalImageModel & em = local_image_model(settings.clip_image_model.empty() ? "klein" : settings.clip_image_model);
                int                     total = 0, shown = 0;
                double                  audio = 0;
                for (const Song & sg : video_songs) {
                    if (!sg.long_rest.empty()) continue;  // partie 1 d'un morceau long : pas de clip
                    const int use = clip_images_for(sg);
                    total += use > 0 ? use : 9;
                    audio += sg.audio_seconds > 0 ? sg.audio_seconds : sg.duration;
                    if (shown++ < 4)
                        muted_wrapped(trf("%s : %d lignes de paroles · recommandé %d · retenu %s", clean_title(display_title(sg.title)).c_str(), video_lines[sg.id],
                                          video_rec[sg.id], use > 0 ? std::to_string(use).c_str() : tr("Auto")));
                }
                if (shown > 4) muted_wrapped(trf("… et %d autre(s) morceau(x).", shown - 4));
                // 1.0.57 : le montage compte aussi (≈ 1,1 fois la durée du morceau, mesuré, pour chaque format coché)
                const int img_min = std::max(1, (int) (total * em.seconds * 1.75 / 60.0 + 0.5));
                const int cut_min = std::max(1, (int) (audio * 1.1 * ((int) video_yt + (int) video_tt + (int) video_ig) / 60.0 + 0.5));
                muted_wrapped(trf("Calcul estimé : environ %d min d'images (celles déjà faites sont réutilisées) et %d min de montage pour les formats cochés.", img_min, cut_min));
            }
            // 1.0.54 : le clip affiche le titre, le groupe et l'album au début ; textes personnalisés de début et de fin
            muted_wrapped(tr("Au début du clip : titre du morceau, groupe et album. Vous pouvez ajouter vos propres textes (plusieurs lignes possibles) :"));
            ImGui::TextColored(P.muted, "%s", tr("Texte de début"));
            ImGui::SameLine(field_x);
            ImGui::SetNextItemWidth(440 * g_dpi);
            tx::InputTextMultiline("##clip_t1", &settings.clip_text_start, ImVec2(std::max(300 * g_dpi, ImGui::GetContentRegionAvail().x), 52 * g_dpi));
            if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
            ImGui::TextColored(P.muted, "%s", tr("Texte de fin"));
            ImGui::SameLine(field_x);
            ImGui::SetNextItemWidth(440 * g_dpi);
            tx::InputTextMultiline("##clip_t2", &settings.clip_text_end, ImVec2(std::max(300 * g_dpi, ImGui::GetContentRegionAvail().x), 52 * g_dpi));
            if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
            muted_wrapped(tr("Exemples : « Rock Ballads Fr » au début ; « Abonnez-vous à la chaîne » à la fin (affiché pendant les 8 dernières secondes)."));
        }
        if (video_clip) muted_wrapped(tr("Fichiers « _Clip.mp4 » dans les mêmes dossiers YouTube / TikTok / IG. Paroles : fiche « paroles.txt » du morceau (modifiable) ; scénario et images dans le dossier « clip_scenes » du morceau. À lancer au repos."));
        if (!video_clip && two) muted_wrapped(tr("Cochez « Vrai clip » pour un clip animé : scénario, images par scène, paroles en sous-titres et Shorts."));
        if (two) ImGui::EndTable();
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));

        if (!two) files_block();

        bool ok = (video_yt && !img_yt.empty()) || (video_tt && !img_tt.empty()) || (video_ig && !img_ig.empty()) ||
                  (video_clip && (video_yt || video_tt || video_ig || video_short));
        if (modal_buttons(tr("Créer les vidéos"), ok, &P.teal)) {
            create_videos();
            ImGui::CloseCurrentPopup();
        }
        draw_cover_splash();  // 1.0.78 : images créées depuis cette fenêtre
        ImGui::EndPopup();
    }

    void export_album(const std::string & key, const std::string & artist, const std::string & album,
                      const std::vector<Song> & all) {
        std::vector<Song> sel = album_songs(all, key, exp_validated);
        if (sel.empty()) {
            notify(exp_validated ? tr("Aucun morceau validé dans cet album : validez-en (pouce vert) ou décochez « seulement les validés ».")
                                 : tr("Aucun morceau terminé dans cet album."));
            return;
        }
        if (!(exp_flac) && !exp_mp3 && !(exp_wav)) {
            notify(tr("Choisissez au moins un format d'export."));
            return;
        }
        // 1.0.46 (demande de Herve) : export dans le dossier de l'album, sous-dossier « Audios » (comme « videos »)
        fs::path    parent = album_folder(artist, album) / L"Audios";
        std::string c      = cover_for(key);
        auto        job = new_job(tr("Export — ") + (artist.empty() ? album : artist + " — " + album));
        start_export(job, studio->paths(), sel, artist, album, c.empty() ? fs::path() : fs::path(widen(c)), parent, exp_flac,
                     exp_mp3, exp_wav);
        notify(trf("Export de « %s » lancé (%d morceau(x)).", album.c_str(), (int) sel.size()));
    }

    void draw_jobs() {
        int remove_at = -1, shown = 0;
        for (int i = (int) jobs.size() - 1; i >= 0 && shown < 4; i--, shown++) {
            Job &  j     = *jobs[i];
            int    state = j.state;
            ImVec4 color = state == 0 ? P.teal : state == 1 ? P.ok : P.danger;
            std::string msg, chapters;
            fs::path    out;
            {
                std::lock_guard<std::mutex> lock(j.mutex);
                msg      = j.message;
                chapters = j.chapters;
                out      = j.output;
            }
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.panel, color, 0.07f));
            ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.35f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpi, 8 * g_dpi));
            ImGui::BeginChild("job", ImVec2(0, 0),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushFont(g_bold);
            ImGui::TextColored(color, "%s", j.name.c_str());
            ImGui::PopFont();
            if (state == 0) {
                // 1.0.54 : vidéo en cours sur le total, puis l'étape (scénario, images…) quand il y en a une
                if (j.total > 0) {
                    std::string line = trf("Vidéo %d / %d", (int) j.item, (int) j.total);
                    if (!msg.empty()) line += " — " + msg;
                    muted_wrapped(line);
                }
                progress_bar(j.progress, ImVec2(-FLT_MIN, 0));
                if (danger_button(tr("Annuler"))) j.stop();
            } else {
                muted_wrapped(msg);
                if (state == 1 && !out.empty() && soft_button(tr("Afficher"), P.teal)) {
                    std::wstring arg = fs::is_directory(out) ? L"\"" + out.wstring() + L"\""
                                                             : L"/select,\"" + out.wstring() + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Fermer"))) remove_at = i;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        if (remove_at >= 0) jobs.erase(jobs.begin() + remove_at);
    }

    // Albums : regroupement par groupe + album, pochette, export, vidéo.
    struct AlbumInfo {
        std::string key, artist, album;
        int         done = 0, validated = 0;
        int         pending = 0;  // 1.0.36 : morceaux en file ou en pause (album « en attente »)
        std::string style;        // style d'un de ses morceaux (description de pochette par défaut)
    };

    std::vector<AlbumInfo> list_albums(const std::vector<Song> & songs) const {
        std::map<std::string, AlbumInfo> by_key;
        for (const Song & s : songs) {
            // 1.0.36 : les albums dont les morceaux attendent dans la file (en pause) sont listés aussi
            bool waiting = s.status != "done" && s.status != "error" && s.status != "cancelled";
            if ((s.status != "done" && !waiting) || s.album.empty()) continue;
            AlbumInfo & a = by_key[album_key(s.artist, s.album)];
            if (a.key.empty()) {
                a.key    = album_key(s.artist, s.album);
                a.artist = s.artist;
                a.album  = s.album;
            }
            if (a.style.empty()) a.style = s.style;
            if (waiting) {
                a.pending++;
                continue;
            }
            a.done++;
            a.validated += s.validated;
        }
        std::vector<AlbumInfo> out;
        for (auto & [k, a] : by_key) out.push_back(a);
        std::sort(out.begin(), out.end(), [](const AlbumInfo & x, const AlbumInfo & y) { return fold(x.album) < fold(y.album); });
        return out;
    }

    // Emplacement d'image dans la ligne d'un album : aperçu, légende, bouton de choix.
    // prompt (1.0.36) : description de l'image ; non nul = boutons « Générer » / « Régénérer » (pochettes créées par l'IA).
    void draw_image_slot(const std::string & slot, const char * label, float w, float h, const std::string * prompt = nullptr,
                         const std::string & album = std::string()) {
        std::string c = cover_for(slot);
        ImGui::PushID(slot.c_str());  // les trois emplacements ont des boutons de même libellé
        ImGui::BeginGroup();
        ImTextureID tex = 0;
        ImVec2      tsize;
        ImVec2      p0 = ImGui::GetCursorScreenPos();
        // 1.0.46 : clic sur l'image = aperçu agrandi
        if (ImGui::InvisibleButton("##apercu", ImVec2(w, h)) && !c.empty()) {
            preview_image = fs::path(widen(c));
            preview_open  = true;
        }
        if (!c.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Cliquer pour agrandir"));
        ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(w, h), ImGui::GetColorU32(P.chip), 6 * g_dpi);
        if (!c.empty() && thumb_texture(fs::path(widen(c)), tex, tsize, 256)) {
            float  k  = std::min(w / tsize.x, h / tsize.y);
            ImVec2 sz(tsize.x * k, tsize.y * k);
            ImVec2 a = p0 + ImVec2((w - sz.x) / 2, (h - sz.y) / 2);
            ImGui::GetWindowDrawList()->AddImage(tex, a, a + sz);
        }
        ImGui::TextColored(P.muted, "%s", label);
        if (soft_button(c.empty() ? tr("Choisir…") : tr("Changer…"), P.violet)) set_image(slot, wtr("Image de l'album").c_str());
        if (prompt) {
            std::string why, lbl;
            bool        ready = image_engine_ready(why, lbl);
            // 1.0.51 : le bouton ouvre le menu des modèles (celui des Réglages, ou un autre modèle local pour cette image)
            ImGui::BeginDisabled(cover_job != nullptr || trim(*prompt).empty());
            if (soft_button((std::string(c.empty() ? tr("Générer") : tr("Régénérer")) + " ▾").c_str(), P.pink)) ImGui::OpenPopup("##modele");
            if (album_menu_test && slot == album_open) {  // essai : menu ouvert d'office
                ImGui::OpenPopup("##modele");
                album_menu_test = false;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", !ready ? why.c_str() : tr("Crée l'image avec la description de l'album (Réglages › IA › Pochettes)."));
            if (ImGui::BeginPopup("##modele")) {
                std::string model;
                if (image_model_menu(model)) {
                    CoverTask t;
                    t.slot   = slot;
                    t.prompt = *prompt;
                    t.label  = album + " · " + label;
                    t.model  = model;
                    cover_size(slot, t.w, t.h);
                    if (c.empty()) start_covers({ t });
                    else {  // une image existe : confirmation avant de la remplacer
                        pending_regen      = t;
                        pending_regen_open = true;
                    }
                }
                ImGui::EndPopup();
            }
        }
        if (!c.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("×")) {
                settings.covers.erase(slot);
                save_settings(settings, settings_path);
            }
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }

    // ---- 1.0.46 : aperçu agrandi d'une image, toutes les pochettes d'un album, suppression d'un album
    fs::path               preview_image;
    bool                   preview_open = false, all_covers_open = false, delete_album_open = false;
    // Essai : les captures de l'aide ne montrent aucun nom de personne réelle (balises de plusieurs mots à majuscule)
    static std::string test_clean_style(const std::string & style) {
        std::vector<std::string> kept;
        for (const std::string & tag : split_style(style))
            if (!(isupper((unsigned char) tag[0]) && tag.find(' ') != std::string::npos)) kept.push_back(tag);
        return join_style(kept);
    }
    float                  test_scroll = 0;         // essai : défilement des Réglages pour les captures de l'aide
    int                    test_scroll_frames = 0;
    std::vector<CoverTask> all_covers_tasks;
    std::string            all_covers_key, all_covers_label, delete_album_key, delete_album_artist, delete_album_name;

    void delete_album() {
        fs::path dir = album_folder(delete_album_artist, delete_album_name);
        for (const Song & s : studio->snapshot())
            if (!s.album.empty() && album_key(s.artist, s.album) == delete_album_key && player.id() == s.id) player.close();
        std::string err;
        if (!studio->remove_folder(dir, err)) {
            notify(err);
            return;
        }
        for (const std::string & suffix : { std::string(), std::string("#youtube"), std::string("#tiktok") }) settings.covers.erase(delete_album_key + suffix);
        settings.cover_prompts.erase(delete_album_key);
        settings.album_sounds.erase(delete_album_key);
        if (!ai_album_name.empty() && album_key(ai_album_band, ai_album_name) == delete_album_key) {
            ai_album_band.clear();
            ai_album_name.clear();
            ai_album_titles.clear();
            save_last_album();
        }
        save_settings(settings, settings_path);
        exists_cache.clear();
        notify(trf("Album « %s » envoyé à la corbeille.", delete_album_name.c_str()));
    }

    void draw_album_tool_modals() {
        // Aperçu agrandi : la plus grande taille qui tient dans 85 % de la fenêtre ; clic ou Échap pour fermer.
        const char * pv = "##apercu_pochette";
        if (preview_open) {
            ImGui::OpenPopup(pv);
            preview_open = false;
        }
        ImVec2 vp = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(pv, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImTextureID tex = 0;
            ImVec2      tsize;
            if (thumb_texture(preview_image, tex, tsize, 1600)) {
                float k = std::min({ vp.x * 0.85f / tsize.x, vp.y * 0.85f / tsize.y, 1.f });
                ImGui::Image(tex, ImVec2(tsize.x * k, tsize.y * k));
                if (ImGui::IsItemClicked()) ImGui::CloseCurrentPopup();
            } else {
                ImGui::TextUnformatted(tr("Image illisible."));
            }
            ImGui::TextColored(P.muted, "%s", tr("Cliquez sur l'image ou appuyez sur Échap pour fermer."));
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // Toutes les pochettes : certaines existent déjà
        const char * ac = tr("Toutes les pochettes de l'album");
        if (all_covers_open) {
            ImGui::OpenPopup(ac);
            all_covers_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480 * g_dpi, 0));
        if (ImGui::BeginPopupModal(ac, nullptr, ImGuiWindowFlags_NoResize)) {
            int existing = 0;
            for (const CoverTask & t : all_covers_tasks) existing += !cover_for(t.slot).empty();
            ImGui::TextWrapped("%s", trf("L'album « %s » a déjà %d image(s) sur 3. Créer seulement les images manquantes, ou tout refaire ?",
                                         all_covers_label.c_str(), existing).c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::BeginDisabled(existing == (int) all_covers_tasks.size());
            if (ImGui::Button(tr("Seulement les manquantes"))) {
                std::vector<CoverTask> missing;
                for (const CoverTask & t : all_covers_tasks)
                    if (cover_for(t.slot).empty()) missing.push_back(t);
                start_covers(missing);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (accent_button(tr("Tout remplacer"), ImVec2(0, 0), &P.pink)) {
                start_covers(all_covers_tasks);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // Suppression d'un album : tout son dossier à la corbeille de Windows
        const char * da = tr("Supprimer l'album ?");
        if (delete_album_open) {
            ImGui::OpenPopup(da);
            delete_album_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
        if (ImGui::BeginPopupModal(da, nullptr, ImGuiWindowFlags_NoResize)) {
            fs::path        dir = album_folder(delete_album_artist, delete_album_name);
            int             songs = 0, files = 0;
            std::error_code ec;
            for (const Song & s : studio->snapshot()) songs += !s.album.empty() && album_key(s.artist, s.album) == delete_album_key;
            for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                files += it->is_regular_file(ec);
            ImGui::TextWrapped("%s", trf("L'album « %s » (%d morceau(x), %d fichier(s) : audio, pochettes, vidéos, exports) sera envoyé à la corbeille de Windows : vous pourrez l'y récupérer.",
                                         delete_album_name.c_str(), songs, files).c_str());
            path_line(dir);
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (accent_button(tr("Mettre à la corbeille"), ImVec2(0, 0), &P.danger)) {
                delete_album();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // 1.0.41 : renommer le groupe et/ou le titre d'un album. Étiquettes des morceaux réécrites (edit_metadata),
    // pochettes, images des vidéos, description de pochette et dernier album suivent la nouvelle clé.
    std::string rename_key, rename_artist, rename_title;
    bool        rename_open = false;

    void rename_album(const std::string & old_key, const std::string & artist, const std::string & album) {
        std::string new_key = album_key(artist, album);
        fs::path    old_dir;
        for (const Song & s : studio->snapshot())
            if (!s.album.empty() && album_key(s.artist, s.album) == old_key) {
                if (old_dir.empty()) old_dir = album_folder(s.artist, s.album);
                studio->edit_metadata(s.id, s.title, artist, album, s.track);  // le dossier du morceau suit (1.0.43)
            }
        // 1.0.43 : pochettes et vidéos de l'album suivent ; les dossiers de morceaux pas encore déplaçables (en file)
        // restent et suivront à leur prochain enregistrement.
        fs::path        new_dir = album_folder(artist, album);
        std::error_code ec;
        if (!old_dir.empty() && old_dir != new_dir && fs::is_directory(old_dir, ec)) {
            std::vector<fs::path> rest;
            for (const auto & e : fs::directory_iterator(old_dir, ec))
                if (!fs::exists(e.path() / L"meta.json", ec)) rest.push_back(e.path());
            for (const fs::path & p : rest) merge_folder(p, new_dir / p.filename());
            // chemins mémorisés des images de l'album : même fichier dans le nouveau dossier
            std::wstring old_prefix = old_dir.wstring() + L"\\";
            for (auto & [slot, path] : settings.covers) {
                std::wstring w = widen(path);
                if (w.rfind(old_prefix, 0) == 0 && fs::exists(new_dir / w.substr(old_prefix.size()), ec))
                    path = narrow((new_dir / w.substr(old_prefix.size())).wstring());
            }
        }
        exists_cache.clear();
        if (new_key != old_key) {
            for (const std::string & suffix : { std::string(), std::string("#youtube"), std::string("#tiktok") }) {
                auto it = settings.covers.find(old_key + suffix);
                if (it == settings.covers.end()) continue;
                // album fusionné avec un album existant : ses propres images sont gardées
                if (settings.covers.find(new_key + suffix) == settings.covers.end()) settings.covers[new_key + suffix] = it->second;
                settings.covers.erase(it);
            }
            for (auto * m : { &settings.cover_prompts, &settings.album_sounds }) {  // 1.0.47 : le son de l'album suit aussi
                auto p = m->find(old_key);
                if (p == m->end()) continue;
                std::string v = p->second;
                m->erase(p);
                if (trim((*m)[new_key]).empty()) (*m)[new_key] = v;
            }
        }
        if (!ai_album_name.empty() && album_key(ai_album_band, ai_album_name) == old_key) {
            ai_album_band = artist;
            ai_album_name = album;
            save_last_album();
        }
        save_settings(settings, settings_path);
        notify(trf("Album renommé : « %s » (%s).", album.c_str(), artist.empty() ? tr("(sans groupe)") : artist.c_str()));
    }

    void draw_rename_album_modal() {
        const char * title = tr("Renommer l'album");
        if (rename_open) {
            ImGui::OpenPopup(title);
            rename_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize)) return;
        ImGui::TextColored(P.muted, "%s", tr("Groupe / chanteur"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        tx::InputText("##rename_artist", &rename_artist);
        ImGui::TextColored(P.muted, "%s", tr("Titre de l'album"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        tx::InputText("##rename_album", &rename_title);
        muted_wrapped(tr("Les étiquettes des morceaux sont réécrites ; les pochettes et les images des vidéos sont gardées."));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (modal_buttons(tr("Renommer"), !trim(rename_title).empty())) {
            rename_album(rename_key, trim(rename_artist), trim(rename_title));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void draw_albums_modal() {
        if (open_albums) {
            ImGui::OpenPopup(tr("Albums"));
            open_albums = false;
        }
        albums_drawn = false;
        ImGui::SetNextWindowSize(ImVec2(1080 * g_dpi, 0));  // 1.0.51 : plus large pour la ligne compacte (voyants + actions)
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));  // 1.0.78 : reste centrée (Studio agrandi, rétabli)
        if (!ImGui::BeginPopupModal(tr("Albums"), nullptr, ImGuiWindowFlags_NoResize)) return;
        albums_drawn = true;
        std::vector<Song> all    = studio->snapshot();
        auto              albums = list_albums(all);
        muted_wrapped(tr("Un album regroupe les morceaux terminés ayant le même groupe et le même titre d'album (renseignés à l'import ou avec « Modifier »). La pochette (carrée) est intégrée aux fichiers exportés ; les images YouTube (16:9) et TikTok (9:16) servent aux vidéos."));
        ImGui::Checkbox(tr("Seulement les morceaux validés"), &exp_validated);
        ImGui::SameLine(0, 18 * g_dpi);
        ImGui::TextColored(P.muted, tr("Formats d'export"));
        ImGui::SameLine();
        ImGui::Checkbox("FLAC", &exp_flac);
        ImGui::SameLine();
        ImGui::Checkbox("MP3", &exp_mp3);
        ImGui::SameLine();
        ImGui::Checkbox("WAV", &exp_wav);
        {  // 1.0.82 : Shorts manquants de toute la bibliothèque, en une seule file ; 1.0.83 : contrôle qualité
            const char * lib_label = tr("Shorts manquants (bibliothèque)…");
            const char * qc_label  = tr("Contrôle qualité (bibliothèque)");
            const float  pad       = ImGui::GetStyle().FramePadding.x * 2;
            const bool   busy      = studio->local_active() || ai_local_busy();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(lib_label).x - ImGui::CalcTextSize(qc_label).x - pad * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
            ImGui::BeginDisabled(busy);
            if (soft_button(qc_label, P.amber, false)) start_quality_check(all, std::string(), tr("Toute la bibliothèque"));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", busy ? tr("Le contrôle se fait au repos : attendez la fin de la génération en cours.")
                                             : tr("Sans réécoute : durée, paroles chantées (Whisper), silences anormaux, partie trop courte. Les morceaux à refaire reçoivent un pouce rouge ; rapport « controle_qualite.txt »."));
            ImGui::SameLine();
            ImGui::BeginDisabled(busy);
            if (soft_button(lib_label, P.teal, false)) ask_missing_shorts(all, std::string(), tr("Toute la bibliothèque"));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", busy ? tr("Les images des clips se font au repos : attendez la fin de la génération en cours.")
                                             : tr("Crée un Short pour chaque morceau terminé qui n'en a pas encore, dans tous les albums."));
        }
        if (!quality_test.empty() && !all.empty()) {
            start_quality_check(all, quality_test, quality_test);
            quality_test.clear();
        }
        if (shorts_test && !all.empty()) {  // essai : bibliothèque lue
            shorts_test = false;
            ask_missing_shorts(all, std::string(), tr("Toute la bibliothèque"));
        }
        draw_shorts_confirm();
        ImGui::BeginChild("albums_list", ImVec2(0, 470 * g_dpi), ImGuiChildFlags_Borders);
        if (albums.empty()) {
            ImGui::Dummy(ImVec2(0, 20 * g_dpi));
            muted_wrapped(tr("Aucun album pour l'instant."));
        }
        if (cover_job) draw_cover_progress();  // 1.0.36
        if (desc_job) {  // 1.0.57
            std::string cur;
            {
                std::lock_guard<std::mutex> lock(desc_job->mutex);
                cur = desc_job->current;
            }
            progress_bar((float) desc_job->done / (float) desc_job->total, ImVec2(-FLT_MIN, 0), trf("Description : %s", cur.c_str()).c_str());
            if (soft_button(tr("Arrêter"), P.danger)) desc_job->cancel = true;
        }
        // 1.0.51 : une ligne compacte par album (vignette, voyants des images et des vidéos, actions) ; un clic sur la flèche
        // ou la vignette déplie la fiche à onglets « Pochettes » / « Vidéos ». Une seule fiche dépliée à la fois.
        for (const AlbumInfo & a : albums) {
            ImGui::PushID(a.key.c_str());
            // 1.0.36 : description de l'image (écrite par l'IA du plan d'album, sinon tirée du style), modifiable dans l'onglet
            std::string       prompt      = cover_prompt_for(a.key, a.album, a.style);
            fs::path          vdir        = album_folder(a.artist, a.album) / L"videos";
            const std::string slots[3]    = { a.key, slot_youtube(a.key), slot_tiktok(a.key) };
            const char *      img_tags[3] = { "1:1", "16:9", "9:16" };
            // sous-dossiers écrits par les vidéos (1.0.43) ; 1.0.79 : 4e format, les Shorts (videos\Shorts)
            const wchar_t *   vid_dirs[4] = { L"YouTube", L"TikTok", L"IG", L"Shorts" };
            const char *      vid_tags[4] = { "YouTube", "TikTok", "Instagram", "Shorts" };
            int               imgs = 0, vids = 0, vcount[4];
            for (int i = 0; i < 4; i++) {
                if (i < 3) imgs += !cover_for(slots[i]).empty();
                vcount[i] = video_count(vdir / vid_dirs[i]);
                vids += vcount[i] > 0;
            }
            bool open   = album_open == a.key;
            auto toggle = [&] {
                album_open = open ? std::string() : a.key;
                open       = !open;
            };
            float row_y = ImGui::GetCursorPosY();
            if (ImGui::ArrowButton("##ouvrir", open ? ImGuiDir_Down : ImGuiDir_Right)) toggle();
            ImGui::SameLine();
            {  // vignette de la pochette 1:1 (clic = déplier)
                float       t  = 56 * g_dpi;
                ImVec2      p0 = ImGui::GetCursorScreenPos();
                std::string c  = cover_for(a.key);
                if (ImGui::InvisibleButton("##vignette", ImVec2(t, t))) toggle();
                ImGui::GetWindowDrawList()->AddRectFilled(p0, p0 + ImVec2(t, t), ImGui::GetColorU32(P.chip), 4 * g_dpi);
                ImTextureID tex = 0;
                ImVec2      ts;
                if (!c.empty() && thumb_texture(fs::path(widen(c)), tex, ts, 256)) ImGui::GetWindowDrawList()->AddImage(tex, p0, p0 + ImVec2(t, t));
            }
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::BeginGroup();
            ImGui::PushFont(g_bold);
            ImGui::TextUnformatted(a.album.c_str());
            ImGui::PopFont();
            ImGui::TextColored(P.blue, "%s", a.artist.empty() ? tr("(sans groupe)") : a.artist.c_str());
            ImGui::TextColored(P.muted, tr("%d terminé(s) · %d validé(s)"), a.done, a.validated);
            if (a.pending > 0) {
                ImGui::SameLine();
                ImGui::TextColored(P.amber, "%s", trf("· en attente : %d", a.pending).c_str());
            }
            ImGui::EndGroup();
            float row_end = ImGui::GetCursorPosY();  // bas de la ligne (vignette ou textes)
            // Voyants : vert = fait, gris = pas encore (positions fixes : deux lignes alignées sous « Images » / « Vidéos »)
            float img_end = 0;  // 1.0.79 : bord droit des voyants « Images » (le bouton « Pochettes » se place après)
            {
                float  lx = 370 * g_dpi, px = lx + 62 * g_dpi, lh = ImGui::GetTextLineHeight() + 8 * g_dpi;
                ImVec2 tip0 = ImGui::GetCursorScreenPos();
                for (int line = 0; line < 2; line++) {
                    float y = row_y + 8 * g_dpi + line * lh, x = px;
                    ImGui::SetCursorPos(ImVec2(lx, y + 1 * g_dpi));
                    ImGui::TextColored(P.muted, "%s", line == 0 ? tr("Images") : tr("Vidéos"));
                    for (int i = 0; i < (line == 0 ? 3 : 4); i++) {
                        const char * tag  = line == 0 ? img_tags[i] : vid_tags[i];
                        bool         done = line == 0 ? !cover_for(slots[i]).empty() : vcount[i] > 0;
                        ImGui::SetCursorPos(ImVec2(x, y));
                        pill(tag, done ? P.ok : P.muted);
                        x += pill_width(tag) + 4 * g_dpi;
                    }
                    if (line == 0) img_end = x;
                }
                (void) tip0;
                ImVec2 win = ImGui::GetWindowPos() - ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
                if (ImGui::IsMouseHoveringRect(win + ImVec2(lx, row_y), win + ImVec2(px + 250 * g_dpi, row_y + 8 * g_dpi + 2 * lh)))
                    ImGui::SetTooltip("%s", trf("Pochettes %d/3 · Vidéos %d/4", imgs, vids).c_str());
            }
            // Actions courantes à droite ; Renommer, Supprimer et Ouvrir le dossier dans « … »
            // Au bout de la ligne « Images » : 1.0.78 : une pochette manque → « Pochettes » (seulement les manquantes) ;
            // 1.0.80 : les 3 existent → « Refaire les pochettes » (les 3 formats, après confirmation « Tout remplacer »)
            const bool missing = imgs < 3;
            {
                ImGui::SetCursorPos(ImVec2(img_end + 6 * g_dpi, row_y + 6 * g_dpi));
                ImGui::BeginDisabled(cover_job != nullptr);
                if (soft_button(missing ? tr("Pochettes") : tr("Refaire les pochettes"), P.pink, false)) {
                    int               skipped = 0;
                    const std::string prompt  = album_image_prompt(a.key, a.album, a.artist);
                    if (missing) {
                        start_covers(album_cover_tasks(a.key, a.album, prompt, true, true, true, skipped));
                    } else {
                        all_covers_tasks = album_cover_tasks(a.key, a.album, prompt, true, true, false, skipped);
                        all_covers_key   = a.key;
                        all_covers_label = a.album;
                        all_covers_open  = true;  // fenêtre de confirmation : « Tout remplacer »
                    }
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", missing ? trf("Crée les images manquantes de l'album (%d sur 3) avec le modèle d'images des Réglages.", 3 - imgs).c_str()
                                                    : tr("Recrée la pochette 1:1 et les images YouTube 16:9 et TikTok 9:16 de l'album (après confirmation), avec le modèle d'images des Réglages et la description de l'album."));
            }
            ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - 330 * g_dpi, row_y + 14 * g_dpi));
            int usable = exp_validated ? a.validated : a.done;
            ImGui::BeginDisabled(usable == 0);
            if (soft_button(tr("Exporter…"), P.green, false)) export_album(a.key, a.artist, a.album, all);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(usable == 0);
            if (soft_button(tr("Vidéos…"), P.teal, false)) open_video_dialog(album_songs(all, a.key, exp_validated), a.album, a.key);
            ImGui::EndDisabled();
            ImGui::SameLine();
            // 1.0.47 (b2) : ajouter des morceaux à cet album avec « Composer avec l'IA » (même groupe, même son)
            ImGui::BeginDisabled(ai_local_busy());
            if (soft_button(tr("Compléter…"), P.violet, false)) {
                settings.ai_album_mode  = true;
                settings.ai_band        = a.artist;
                settings.ai_album_title = a.album;
                for (const Song & s : all) {  // LoRA du premier morceau de l'album, cochées dans le formulaire
                    if (s.album.empty() || album_key(s.artist, s.album) != a.key) continue;
                    form.loras.clear();
                    for (const auto & [name, scale] : s.adapters) form.loras.push_back({ name, (float) scale, true });
                    scan_loras(true);
                    sync_lora_triggers();
                    break;
                }
                save_settings(settings, settings_path);
                form_tab_request = 1;
                ImGui::CloseCurrentPopup();
                notify(trf("Composer avec l'IA : choisissez le nombre de morceaux à ajouter à « %s », puis lancez l'album.", a.album.c_str()));
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Ajoute des morceaux à cet album avec « Composer avec l'IA » : même groupe, même son, nouveaux titres, pistes à la suite."));
            ImGui::SameLine();
            if (soft_button(" … ", P.muted, false)) ImGui::OpenPopup("##plus");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Plus d'actions"));
            if (ImGui::BeginPopup("##plus")) {
                bool generating = std::any_of(all.begin(), all.end(), [&](const Song & s) {
                    return s.status == "running" && !s.album.empty() && album_key(s.artist, s.album) == a.key;
                });
                bool busy_album = std::any_of(all.begin(), all.end(), [&](const Song & s) {
                    return s.active() && !s.album.empty() && album_key(s.artist, s.album) == a.key;
                });
                if (ImGui::MenuItem(tr("Renommer…"), nullptr, false, !generating && !cover_job)) {
                    rename_key    = a.key;
                    rename_artist = a.artist;
                    rename_title  = a.album;
                    rename_open   = true;
                }
                if (generating && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("Un morceau de cet album est en cours de génération."));
                if (ImGui::MenuItem(tr("Ouvrir le dossier de l'album")))
                    ShellExecuteW(nullptr, L"open", album_folder(a.artist, a.album).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::MenuItem(tr("Contrôle qualité de l'album"), nullptr, false, !ai_local_busy() && !studio->local_active()))
                    start_quality_check(all, a.key, a.album);  // 1.0.83
                if (ImGui::MenuItem(tr("Shorts manquants de l'album…"), nullptr, false, !ai_local_busy() && !studio->local_active()))
                    ask_missing_shorts(all, a.key, a.album);  // 1.0.82
                if (ImGui::MenuItem(tr("Rédiger les descriptions (IA locale)"), nullptr, false, !ai_local_busy() && !studio->local_active()))
                    start_descriptions(a.key, all);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("Écrit un fichier « description.txt » pour le groupe, l'album et chaque morceau. Un fichier déjà présent n'est jamais remplacé : supprimez-le pour le refaire."));
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, P.danger);
                bool del = ImGui::MenuItem(tr("Supprimer l'album…"), nullptr, false, !busy_album && !cover_job);
                ImGui::PopStyleColor();
                if (del) {
                    delete_album_key    = a.key;
                    delete_album_artist = a.artist;
                    delete_album_name   = a.album;
                    delete_album_open   = true;
                }
                if (busy_album && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("Des morceaux de cet album sont en cours ou en attente de génération : annulez-les d'abord."));
                ImGui::EndPopup();
            }
            ImGui::SetCursorPosY(std::max(row_end, ImGui::GetCursorPosY()));
            if (open && album_scroll) {  // essai : amener l'album déplié en vue
                ImGui::SetScrollHereY(0.1f);
                album_scroll = false;
            }
            if (open) {
                // Onglets : tous déclarés à chaque image, barre fermée, puis le contenu (règle de la 1.0.27)
                std::string t1 = trf("Pochettes %d/3", imgs) + "###pochettes", t2 = trf("Vidéos %d/4", vids) + "###videos";
                if (ImGui::BeginTabBar("##onglets")) {
                    if (ImGui::BeginTabItem(t1.c_str(), nullptr, album_tab_request == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                        album_tab = 0;
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem(t2.c_str(), nullptr, album_tab_request == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                        album_tab = 1;
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                    album_tab_request = -1;
                }
                if (album_tab == 0) {
                    float h = 120 * g_dpi;
                    draw_image_slot(a.key, tr("Pochette 1:1"), h, h, &prompt, a.album);
                    ImGui::SameLine(0, 10 * g_dpi);
                    draw_image_slot(slot_youtube(a.key), "YouTube 16:9", h * 16 / 9, h, &prompt, a.album);
                    ImGui::SameLine(0, 10 * g_dpi);
                    draw_image_slot(slot_tiktok(a.key), "TikTok 9:16", h * 9 / 16, h, &prompt, a.album);
                    ImGui::SameLine(0, 16 * g_dpi);
                    // Description de la pochette (1.0.36), en anglais pour les modèles d'images ; plus haute en 1.0.51
                    ImGui::BeginGroup();
                    ImGui::TextColored(P.muted, "%s", tr("Description de la pochette (anglais, sans texte dans l'image)"));
                    // ImGui 1.91 ne coupe pas les lignes : les espaces deviennent des retours à la ligne (soft_wrap), enregistrés en espaces
                    prompt_wrap_w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ScrollbarSize - 4 * g_dpi;
                    std::string edit = prompt;
                    soft_wrap(edit, prompt_wrap_w);
                    auto rewrap = [](ImGuiInputTextCallbackData * d) -> int {
                        std::string text(d->Buf, d->BufTextLen), out = text;
                        soft_wrap(out, *(float *) d->UserData);
                        if (out != text) {  // même longueur : le curseur reste à sa place
                            int c = d->CursorPos;
                            d->DeleteChars(0, d->BufTextLen);
                            d->InsertChars(0, out.c_str());
                            d->CursorPos      = c;
                            d->SelectionStart = d->SelectionEnd = c;
                        }
                        return 0;
                    };
                    if (tx::InputTextMultiline("##cover_prompt", &edit, ImVec2(-FLT_MIN, h + ImGui::GetFrameHeight()), ImGuiInputTextFlags_CallbackEdit,
                                               rewrap, &prompt_wrap_w)) {
                        std::replace(edit.begin(), edit.end(), '\n', ' ');
                        settings.cover_prompts[a.key] = edit;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
                    // 1.0.46 : toutes les pochettes de l'album d'un coup (1:1, YouTube, TikTok) ; 1.0.51 : choix du modèle
                    ImGui::BeginDisabled(cover_job != nullptr || trim(prompt).empty());
                    if (soft_button((std::string(tr("Toutes les pochettes")) + " ▾").c_str(), P.pink, false)) ImGui::OpenPopup("##toutes");
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", tr("Crée la pochette 1:1 et les images YouTube et TikTok de l'album avec sa description."));
                    if (ImGui::BeginPopup("##toutes")) {
                        std::string model;
                        if (image_model_menu(model)) {
                            int skipped = 0;
                            all_covers_tasks = album_cover_tasks(a.key, a.album, prompt, true, true, false, skipped);
                            for (CoverTask & t : all_covers_tasks) t.model = model;
                            int existing = 0;
                            for (const CoverTask & t : all_covers_tasks) existing += !cover_for(t.slot).empty();
                            all_covers_key   = a.key;
                            all_covers_label = a.album;
                            if (existing == 0) start_covers(all_covers_tasks);
                            else all_covers_open = true;  // confirmation : remplacer ou seulement les manquantes
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::EndGroup();
                } else {
                    for (int i = 0; i < 4; i++) {
                        ImGui::TextColored(vcount[i] > 0 ? P.ok : P.muted, "●");
                        ImGui::SameLine();
                        ImGui::TextUnformatted(vid_tags[i]);
                        ImGui::SameLine(140 * g_dpi);
                        if (vcount[i] > 0) ImGui::TextColored(P.ok, "%s", trf("%d vidéo(s)", vcount[i]).c_str());
                        else ImGui::TextColored(P.muted, "%s", tr("pas encore créée"));
                    }
                    ImGui::Dummy(ImVec2(0, 4 * g_dpi));
                    bool has_dir = cached_exists(vdir);
                    ImGui::BeginDisabled(!has_dir);
                    if (soft_button(tr("Ouvrir le dossier des vidéos"), P.teal, false))
                        ShellExecuteW(nullptr, L"open", vdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextColored(P.muted, "%s", tr("Les vidéos se créent avec « Vidéos… »."));
                }
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();
        draw_video_modal();  // dans la portée de « Albums » : s'ouvre par-dessus au lieu de la fermer
        draw_cover_splash();  // 1.0.37 : au-dessus de « Albums »
        draw_rename_album_modal();  // 1.0.41
        draw_album_tool_modals();   // 1.0.46 : aperçu, toutes les pochettes, suppression
        // « Régénérer » (1.0.36) : remplacer une image existante seulement après confirmation
        const char * regen_title = tr("Remplacer l'image ?");
        if (pending_regen_open) {
            ImGui::OpenPopup(regen_title);
            pending_regen_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(regen_title, nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", trf("L'image « %s » sera remplacée par une nouvelle image créée par l'IA.", pending_regen.label.c_str()).c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (accent_button(tr("Remplacer"), ImVec2(0, 0), &P.pink)) {
                start_covers({ pending_regen });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndPopup();
    }

    // Toute suppression définitive de fichier passe par ici (1.0.32) : refusée hors du dossier du programme, pour
    // qu'une copie du Studio n'efface jamais les modèles ou LoRA d'une autre (incident de la 1.0.31).
    bool remove_refused = false;  // dernier appel de remove_inside refusé (déjà signalé à l'écran)
    bool remove_inside(const fs::path & p, std::error_code & ec) {
        ec.clear();
        remove_refused = false;
        if (!fs::exists(p, ec)) return false;
        if (!inside_app_dir(p)) {
            remove_refused = true;
            notify(trf("Suppression refusée : « %s » est hors du dossier du programme.", narrow(p.wstring()).c_str()));
            return false;
        }
        return fs::remove(p, ec);
    }
    // Chemin complet affiché dans les confirmations de suppression (1.0.32).
    void path_line(const fs::path & p) {
        ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
        ImGui::TextWrapped("%s", narrow(p.wstring()).c_str());
        ImGui::PopStyleColor();
    }
    // Suppression d'un modèle (fichier GGUF) : définitive, il peut être téléchargé de nouveau.
    void delete_model_file(const std::string & file) {
        std::error_code ec;
        remove_inside(studio->paths().gguf_dir / widen(file), ec);
        if (remove_refused) return;  // déjà signalé
        if (ec) {
            notify(tr("Suppression impossible : ") + narrow(widen(ec.message())));
            return;
        }
        const std::string keep = current_model == file ? std::string() : current_model;
        if (settings.model == file) {
            settings.model.clear();
            save_settings(settings, settings_path);
        }
        init_models();  // liste à jour ; un autre modèle remplace celui qui vient d'être supprimé
        if (!keep.empty() && std::find(models.begin(), models.end(), keep) != models.end()) {
            current_model = keep;
            studio->set_active_model(keep);
        }
        notify(tr("Modèle supprimé : ") + model_label(file));
    }
    // Suppression des fichiers d'une LoRA (models/adapters), définitive.
    void delete_lora_files(const std::vector<std::string> & files, const std::string & name) {
        std::error_code ec;
        for (const std::string & f : files) {
            remove_inside(studio->paths().adapters / widen(f), ec);
            if (remove_refused) return;  // déjà signalé
            if (ec) {
                notify(tr("Suppression impossible : ") + narrow(widen(ec.message())));
                break;
            }
        }
        scan_loras(true);
        if (!ec) notify(tr("LoRA supprimée : ") + name);
    }
    // Petit bouton rouge des tableaux ; grisé pendant une génération (les fichiers sont alors utilisés).
    bool delete_button(const char * tip) {
        bool busy = studio->local_active();
        ImGui::BeginDisabled(busy);
        bool clicked = soft_button(tr("Supprimer"), P.danger);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", busy ? tr("Indisponible pendant une génération.") : tip);
        }
        return clicked;
    }

    // Tableau des LoRA du catalogue : téléchargement direct dans models/adapters.
    void draw_lora_catalog() {
        scan_loras();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.pink, tr("LoRA"));
        ImGui::PopFont();
        muted_wrapped(tr("Extensions qui orientent le style, la voix ou l'ambiance d'un morceau. Le téléchargement va directement dans le dossier de l'application ; la LoRA se coche ensuite dans le formulaire (plusieurs possibles à la fois). Licences propres à chaque LoRA (le plus souvent usage non commercial)."));
        ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##lora_catalog", 6, tf, ImVec2(0, 230 * g_dpi))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(tr("LoRA"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("Type"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
            ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
            ImGui::TableSetupColumn(tr("Mot"), ImGuiTableColumnFlags_WidthFixed, 124 * g_dpi);
            ImGui::TableSetupColumn(tr("Téléchargement"), ImGuiTableColumnFlags_WidthFixed, 210 * g_dpi);
            ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
            ImGui::TableHeadersRow();
            for (const LoraEntry & e : LORA_CATALOG) {
                ImGui::PushID(e.id);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(tr(e.name));
                if (ImGui::IsItemHovered()) {
                    std::string tip = std::string(tr(e.description)) + tr("\nAuteur : ") + e.author;
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("↗")) ShellExecuteW(nullptr, L"open", widen(e.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Ouvrir la page de la LoRA (auteur, licence, exemples)"));
                ImGui::TableSetColumnIndex(1);
                const char * kind = !strcmp(e.kind, "style") ? tr("Style") : !strcmp(e.kind, "artist") ? tr("Artiste") : !strcmp(e.kind, "slider") ? tr("Curseur") : !strcmp(e.kind, "instrument") ? tr("Instrum.") : tr("Compos.");
                ImGui::TextColored(P.muted, "%s", kind);
                ImGui::TableSetColumnIndex(2);
                unsigned long long total = 0;
                for (int i = 0; i < e.file_count; i++) total += e.files[i].bytes;
                ImGui::TextUnformatted(fmt_mb(total).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(P.muted, "%s", e.trigger[0] ? e.trigger : "—");
                ImGui::TableSetColumnIndex(4);
                auto job = downloader.find(e.id);
                if (job && job->state == 0) {
                    unsigned long long d = job->done, t = std::max<unsigned long long>(1, job->total);
                    progress_bar((float) ((double) d / (double) t), ImVec2(80 * g_dpi, 0));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("✕")) job->cancel = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
                } else if (lora_installed(e)) {
                    ImGui::TextColored(P.ok, tr("● installée"));
                    if (lora_update_available(e)) {
                        ImGui::SameLine(0, 6 * g_dpi);
                        ImGui::BeginDisabled(studio->local_active());
                        if (soft_button(tr("Mise à jour ↓"), P.orange)) start_lora_update(e);
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("%s", studio->local_active() ? tr("Indisponible pendant une génération.") : tr("Une nouvelle version de cette LoRA est disponible : cliquer pour la télécharger et remplacer les fichiers."));
                        }
                    }
                } else if (job && job->state == 0) {
                    unsigned long long d = job->done, t = std::max<unsigned long long>(1, job->total);
                    progress_bar((float) ((double) d / (double) t), ImVec2(80 * g_dpi, 0));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("✕")) job->cancel = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
                } else {
                    bool failed = job && job->state == 2;
                    if (ImGui::SmallButton(failed ? tr("Réessayer ↓") : tr("Télécharger ↓"))) start_lora_download(e);
                    if (failed && ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Échec : %s"), job->error.c_str());
                }
                ImGui::TableSetColumnIndex(5);
                if (lora_installed(e) && delete_button(tr("Supprimer les fichiers de cette LoRA du disque"))) {
                    pending_lora_delete.clear();
                    for (int i = 0; i < e.file_count; i++) pending_lora_delete.push_back(e.files[i].file);
                    pending_lora_name = tr(e.name);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        // Mes LoRA (1.0.24) : LoRA personnelles (créées avec la Forge ou déposées à la main), en tableau comme le catalogue.
        int custom = 0;
        for (const auto & [file, size] : lora_files) custom += !lora_ref(file).entry;
        if (custom > 0) {
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
            ImGui::PushFont(g_bold);
            ImGui::TextColored(P.pink, "%s", tr("Mes LoRA"));
            ImGui::PopFont();
            muted_wrapped(tr("LoRA personnelles : créées avec la Forge ou déposées à la main dans modeles\\lora. Survolez un nom pour voir sa description. Un fichier .json du même nom, posé à côté, peut fournir le nom, la description et le mot déclencheur."));
            float rows = std::min(custom, 6) + 1.4f;
            if (ImGui::BeginTable("##lora_custom", 6, tf, ImVec2(0, rows * ImGui::GetFrameHeightWithSpacing()))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(tr("LoRA"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(tr("Type"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
                ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 66 * g_dpi);
                ImGui::TableSetupColumn(tr("Mot"), ImGuiTableColumnFlags_WidthFixed, 124 * g_dpi);
                ImGui::TableSetupColumn(tr("Détails"), ImGuiTableColumnFlags_WidthFixed, 210 * g_dpi);
                ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
                ImGui::TableHeadersRow();
                for (const auto & [file, size] : lora_files) {
                    if (lora_ref(file).entry) continue;
                    const CustomLora * c = custom_lora(file);
                    ImGui::PushID(file.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(custom_label(file).c_str());
                    if (ImGui::IsItemHovered()) {
                        std::string tip = c ? custom_description(*c) : std::string(tr("LoRA personnelle déposée dans modeles\\lora."));
                        if (c && !c->author.empty()) tip += tr("\nAuteur : ") + c->author;
                        if (c && !c->tags.empty()) tip += tr("\nBalises : ") + c->tags;
                        if (c && (c->songs || c->clips)) tip += trf("\nMorceaux : %d · extraits : %d", c->songs, c->clips);
                        if (c && !c->date.empty()) tip += tr("\nDate : ") + c->date;
                        if (c && !c->sheet_trigger.empty())
                            tip += trf("\nMot « %s » de la fiche ignoré : la LoRA a appris « %s » (il faut la réentraîner pour changer de mot).",
                                       c->sheet_trigger.c_str(), c->trigger.c_str());
                        if (c && c->trigger.empty()) tip += tr("\nSans mot déclencheur : la LoRA est quand même appliquée à la génération.");
                        tip += tr("\nFichier : ") + file;
                        ImGui::SetTooltip("%s", tip.c_str());
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(P.muted, "%s", custom_kind(c));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(fmt_mb(size).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextColored(c && !c->sheet_trigger.empty() ? P.warn : P.muted, "%s", c && !c->trigger.empty() ? c->trigger.c_str() : "—");
                    ImGui::TableSetColumnIndex(4);
                    std::string details = c ? c->details : std::string();
                    if (c && c->forge) details = details.empty() ? std::string(tr("Forge")) : std::string(tr("Forge")) + " · " + details;
                    ImGui::TextColored(P.muted, "%s", details.empty() ? "—" : details.c_str());
                    if (!details.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", details.c_str());
                    ImGui::TableSetColumnIndex(5);
                    if (delete_button(tr("Supprimer ce fichier du disque"))) {
                        std::string stem = file.size() > 12 ? file.substr(0, file.size() - 12) : file;  // sans « .safetensors »
                        pending_lora_delete = { file, stem + ".json" };  // la fiche .json part avec la LoRA (absente : ignorée)
                        pending_lora_name   = custom_label(file);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        path_line(studio->paths().adapters);  // 1.0.42 : toujours <programme>\modeles\lora
        if (ImGui::SmallButton(tr("Ouvrir le dossier des LoRA"))) {
            std::error_code ec;
            fs::create_directories(studio->paths().adapters, ec);
            ShellExecuteW(nullptr, L"open", studio->paths().adapters.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        ImGui::TextColored(P.muted, tr("Une LoRA déposée à la main est reconnue automatiquement."));
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    }

    // Ouvre la page d'aide (dossier « aide » à côté de l'application) dans le navigateur par défaut.
    void open_help() {
        // Page d'aide de la langue courante (aide.<code>.html), sinon l'anglais, sinon le français (aide.html)
        fs::path     dir = app_resource_dir(L"aide");  // ressources\aide (1.0.30), sinon aide
        fs::path     page = dir / "aide.html";
        std::error_code ec;
        if (i18n::current() != "fr") {
            for (const std::string & code : { i18n::current(), std::string("en") }) {
                fs::path candidate = dir / ("aide." + code + ".html");
                if (fs::exists(candidate, ec)) {
                    page = candidate;
                    break;
                }
            }
        }
        if (!fs::exists(page, ec)) {
            notify(tr("Page d'aide introuvable : ") + narrow(page.wstring()));
            return;
        }
        ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void draw_whisper_settings() {
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.teal, tr("Transcription des paroles"));
        ImGui::PopFont();
        muted_wrapped(tr("Whisper (OpenAI, licence MIT, moteur whisper.cpp) écrit les paroles d'un fichier audio chanté et en détecte la langue. Modèle « medium » quantifié (environ 540 Mo), exécuté par la carte graphique : environ 35 secondes pour un morceau de 5 minutes. La transcription fait partie de l'analyse d'un fichier de reprise ; sur de la musique elle n'est pas parfaite, relisez les paroles avant de générer."));
        refresh_whisper();
        auto wjob = downloader.find("whisper");
        ImGui::AlignTextToFramePadding();
        if (wjob && wjob->state == 0) {
            unsigned long long dd = wjob->done, tt = std::max<unsigned long long>(1, wjob->total);
            progress_bar((float) ((double) dd / (double) tt), ImVec2(200 * g_dpi, 0));
            ImGui::SameLine();
            if (ImGui::SmallButton("✕##wh_cancel")) wjob->cancel = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
        } else if (whisper_present) {
            ImGui::TextColored(P.ok, tr("● installé"));
            ImGui::SameLine(0, 8 * g_dpi);
            ImGui::TextColored(P.muted, "%s", fmt_mb(whisper_local_size()).c_str());
            ImGui::SameLine(0, 12 * g_dpi);
            if (whisper_update_available()) {
                if (soft_button(tr("Mise à jour ↓"), P.orange)) start_whisper_download(true);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trf("Nouvelle version disponible (%s, installée : %s) : cliquer pour la télécharger et remplacer le fichier.",
                                                                        fmt_mb(remote->get(whisper_file().name)).c_str(), fmt_mb(whisper_local_size()).c_str()).c_str());
            } else if (remote->state == 1) {
                ImGui::TextColored(P.muted, "%s", tr("vérification…"));
            } else if (remote->state == 2 && remote->get(whisper_file().name) > 0) {
                ImGui::TextColored(P.ok, "%s", tr("● à jour"));
            }
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::BeginDisabled(remote->state == 1);
            if (ImGui::SmallButton(tr("Vérifier les mises à jour"))) {
                std::vector<std::pair<std::string, std::string>> items = { { whisper_file().name, whisper_file().url } };
                std::thread(check_remote_sizes, remote, items).detach();
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 12 * g_dpi);
            if (delete_button(tr("Supprimer le modèle de transcription du disque"))) pending_whisper_delete = true;
        } else {
            if (!fs::exists(studio->paths().whisper)) {
                ImGui::TextColored(P.warn, "%s", tr("Moteur de transcription (whisper-cli.exe) absent."));
            } else {
                if (accent_button(tr("Télécharger le modèle de transcription"), ImVec2(0, 0), &P.teal)) start_whisper_download(false);
                ImGui::SameLine();
                ImGui::TextColored(P.muted, "%s", trf("environ %s", fmt_mb(whisper_file().bytes).c_str()).c_str());
            }
            if (wjob && wjob->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", wjob->error.c_str()).c_str());
        }
        if (whisper_present) {
            if (lang_running) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(P.muted, "%s", trf("Vérification en cours : %d / %d", (int) lang_checked, (int) lang_total).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Arrêter"))) lang_stop = true;
            } else {
                std::string reason  = busy_reason();
                int         pending = language_check_pending();
                ImGui::BeginDisabled(!reason.empty() || pending == 0);
                if (ImGui::Button(tr("Vérifier la langue des morceaux"))) start_language_check();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", !reason.empty() ? trf("Indisponible : %s.", reason.c_str()).c_str()
                                                       : pending == 0 ? tr("Tous les morceaux chantés ont déjà été vérifiés.")
                                                       : trf("Whisper écoute trois extraits de chaque morceau non encore vérifié (%d) : il corrige la langue des paroles des morceaux chantés si elle est manifestement différente, et signale les morceaux marqués instrumentaux où il entend des paroles. Une fois par morceau.", pending).c_str());
                }
                ImGui::SameLine();
                ImGui::TextColored(P.muted, "%s", trf("%d à vérifier", pending).c_str());
            }
        }
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    }

    void draw_analysis_settings() {
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.pink, tr("Analyse audio"));
        ImGui::PopFont();
        muted_wrapped(tr("Quand vous choisissez un fichier audio pour une reprise, ce modèle d'étiquetage (Discogs-EffNet et ses classificateurs, Essentia / Music Technology Group, licence CC BY-NC-SA 4.0, usage non commercial) détecte genre, ambiance, instruments et type de voix ; la partition (moteur de transcription) donne tempo, tonalité, tessiture et structure. Le tout propose des balises de style et la structure des paroles."));
        refresh_analysis();
        auto ajob = downloader.find("analysis");
        ImGui::AlignTextToFramePadding();
        if (ajob && ajob->state == 0) {
            unsigned long long dd = ajob->done, tt = std::max<unsigned long long>(1, ajob->total);
            progress_bar((float) ((double) dd / (double) tt), ImVec2(200 * g_dpi, 0));
            ImGui::SameLine();
            if (ImGui::SmallButton("✕##an_cancel")) ajob->cancel = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
        } else if (analysis_present) {
            ImGui::TextColored(P.ok, tr("● installé"));
            ImGui::SameLine(0, 8 * g_dpi);
            ImGui::TextColored(P.muted, "%s", fmt_mb(analysis_total_bytes()).c_str());
            int upd = analysis_update_count();
            ImGui::SameLine(0, 12 * g_dpi);
            if (upd > 0) {
                if (soft_button(tr("Mise à jour ↓"), P.orange)) start_analysis_download(true);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trf("%d fichier(s) du modèle ont une nouvelle version : cliquer pour les télécharger.", upd).c_str());
            } else if (remote->state == 1) {
                ImGui::TextColored(P.muted, "%s", tr("vérification…"));
            } else if (remote->state == 2 && remote->get(analysis_files()[0].name) > 0) {
                ImGui::TextColored(P.ok, "%s", tr("● à jour"));
            }
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::BeginDisabled(remote->state == 1);
            if (ImGui::SmallButton(tr("Vérifier les mises à jour"))) check_analysis_updates();
            ImGui::EndDisabled();
            ImGui::SameLine(0, 12 * g_dpi);
            if (delete_button(tr("Supprimer le modèle d'analyse du disque"))) pending_analysis_delete = true;
        } else {
            if (accent_button(tr("Télécharger le modèle d'analyse"), ImVec2(0, 0), &P.pink)) start_analysis_download(false);
            ImGui::SameLine();
            ImGui::TextColored(P.muted, "%s", trf("environ %s", fmt_mb(analysis_total_bytes()).c_str()).c_str());
            if (ajob && ajob->state == 2) colored_wrapped(P.warn, trf("Téléchargement impossible : %s", ajob->error.c_str()).c_str());
        }
        if (ImGui::Checkbox(tr("Analyser automatiquement le fichier choisi pour une reprise"), &settings.analysis_auto)) save_settings(settings, settings_path);
        if (ImGui::Checkbox(tr("Chercher les paroles sur Internet pour corriger la transcription"), &settings.lyrics_web)) save_settings(settings, settings_path);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Envoie le groupe et le titre du morceau (étiquettes ou nom du fichier) à lrclib.net, une base de paroles libre et gratuite. Sans connexion, la transcription Whisper est gardée."));
        }
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
    }

    // Bloc de réglages : cadre teinté, barre de la couleur du bloc à gauche (comme les cartes du formulaire).
    void begin_card(const ImVec4 & color, const char * id) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, tint(P.card, color, 0.07f));
        ImGui::PushStyleColor(ImGuiCol_Border, alpha(color, 0.45f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * g_dpi, 10 * g_dpi));
        ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        card_color = color;
    }
    void end_card() {
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp, wp + ImVec2(5 * g_dpi, ws.y), ImGui::GetColorU32(card_color), ImGui::GetStyle().ChildRounding, ImDrawFlags_RoundCornersLeft);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
    }
    ImVec4 card_color;

    void draw_settings_modal() {
        if (open_settings) {
            ImGui::OpenPopup(tr("Réglages"));
            open_settings = false;
            models        = studio->paths().list_models();  // modèles ajoutés depuis le dernier affichage
        }
        {   // taille par défaut : 1100 px de large (au moins 1024 si l'écran le permet) ; redimensionnable, le contenu s'adapte
            ImVec2 vp = ImGui::GetMainViewport()->Size;
            ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(640 * g_dpi, vp.x - 20 * g_dpi), std::min(420 * g_dpi, vp.y - 20 * g_dpi)),
                                                ImVec2(vp.x - 20 * g_dpi, vp.y - 20 * g_dpi));
            ImGui::SetNextWindowSize(ImVec2(std::min(1100 * g_dpi, vp.x - 40 * g_dpi), std::min(820 * g_dpi, vp.y - 60 * g_dpi)), ImGuiCond_Appearing);
        }
        if (!ImGui::BeginPopupModal(tr("Réglages"), nullptr, ImGuiWindowFlags_NoCollapse)) return;
        settings_shown = true;

        static int cat = 0;
        if (open_settings_cat >= 0) {  // bouton « Réglages › IA… » de l'onglet Composer avec l'IA
            cat               = open_settings_cat;
            open_settings_cat = -1;
        }
        {   // navigation par catégories : colonne à gauche, ou rangée en haut quand la fenêtre est étroite
            struct Cat { const char * label; const ImVec4 * color; };
            const Cat cats[] = { { N_("Interface"), &P.accent }, { N_("Génération"), &P.teal }, { N_("Analyse et paroles"), &P.pink },
                                 { N_("LoRA"), &P.violet }, { N_("Système"), &P.blue }, { N_("Bibliothèque"), &P.danger },
                                 { N_("IA (paroles)"), &P.amber } };
            const float foot   = ImGui::GetFrameHeight() + 12 * g_dpi;
            const bool  narrow = ImGui::GetContentRegionAvail().x < 820 * g_dpi;
            if (narrow) {
                for (int i = 0; i < IM_ARRAYSIZE(cats); i++) {
                    if (i > 0) {
                        float w = ImGui::CalcTextSize(tr(cats[i].label)).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
                        if (ImGui::GetCursorPosX() + w < ImGui::GetContentRegionMax().x) ImGui::SameLine();
                    }
                    ImGui::PushID(i);
                    if (cat == i ? accent_button(tr(cats[i].label), ImVec2(0, 0), cats[i].color) : ImGui::Button(tr(cats[i].label))) cat = i;
                    ImGui::PopID();
                }
                ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            } else {
                ImGui::BeginChild("##set_nav", ImVec2(200 * g_dpi, -foot), ImGuiChildFlags_Borders);
                ImDrawList * dl = ImGui::GetWindowDrawList();
                for (int i = 0; i < IM_ARRAYSIZE(cats); i++) {
                    const ImVec4 & c   = *cats[i].color;
                    ImVec2         p0  = ImGui::GetCursorScreenPos();
                    float          w   = ImGui::GetContentRegionAvail().x, h = 34 * g_dpi;
                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Header, alpha(c, 0.22f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, alpha(c, 0.14f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, alpha(c, 0.30f));
                    if (ImGui::Selectable("##nav", cat == i, 0, ImVec2(w, h))) cat = i;
                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                    if (cat == i) dl->AddRect(p0, p0 + ImVec2(w, h), ImGui::GetColorU32(alpha(c, 0.6f)), 4 * g_dpi);
                    dl->AddRectFilled(p0 + ImVec2(0, 6 * g_dpi), p0 + ImVec2(4 * g_dpi, h - 6 * g_dpi), ImGui::GetColorU32(c), 2 * g_dpi);
                    const char * label = tr(cats[i].label);
                    dl->AddText(ImVec2(p0.x + 14 * g_dpi, p0.y + (h - ImGui::GetTextLineHeight()) / 2), ImGui::GetColorU32(cat == i ? c : P.text), label);
                    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
                }
                ImGui::EndChild();
                ImGui::SameLine();
            }
            ImGui::BeginChild("##set_content", ImVec2(0, -foot), 0);
        if (test_scroll_frames > 0) {
            ImGui::SetScrollY(test_scroll);
            test_scroll_frames--;
        }
        }

        if (cat == 0) {
        begin_card(P.accent, "c_lang");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.accent, "%s", tr("Langue de l'application"));
        ImGui::PopFont();
        {
            std::string shown = i18n::current();
            for (const i18n::Language & l : i18n::languages())
                if (l.code == i18n::current()) shown = l.name;
            ImGui::SetNextItemWidth(260 * g_dpi);
            if (ImGui::BeginCombo("##ui_language", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (const i18n::Language & l : i18n::languages()) {
                    if (ImGui::Selectable(l.name.c_str(), l.code == i18n::current())) pending_language = l.code;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Ouvrir le dossier des langues"))) {
                std::error_code ec;
                fs::create_directories(app_resource_dir(L"langues"), ec);
                ShellExecuteW(nullptr, L"open", app_resource_dir(L"langues").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            muted_wrapped(tr("Chaque langue est un fichier du dossier « langues » : pour corriger une traduction ou en ajouter une, modifiez ou copiez un de ces fichiers (voir _modele.json). Une langue ajoutée apparaît ici au prochain démarrage."));
        }
        end_card();
        }

        if (cat == 0) {
        begin_card(P.accent, "c_theme");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.accent, tr("Thème graphique"));
        ImGui::PopFont();
        ImGui::SetNextItemWidth(260 * g_dpi);
        if (ImGui::BeginCombo("##theme", tr(THEME_NAMES[settings.theme]))) {
            for (int i = 0; i < THEME_COUNT; i++) {
                if (ImGui::Selectable(tr(THEME_NAMES[i]), settings.theme == i)) {
                    settings.theme = i;
                    g_theme        = i;
                    apply_theme(g_system_light, g_dpi);
                    BOOL dark_title = theme_is_dark(g_theme, g_system_light);
                    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_title, sizeof(dark_title));
                    save_settings(settings, settings_path);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(THEME_HINTS[i]));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        {   // aperçu de la palette du thème actif
            ImDrawList * dl = ImGui::GetWindowDrawList();
            ImVec2       p0 = ImGui::GetCursorScreenPos();
            const ImVec4 sw[] = { P.bg, P.panel, P.card, P.accent, P.ok, P.warn, P.danger, P.violet, P.pink };
            float        sz = ImGui::GetFrameHeight() * 0.75f, y = p0.y + (ImGui::GetFrameHeight() - sz) / 2;
            for (int i = 0; i < IM_ARRAYSIZE(sw); i++) {
                ImVec2 a(p0.x + i * (sz + 3 * g_dpi), y);
                dl->AddRectFilled(a, a + ImVec2(sz, sz), ImGui::GetColorU32(sw[i]), 3 * g_dpi);
                dl->AddRect(a, a + ImVec2(sz, sz), ImGui::GetColorU32(P.line), 3 * g_dpi);
            }
            ImGui::Dummy(ImVec2(IM_ARRAYSIZE(sw) * (sz + 3 * g_dpi), ImGui::GetFrameHeight()));
        }
        muted_wrapped(tr("Le thème par défaut suit le mode clair ou sombre de Windows ; les autres thèmes sont fixes. Le changement est immédiat."));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, tr("Marques des morceaux"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(340 * g_dpi);
        if (ImGui::Combo("##marques", &settings.mark_style, tr_list(MARK_STYLES, (int) IM_ARRAYSIZE(MARK_STYLES)).data(), IM_ARRAYSIZE(MARK_STYLES))) save_settings(settings, settings_path);
        {   // aperçu des trois états dans le style choisi
            ImGui::TextColored(P.muted, tr("Aperçu :"));
            const struct { const char * label; bool v, f; } states[] = { { tr("à écouter"), false, false }, { tr("validé"), true, false }, { tr("à problème"), false, true } };
            for (auto & st : states) {
                ImGui::SameLine(0, 12 * g_dpi);
                ImGui::TextColored(P.muted, "%s", st.label);
                ImGui::SameLine(0, 6 * g_dpi);
                ImGui::PushID(st.label);
                draw_marks(settings.mark_style, st.v, st.f, 22 * g_dpi);
                ImGui::PopID();
            }
        }
        end_card();
        }

        if (cat == 0) {
        begin_card(P.accent, "c_notif");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.accent, tr("Notifications"));
        ImGui::PopFont();
        if (ImGui::Checkbox(tr("Pastilles à côté de l'horloge"), &settings.notifications)) save_settings(settings, settings_path);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, "%s", tr("Compteurs de l'en-tête"));
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Sport (grands cadrans)"), settings.gauge_style == 0)) { settings.gauge_style = 0; save_settings(settings, settings_path); }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Compact (plus de place pour la bibliothèque)"), settings.gauge_style == 1)) { settings.gauge_style = 1; save_settings(settings, settings_path); }
        if (ImGui::Checkbox(tr("Application réduite : jauges et compteurs à côté de l'horloge"), &settings.tray_gauges)) save_settings(settings, settings_path);
        muted_wrapped(tr("Quand la fenêtre est réduite, deux icônes s'ajoutent près de l'horloge : trois barres (CPU, GPU, mémoire GPU) et deux nombres (morceaux en attente en violet, terminés en vert). Le survol donne les valeurs exactes. Sous Windows 11, épinglez-les via Paramètres › Personnalisation › Barre des tâches › Autres icônes de la barre d'état système si elles restent cachées dans le menu ^."));
        end_card();
        }

        if (cat == 4) {
        begin_card(P.orange, "c_games");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.orange, tr("Jeux"));
        ImGui::PopFont();
        if (ImGui::Checkbox(tr("Suspendre la file de génération quand un jeu est lancé"), &settings.game_pause)) {
            save_settings(settings, settings_path);
        }
        muted_wrapped(tr("La génération en cours est arrêtée (elle repartira du début) et la file reprend 10 secondes après la fermeture du jeu. Les jeux en plein écran sont détectés automatiquement ; indiquez ci-dessous les autres programmes à surveiller (noms de fichiers séparés par des virgules)."));
        if (input_line("##games", "cs2.exe, VRising.exe", settings.game_list)) save_settings(settings, settings_path);
        if (!settings.game_pause) ImGui::TextColored(P.muted, tr("Détection désactivée."));
        else if (game_active) ImGui::TextColored(P.orange, tr("● Jeu détecté : %s — file suspendue"), game_name.c_str());
        else ImGui::TextColored(P.ok, tr("● Aucun jeu détecté — la file peut tourner"));
        end_card();
        }

        if (cat == 1) {
        begin_card(P.teal, "c_model");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.teal, tr("Modèle de génération"));
        ImGui::PopFont();
        draw_model_combo("##modele_defaut", 260 * g_dpi, true);
        muted_wrapped(tr("Modèle par défaut au démarrage. Aux essais, les trois quantifications vont aussi vite l'une que l'autre ; Q6_K et Q5_K_M économisent seulement 0,3 à 0,4 Go de mémoire vidéo au pic. Le choix rapide de la barre du haut s'applique aux prochains morceaux ; chaque morceau retient le modèle qui l'a produit."));
        {
            struct Quant { const char * file; const char * label; const char * size; const char * note; };
            static const Quant quants[] = {
                { "YuE2-3B-Q8_0.gguf", "Q8_0", N_("3,81 Go"), N_("qualité maximale (presque sans perte)") },
                { "YuE2-3B-Q6_K.gguf", "Q6_K", N_("2,94 Go"), N_("recommandé : qualité jugée équivalente, plus léger") },
                { "YuE2-3B-Q5_K_M.gguf", "Q5_K_M", N_("2,62 Go"), N_("le plus léger") },
            };
            ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##quants", 5, tf)) {
                ImGui::TableSetupColumn(tr("Modèle"), ImGuiTableColumnFlags_WidthFixed, 80 * g_dpi);
                ImGui::TableSetupColumn(tr("Taille"), ImGuiTableColumnFlags_WidthFixed, 70 * g_dpi);
                ImGui::TableSetupColumn(tr("Remarque"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(tr("Téléchargement"), ImGuiTableColumnFlags_WidthFixed, 200 * g_dpi);
                ImGui::TableSetupColumn(tr("Supprimer"), ImGuiTableColumnFlags_WidthFixed, 84 * g_dpi);
                ImGui::TableHeadersRow();
                for (const Quant & q : quants) {
                    bool installed = std::find(models.begin(), models.end(), q.file) != models.end();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(q.label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(tr(q.size));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(P.muted, "%s", tr(q.note));
                    ImGui::TableSetColumnIndex(3);
                    auto mjob = downloader.find(std::string("model:") + q.file);
                    if (mjob && mjob->state == 0) {
                        unsigned long long dd = mjob->done, tt = std::max<unsigned long long>(1, mjob->total);
                        progress_bar((float) ((double) dd / (double) tt), ImVec2(80 * g_dpi, 0));
                        ImGui::SameLine();
                        ImGui::PushID(q.file);
                        if (ImGui::SmallButton("✕")) mjob->cancel = true;
                        ImGui::PopID();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Annuler le téléchargement"));
                    } else if (installed) {
                        ImGui::TextColored(P.ok, tr("● installé"));
                        if (model_update_available(q.file)) {
                            ImGui::SameLine(0, 6 * g_dpi);
                            ImGui::PushID(q.file);
                            ImGui::BeginDisabled(studio->local_active());
                            if (soft_button(tr("Mise à jour ↓"), P.orange)) start_model_update(q.file);
                            ImGui::EndDisabled();
                            ImGui::PopID();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                ImGui::SetTooltip("%s", studio->local_active() ? tr("Indisponible pendant une génération.")
                                                                               : trf("Nouvelle version disponible (%s, installée : %s) : cliquer pour la télécharger et remplacer le fichier.",
                                                                                     fmt_mb(remote->get(q.file)).c_str(), fmt_mb(model_local_size(q.file)).c_str()).c_str());
                            }
                        }
                    } else {
                        ImGui::PushID(q.file);
                        if (ImGui::SmallButton(tr("Télécharger ↗"))) {
                            std::wstring url = L"https://huggingface.co/Serveurperso/YuE2-GGUF/resolve/main/" + widen(q.file);
                            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                        ImGui::PopID();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(tr("Ouvre le téléchargement dans le navigateur (Hugging Face).\nPlacez ensuite le fichier dans :\n%s"), narrow(studio->paths().gguf_dir.wstring()).c_str());
                        }
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (installed) {
                        ImGui::PushID(q.file);
                        if (delete_button(tr("Supprimer ce modèle du disque"))) pending_model_delete = q.file;
                        ImGui::PopID();
                    }
                }
                // modèles déposés à la main, hors de cette liste
                for (const std::string & m : models) {
                    bool known = false;
                    for (const Quant & q : quants) known = known || m == q.file;
                    if (known) continue;
                    std::error_code ec;
                    auto            size = fs::file_size(studio->paths().gguf_dir / widen(m), ec);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(model_label(m).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(ec ? "" : fmt_mb(size).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(P.muted, tr("ajouté à la main"));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextColored(P.ok, tr("● installé"));
                    ImGui::TableSetColumnIndex(4);
                    ImGui::PushID(m.c_str());
                    if (delete_button(tr("Supprimer ce modèle du disque"))) pending_model_delete = m;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            path_line(studio->paths().gguf_dir);  // 1.0.42 : toujours <programme>\modeles\musique
            if (ImGui::SmallButton(tr("Ouvrir le dossier des modèles"))) {
                ShellExecuteW(nullptr, L"open", studio->paths().gguf_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
            ImGui::TextColored(P.muted, tr("La liste se met à jour à chaque ouverture des Réglages."));
        }
        end_card();
        }

        if (cat == 1) {
        begin_card(P.orange, "c_end");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.orange, tr("Fin des morceaux"));
        ImGui::PopFont();
        ImGui::SetNextItemWidth(260 * g_dpi);
        if (ImGui::SliderInt("##fondu", &settings.fade_seconds, 1, 30, tr("fondu de %d s"))) studio->set_fade_seconds(settings.fade_seconds);
        if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(settings, settings_path);
        muted_wrapped(tr("Longueur du fondu appliqué quand un morceau atteint le plafond de durée sans conclure (instrumentaux, durée fixe). Dans le formulaire, « Fin en fondu sur la structure » remplace ce réglage par la durée de la dernière section, avec une courbe plus douce."));
        end_card();
        }

        if (cat == 1) {
            begin_card(P.green, "c_packs");
            draw_packs_settings();
            end_card();
        }

        if (cat == 1) {
            begin_card(P.warn, "c_negative");
            draw_negative_tags_settings();
            end_card();
        }

        if (cat == 1) {
        begin_card(P.accent, "c_solver");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.accent, tr("Solveur de la phase audio (NAR)"));
        ImGui::PopFont();
        draw_solver_combo("##solveur_defaut", 260 * g_dpi, true);
        muted_wrapped(tr("Solveur par défaut au démarrage : méthode de calcul et nombre de pas de la phase audio, qui domine le temps de génération. Moins il y a d'évaluations du réseau, plus c'est rapide. Le nom donne la méthode puis le nombre de pas (AB3 24 = Adams-Bashforth d'ordre 3, 24 pas ; MP 32 = midpoint, 32 pas, la référence). Le choix rapide de la barre du haut s'applique aux prochains morceaux ; chaque morceau retient son solveur."));
        if (ImGui::BeginTable("##solveurs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("Solveur"), ImGuiTableColumnFlags_WidthFixed, 90 * g_dpi);
            ImGui::TableSetupColumn(tr("Éval."), ImGuiTableColumnFlags_WidthFixed, 50 * g_dpi);
            ImGui::TableSetupColumn(tr("Vitesse"), ImGuiTableColumnFlags_WidthFixed, 80 * g_dpi);
            ImGui::TableSetupColumn(tr("Remarque"));
            ImGui::TableHeadersRow();
            for (const SolverSpec & sp : solver_specs()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sp.label);
                ImGui::TableNextColumn();
                ImGui::Text("%d", sp.evals);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f ×", 64.0 / sp.evals);
                ImGui::TableNextColumn();
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(P.muted, "%s", tr(sp.note));
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTable();
        }
        end_card();
        }

        if (cat == 2) {
            begin_card(P.pink, "c_analysis");
            draw_analysis_settings();
            end_card();
            begin_card(P.teal, "c_whisper");
            draw_whisper_settings();
            end_card();
            begin_card(P.violet, "c_translate");
            draw_translate_settings();
            end_card();
        }
        if (cat == 3) {
            begin_card(P.pink, "c_lora");
            draw_lora_catalog();
            end_card();
        }

        if (cat == 4) {
        begin_card(P.blue, "c_gpu");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.blue, tr("Version"));
        ImGui::PopFont();
        draw_update_settings();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.blue, tr("Recommandations pour ce PC"));
        ImGui::PopFont();
        draw_advice();
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.blue, tr("Carte graphique"));
        ImGui::PopFont();
        draw_gpu_profile();
        draw_gpu_limit();
        ImGui::TextColored(P.muted, tr("Pilote : %s"), driver_version.empty() ? "inconnu" : driver_version.c_str());
        if (driver_update->state == 3) {  // pilote AMD plus récent trouvé au démarrage
            ImGui::SameLine(0, 12 * g_dpi);
            if (soft_button(tr("Mise à jour disponible"), P.orange)) {
                ShellExecuteW(nullptr, L"open", L"https://www.amd.com/en/support/download/drivers.html", nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", trf("AMD Software installé : %s · dernière version stable : %s\nOuvre la page de téléchargement des pilotes AMD.",
                                            driver_update->installed.c_str(), driver_update->latest.c_str()).c_str());
            }
        } else if (driver_update->state == 2) {
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(P.ok, "%s", tr("● pilote à jour"));
        }
        if (gpu_profile().gpu.vendor == GpuVendor::Nvidia) {  // pas de recherche automatique : lien vers la page Nvidia
            ImGui::SameLine(0, 12 * g_dpi);
            if (soft_button(tr("Pilotes Nvidia"), P.teal)) {
                ShellExecuteW(nullptr, L"open", L"https://www.nvidia.com/fr-fr/drivers/", nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ouvre la page de téléchargement des pilotes Nvidia."));
        }
        if (driver_changed) {
            colored_wrapped(P.warn, tr("Le pilote a changé depuis le dernier test : lancez le test de santé."));
        }
        HealthResult h = studio->health();
        ImVec4       hc = h.state == 2 ? P.ok : h.state == 3 ? P.danger : h.state == 4 ? P.warn : P.muted;
        if (h.state == 0) muted_wrapped(tr("Le test rejoue deux fois un court extrait et compare les résultats : il détecte une carte graphique ou un pilote qui produirait des morceaux saturés ou parasités."));
        else {
            ImGui::PushStyleColor(ImGuiCol_Text, hc);
            ImGui::TextWrapped("%s", h.message.c_str());
            ImGui::PopStyleColor();
        }
        bool busy = h.state == 1 || studio->local_active();
        ImGui::BeginDisabled(busy);
        if (accent_button(h.state == 1 ? tr("Test en cours…") : tr("Lancer le test de santé (≈ 1 min)"), ImVec2(0, 0), &P.blue)) {
            studio->start_health_check();
        }
        ImGui::EndDisabled();
        if (studio->local_active() && h.state != 1) {
            ImGui::SameLine();
            ImGui::TextColored(P.muted, tr("Indisponible pendant une génération."));
        }
        end_card();
        }

        if (cat == 5) {
        begin_card(P.danger, "c_library");
        ImGui::PushFont(g_bold);
        ImGui::TextColored(P.danger, tr("Bibliothèque"));
        ImGui::PopFont();
        {
            const int total = (int) studio->snapshot().size();
            muted_wrapped(tr("Supprime tous les morceaux générés (audio, fiches, journaux). Ils sont envoyés à la corbeille de Windows : vous pourrez les y restaurer. Les images d'albums et les vidéos ne sont pas touchées."));
            ImGui::BeginDisabled(total == 0 || studio->local_active());
            if (accent_button(tr("Supprimer tous les morceaux…"), ImVec2(0, 0), &P.danger)) pending_delete_all = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(P.muted, total > 1 ? tr("%d morceaux dans la bibliothèque") : tr("%d morceau dans la bibliothèque"), total);
        }
        end_card();
        }
        if (cat == 6) draw_ai_settings();
        ImGui::EndChild();  // contenu
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));
        if (ImGui::Button(tr("Fermer"))) ImGui::CloseCurrentPopup();

        // Confirmations (fenêtres au-dessus des Réglages)
        if (!pending_model_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer le modèle ?"))) ImGui::OpenPopup(tr("Supprimer le modèle ?"));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer le modèle ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(tr("Le fichier « %s » sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau ; les morceaux déjà générés ne sont pas touchés."), pending_model_delete.c_str());
            path_line(studio->paths().gguf_dir / widen(pending_model_delete));
            if (models.size() <= 1) {
                colored_wrapped(P.warn, tr("C'est le seul modèle installé : il faudra en télécharger un pour générer."));
            }
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_model_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                delete_model_file(pending_model_delete);
                pending_model_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (pending_analysis_delete && !ImGui::IsPopupOpen(tr("Supprimer le modèle d'analyse ?"))) ImGui::OpenPopup(tr("Supprimer le modèle d'analyse ?"));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer le modèle d'analyse ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", tr("Les fichiers du modèle d'analyse audio seront supprimés définitivement du disque. Vous pourrez les télécharger de nouveau ; les morceaux déjà générés ne sont pas touchés."));
            path_line(analysis_dir());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_analysis_delete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                delete_analysis_files();
                pending_analysis_delete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (pending_whisper_delete && !ImGui::IsPopupOpen(tr("Supprimer le modèle de transcription ?"))) ImGui::OpenPopup(tr("Supprimer le modèle de transcription ?"));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer le modèle de transcription ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", tr("Le fichier du modèle de transcription des paroles sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau ; les morceaux déjà générés ne sont pas touchés."));
            path_line(whisper_dir() / widen(whisper_file().name));
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_whisper_delete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                delete_whisper_file();
                pending_whisper_delete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (pending_translate_delete && !ImGui::IsPopupOpen(tr("Supprimer le modèle de traduction ?"))) ImGui::OpenPopup(tr("Supprimer le modèle de traduction ?"));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer le modèle de traduction ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", tr("Le modèle de traduction local sera supprimé définitivement du disque. Vous pourrez le télécharger de nouveau ; la traduction par Internet reste disponible."));
            path_line(translate_dir() / widen(translate_model_file().name));
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_translate_delete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                std::error_code ec;
                remove_inside(translate_dir() / widen(translate_model_file().name), ec);
                refresh_translator(true);
                if (!remove_refused) notify(tr("Modèle de traduction supprimé."));
                pending_translate_delete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!pending_lora_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer la LoRA ?"))) ImGui::OpenPopup(tr("Supprimer la LoRA ?"));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer la LoRA ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(tr("La LoRA « %s » sera supprimée définitivement du disque. Vous pourrez la télécharger de nouveau (si elle est dans le catalogue) ; les morceaux déjà générés ne sont pas touchés."), pending_lora_name.c_str());
            for (const std::string & f : pending_lora_delete) {
                std::error_code ec;
                if (fs::exists(studio->paths().adapters / widen(f), ec)) path_line(studio->paths().adapters / widen(f));
            }
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_lora_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Supprimer"), ImVec2(0, 0), &P.danger)) {
                delete_lora_files(pending_lora_delete, pending_lora_name);
                pending_lora_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (pending_delete_all && !ImGui::IsPopupOpen(tr("Supprimer tous les morceaux ?"))) ImGui::OpenPopup(tr("Supprimer tous les morceaux ?"));
        ImGui::SetNextWindowSize(ImVec2(480 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer tous les morceaux ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(tr("Les %d morceaux de la bibliothèque seront envoyés à la corbeille de Windows. Vous pourrez les y restaurer tant que la corbeille n'est pas vidée."), (int) studio->snapshot().size());
            path_line(studio->paths().library);
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_delete_all = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Tout mettre à la corbeille"), ImVec2(0, 0), &P.danger)) {
                player.close();
                std::string err;
                int         n = studio->remove_all(err);
                if (n < 0) notify(err);
                else notify(trf("%d morceau(x) envoyé(s) à la corbeille.", n));
                pending_delete_all = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        draw_pack_modals();  // export, import, suppression d'un pack : au-dessus des Réglages
        draw_cover_splash();  // 1.0.37 : pochettes créées pendant que les Réglages sont ouverts
        ImGui::EndPopup();
    }

    // ------------------------------------------------------------------ Tri
    void sort_shown(std::vector<const Song *> & v) const {
        std::stable_sort(v.begin(), v.end(), [&](const Song * a, const Song * b) {
            if (a->active() != b->active()) return a->active();  // la file de travail reste en tête
            switch (settings.sort) {
                case 1: return a->created < b->created;
                case 2: return fold(a->title) < fold(b->title);
                case 3: {
                    int x = track_number(*a), y = track_number(*b);
                    if (x != y) return (x ? x : 100000) < (y ? y : 100000);
                    return fold(a->title) < fold(b->title);
                }
                case 4: return a->audio_seconds > b->audio_seconds;
                case 5:
                    if (a->validated != b->validated) return a->validated;
                    return a->created > b->created;
                default: return a->created > b->created;
            }
        });
    }

    void draw_sort_bar() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.muted, tr("Trier"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(230 * g_dpi);
        if (ImGui::Combo("##tri", &settings.sort, tr_list(SORTS, (int) IM_ARRAYSIZE(SORTS)).data(), IM_ARRAYSIZE(SORTS))) save_settings(settings, settings_path);
        ImGui::SameLine(0, 18 * g_dpi);
        ImGui::TextColored(P.muted, tr("Cartes"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150 * g_dpi);
        static const char * CARD_MODES[] = { N_("Détaillées"), N_("Compactes"), N_("Avec pochette"), N_("Pochette compacte") };
        if (ImGui::Combo("##cartes", &settings.card_mode, tr_list(CARD_MODES, (int) IM_ARRAYSIZE(CARD_MODES)).data(), IM_ARRAYSIZE(CARD_MODES))) save_settings(settings, settings_path);
        {   // la case passe à la ligne suivante quand le texte ne tient pas (langues à textes longs)
            float cb_w  = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(tr("Lecture continue")).x;
            float right = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
            if (ImGui::GetItemRectMax().x + 18 * g_dpi + cb_w <= right) ImGui::SameLine(0, 18 * g_dpi);
        }
        if (ImGui::Checkbox(tr("Lecture continue"), &settings.autoplay)) save_settings(settings, settings_path);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("À la fin d'un morceau, lit le suivant de la liste affichée (morceaux terminés).\nAvec le tri « N° de piste » et le filtre « Validés », on écoute un album dans l'ordre."));
        }
    }

    // ------------------------------------------------------------------ Fenêtres modales

    bool modal_buttons(const char * ok_label, bool ok_enabled, const ImVec4 * ok_color = nullptr) {
        ImGui::Dummy(ImVec2(0, 6 * g_dpi));
        if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!ok_enabled);
        bool ok = accent_button(ok_label, ImVec2(0, 0), ok_color) && ok_enabled;
        ImGui::EndDisabled();
        return ok;
    }

    void draw_modals() {
        draw_startup_issues();  // 1.0.81
        // Essai (1.0.51) : STUDIO_TEST_ALBUMS=<clé d'album>[|1] ouvre « Albums » avec cet album déplié (onglet Vidéos si « |1 »)
        static bool test_library = [this] {  // essai (1.0.62) : STUDIO_TEST_LIBRARY=<filtre> : bibliothèque filtrée, cartes dépliées une sur deux
            wchar_t v[256];
            DWORD   n = GetEnvironmentVariableW(L"STUDIO_TEST_LIBRARY", v, 256);
            if (n == 0 || n >= 256) return false;
            filter_text = narrow(v);
            int k = 0;
            for (const Song & sg : studio->snapshot())
                if (sg.album == filter_text && k++ % 2 == 0) expanded_cards.insert(sg.id);
            return true;
        }();
        (void) test_library;
        static bool test_albums = [this] {
            wchar_t v[512];
            DWORD   n = GetEnvironmentVariableW(L"STUDIO_TEST_ALBUMS", v, 512);
            if (n == 0 || n >= 512) return false;
            std::string s = narrow(v);
            // 1.0.57 : « |v » ouvre directement « Créer des vidéos » sur cet album, clip coché
            if (s.size() > 2 && s.compare(s.size() - 2, 2, "|v") == 0) {
                std::string key = s.substr(0, s.size() - 2), name;
                std::vector<Song> all = studio->snapshot();
                for (const Song & sg : all)
                    if (!sg.album.empty() && album_key(sg.artist, sg.album) == key) name = sg.album;
                open_video_dialog(album_songs(all, key, false), name, key);
                video_clip = true;
                video_short = true;  // 1.0.75 : liste des durées visible sur la capture
                return true;
            }
            if (s.rfind("qc:", 0) == 0) {  // 1.0.83 : contrôle qualité d'un album
                quality_test    = s.substr(3);
                quality_no_flag = true;  // essai : les pouces de l'utilisateur ne sont pas touchés
                open_albums  = true;
                return true;
            }
            if (s == "shorts") {  // 1.0.82 : fenêtre de confirmation des Shorts manquants de toute la bibliothèque
                shorts_test = true;
                open_albums = true;
                return true;
            }
            album_menu_test = s.size() > 2 && s.compare(s.size() - 2, 2, "|m") == 0;
            album_tab       = s.size() > 2 && s.compare(s.size() - 2, 2, "|1") == 0;
            album_tab_request = album_tab;
            album_open      = album_tab || album_menu_test ? s.substr(0, s.size() - 2) : s;
            open_albums   = true;
            album_scroll  = true;
            return true;
        }();
        (void) test_albums;
        // Essai (1.0.69, captures de l'aide) : STUDIO_TEST_SHOT="clé=valeur;…" : filter, cards, marks, style, gauge, theme,
        // reglages (catégorie), scroll, ia, open (blocs dépliés), fold (blocs repliés), reuse (premier morceau filtré), edit.
        static bool test_shot = [this] {
            wchar_t v[512];
            DWORD   n = GetEnvironmentVariableW(L"STUDIO_TEST_SHOT", v, 512);
            if (n == 0 || n >= 512) return false;
            std::istringstream in(narrow(v));
            std::string        item;
            auto first_song = [&]() {
                for (const Song & sg : studio->snapshot())
                    if (sg.status == "done" && (filter_text.empty() || sg.album == filter_text) && sg.title.find("partie") == std::string::npos) return sg;
                return Song{};
            };
            while (std::getline(in, item, ';')) {
                const size_t eq = item.find('=');
                const std::string key = item.substr(0, eq), val = eq == std::string::npos ? std::string() : item.substr(eq + 1);
                if (key == "filter") filter_text = val;
                else if (key == "cards") settings.card_mode = atoi(val.c_str());
                else if (key == "marks") settings.mark_style = atoi(val.c_str());
                else if (key == "style") settings.style_mode = atoi(val.c_str());
                else if (key == "gauge") settings.gauge_style = atoi(val.c_str());
                else if (key == "theme") {
                    settings.theme = g_theme = atoi(val.c_str());
                    apply_theme(g_system_light, g_dpi);
                }
                else if (key == "reglages") {
                    open_settings     = true;
                    open_settings_cat = atoi(val.c_str());
                }
                else if (key == "scroll") test_scroll = (float) atof(val.c_str()) * g_dpi, test_scroll_frames = 20;
                else if (key == "ia") form_tab_request = 1;
                else if (key == "open") g_collapsed.clear();
                else if (key == "fold") {
                    std::istringstream ids(val);
                    std::string        id;
                    while (std::getline(ids, id, ',')) g_collapsed.insert(id);
                }
                else if (key == "reuse") {
                    reuse(first_song());
                    form.style = test_clean_style(form.style);
                }
                else if (key == "edit") {
                    Song s      = first_song();
                    edit_id     = s.id;
                    edit_title  = s.title;
                    edit_artist = s.artist;
                    edit_album  = s.album;
                    edit_track  = s.track;
                    open_edit   = true;
                }
            }
            return true;
        }();
        (void) test_shot;
        draw_albums_modal();
        if (!albums_drawn) draw_video_modal();  // sinon déjà dessinée à l'intérieur de « Albums »
        settings_shown = false;
        draw_settings_modal();
        if (!settings_shown) draw_pack_modals();  // pack déposé sur la fenêtre, Réglages fermés
        // 1.0.37 : splash des pochettes au niveau principal, seulement si aucune autre fenêtre modale n'est ouverte
        // (sinon elle est dessinée depuis cette fenêtre-là : « Albums… », Réglages)
        if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::IsPopupOpen(tr("Création des pochettes"))) draw_cover_splash();
        draw_structure_modals();
        draw_ai_style_modal();  // 1.0.46 : « Style perso… » des cartes (sinon ouverte depuis l'onglet IA)
        // Supprimer un morceau
        if (!pending_delete.empty() && !ImGui::IsPopupOpen(tr("Supprimer le morceau ?"))) ImGui::OpenPopup(tr("Supprimer le morceau ?"));
        ImGui::SetNextWindowSize(ImVec2(420 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Supprimer le morceau ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(tr("Le morceau et ses fichiers (FLAC, WAV, MP3…) seront envoyés à la corbeille de Windows : vous pourrez les y récupérer."));
            path_line(song_folder(pending_delete));
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                pending_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Mettre à la corbeille"), ImVec2(0, 0), &P.danger)) {
                if (player.id() == pending_delete) player.close();
                std::string err;
                if (!studio->remove(pending_delete, err)) notify(err);
                pending_delete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Modifier un morceau de la bibliothèque
        if (open_edit) {
            ImGui::OpenPopup(tr("Modifier le morceau"));
            open_edit = false;
        }
        ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Modifier le morceau"), nullptr, ImGuiWindowFlags_NoResize)) {
            muted_wrapped(tr("Les étiquettes des fichiers FLAC et MP3 sont mises à jour (sans réencodage)."));
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            ImGui::TextColored(P.muted, tr("Titre"));
            input_line("##edit_title", tr("Titre"), edit_title);
            ImGui::TextColored(P.muted, tr("Groupe ou chanteur"));
            input_line("##edit_artist", tr("Groupe ou chanteur"), edit_artist);
            ImGui::TextColored(P.muted, tr("Album"));
            input_line("##edit_album", tr("Album"), edit_album);
            ImGui::TextColored(P.muted, tr("N° de piste"));
            input_line("##edit_track", tr("Ex. : 01"), edit_track, 160 * g_dpi);
            ImGui::Dummy(ImVec2(0, 2 * g_dpi));
            ImGui::TextColored(P.muted, tr("Pochette"));
            {
                std::string ckey = cover_key(edit_id, trim(edit_artist), trim(edit_album));
                draw_cover_art(ckey, edit_title, 76 * g_dpi);
                ImGui::SameLine(0, 12 * g_dpi);
                ImGui::BeginGroup();
                if (soft_button(tr("Choisir une image…"), P.violet)) set_image(ckey, wtr("Pochette du morceau").c_str());
                if (!cover_for(ckey).empty()) {
                    ImGui::SameLine();
                    if (danger_button(tr("Retirer"))) {
                        settings.covers.erase(ckey);
                        save_settings(settings, settings_path);
                    }
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextColored(P.muted, "%s", edit_album.empty() ? tr("Sans album, la pochette appartient à ce morceau seul.")
                                                                     : tr("La pochette est celle de l'album : tous ses morceaux la partagent (elle sert aussi aux exports et aux vidéos)."));
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
            }
            if (modal_buttons(tr("Enregistrer"), !trim(edit_title).empty(), &P.blue)) {
                if (player.id() == edit_id) player.close();  // le fichier MP3 ne doit pas être ouvert
                studio->edit_metadata(edit_id, trim(edit_title), trim(edit_artist), trim(edit_album), trim(edit_track));
                notify(tr("Morceau mis à jour."));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Re-rendu : même graine ou graine aléatoire
        if (open_rerender) {
            ImGui::OpenPopup(tr("Refaire le son : choix de la graine"));
            open_rerender = false;
        }
        ImGui::SetNextWindowSize(ImVec2(520 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Refaire le son : choix de la graine"), nullptr, ImGuiWindowFlags_NoResize)) {
            Song target;
            for (const Song & s : studio->snapshot()) {
                if (s.id == rerender_id) target = s;
            }
            muted_wrapped(trf("Le re-rendu garde la composition de « %s » (mélodie, paroles, structure) et refait seulement la synthèse audio.", target.title.c_str()));
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::BeginDisabled(!target.has_used_seeds);
            if (accent_button(tr("Garder la même graine"), ImVec2(w, 36 * g_dpi), &P.violet)) {
                rerender(target, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            muted_wrapped(target.has_used_seeds
                              ? trf("Rendu identique à l'original (graine %s), sans l'écrêtage des anciennes versions. Utile pour corriger un ancien morceau.", std::to_string(target.used_seed).c_str())
                              : tr("Indisponible : la graine de ce morceau n'a pas été enregistrée."));
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
            if (accent_button(tr("Prendre une graine aléatoire"), ImVec2(w, 36 * g_dpi), &P.accent)) {
                rerender(target, false);
                ImGui::CloseCurrentPopup();
            }
            muted_wrapped(tr("Nouvelle variation du timbre et du mixage, avec la même composition."));
            ImGui::Dummy(ImVec2(0, 4 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        draw_vram_popup();  // mémoire vidéo insuffisante avant une génération

        // Remplacer les paroles par une structure type
        if (form.pending_structure && !ImGui::IsPopupOpen(tr("Appliquer la structure ?"))) ImGui::OpenPopup(tr("Appliquer la structure ?"));
        ImGui::SetNextWindowSize(ImVec2(440 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Appliquer la structure ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(tr("Les paroles actuelles seront remplacées par la structure « %s », avec des lignes vides à compléter."),
                               form.pending_name.c_str());
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Annuler"))) {
                form.pending_structure = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Remplacer les paroles"))) {
                form.lyrics_song       = form.pending_text;
                form.pending_structure = false;
                form.dirty             = true;
                ImGui::ClearActiveID();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Enregistrer un préréglage de style
        if (open_preset) {
            ImGui::OpenPopup(tr("Enregistrer le préréglage"));
            open_preset = false;
        }
        ImGui::SetNextWindowSize(ImVec2(440 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Enregistrer le préréglage"), nullptr, ImGuiWindowFlags_NoResize)) {
            muted_wrapped(form.style);
            // Ce que le préréglage emportera en plus du style
            std::vector<LoraUse> chosen;
            for (const LoraUse & u : form.loras) {
                if (u.enabled && lora_files.count(u.file) && u.file != INSTRUMENTAL_FILE) chosen.push_back(u);
            }
            std::string tags = structure_tags(form.instrumental ? form.lyrics_inst : form.lyrics_song);
            if (!chosen.empty()) {
                std::string names;
                for (const LoraUse & u : chosen) names += (names.empty() ? "" : ", ") + lora_label(u.file);
                ImGui::Checkbox(trf("Inclure les LoRA cochées et leur force (%s)", names.c_str()).c_str(), &preset_with_loras);
            }
            if (!tags.empty()) {
                std::string shown;
                for (char c : tags) shown += c == ',' ? std::string(" > ") : std::string(1, c);
                ImGui::Checkbox((form.instrumental ? trf("Inclure la structure instrumentale (%s)", shown.c_str()) : trf("Inclure la structure des paroles (%s)", shown.c_str())).c_str(),
                                &preset_with_structure);
            }
            std::string negative = join_style(split_style(form.negative_style));
            if (!negative.empty()) {
                ImGui::Checkbox(trf("Inclure le style à éviter (%s)", negative.c_str()).c_str(), &preset_with_negative);
            }
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool enter = ImGui::InputTextWithHint("##preset", tr("Nom du préréglage (ex. : Cold wave 80s)"), &preset_name,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            text_context_menu(preset_name);
            bool valid = !trim(preset_name).empty();
            if ((modal_buttons(tr("Enregistrer"), valid) || (enter && valid))) {
                Preset np;
                np.name  = trim(preset_name);
                np.style = form.style;
                if (preset_with_loras) np.loras = chosen;
                if (preset_with_structure && !tags.empty()) (form.instrumental ? np.structure_inst : np.structure_song) = tags;
                // Style à éviter : mémorisé si la case est cochée ; un champ vide est mémorisé aussi
                // (choisir ce préréglage videra alors le champ).
                np.has_negative = negative.empty() || preset_with_negative;
                if (!negative.empty() && preset_with_negative) {
                    np.negative_style = negative;
                    np.negative_scale = form.negative_scale;
                }
                bool replaced = false;
                for (Preset & p : settings.presets) {
                    if (p.name == np.name && p.pack.empty()) {
                        // Remplacement : on garde la structure de l'autre mode si celle-ci n'est pas réenregistrée
                        if (np.structure_song.empty()) np.structure_song = p.structure_song;
                        if (np.structure_inst.empty()) np.structure_inst = p.structure_inst;
                        p        = np;
                        replaced = true;
                    }
                }
                if (!replaced) settings.presets.push_back(np);
                save_settings(settings, settings_path);
                notify(trf("Préréglage « %s » enregistré.", trim(preset_name).c_str()));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Prolonger un morceau : nouveau morceau qui reprend l'original et le continue
        if (open_extend) {
            ImGui::OpenPopup(tr("Prolonger le morceau"));
            open_extend = false;
        }
        ImGui::SetNextWindowSize(ImVec2(540 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Prolonger le morceau"), nullptr, ImGuiWindowFlags_NoResize)) {
            Song parent;
            for (const Song & s : studio->snapshot()) {
                if (s.id == extend_id) parent = s;
            }
            int max_extra  = std::max(10, 360 - (int) parent.audio_seconds);
            extend_seconds = std::clamp(extend_seconds, 10, max_extra);
            muted_wrapped(trf("Le nouveau morceau reprend « %s » (%s) puis compose la suite dans le même style.", parent.title.c_str(),
                              fmt_time(parent.audio_seconds).c_str()));
            ImGui::TextColored(P.muted, tr("Nom du nouveau morceau"));
            input_line("##ext_title", tr("Nom"), extend_title);
            ImGui::TextColored(P.muted, tr("Durée ajoutée"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##ext_sec", &extend_seconds, 10, max_extra, "%d s");
            ImGui::TextColored(P.muted, parent.instrumental ? tr("Structure de la suite") : tr("Paroles de la suite"));
            ImGui::InputTextMultiline("##ext_lyrics", &extend_lyrics, ImVec2(-FLT_MIN, 160 * g_dpi));
            text_context_menu(extend_lyrics);
            if (modal_buttons(tr("Lancer la prolongation"), !parent.id.empty() && !trim(extend_title).empty(), &P.violet)) {
                Song e;
                e.kind              = "extend";
                e.parent            = parent.id;
                e.parent_title      = parent.title;
                e.title             = trim(extend_title);
                e.style             = parent.style;
                e.instrumental      = parent.instrumental;
                e.instrumental_lora = parent.instrumental_lora;
                e.adapters          = parent.adapters;
                e.negative_style    = parent.negative_style;  // la suite évite le même style
                e.negative_scale    = parent.negative_scale;
                e.cot               = "off";
                e.artist            = parent.artist;
                e.album             = parent.album;
                // Les paroles complètes (originales + suite) accompagnent les codes du morceau d'origine.
                e.lyrics           = parent.lyrics + (parent.instrumental ? "\n" : "\n\n") + extend_lyrics;
                e.duration         = std::min(360.0, parent.audio_seconds + extend_seconds);
                e.expected_seconds = e.duration;
                studio->submit(e);
                notify(trf("Prolongation de « %s » ajoutée à la file.", parent.title.c_str()));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Quitter pendant une génération
        if (g_want_close && !ImGui::IsPopupOpen(tr("Quitter ?"))) ImGui::OpenPopup(tr("Quitter ?"));
        ImGui::SetNextWindowSize(ImVec2(460 * g_dpi, 0));
        if (ImGui::BeginPopupModal(tr("Quitter ?"), nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(tr("Une génération est en cours : elle sera interrompue et vous pourrez la relancer plus tard. Les morceaux en file sont conservés et reviendront en pause."));
            ImGui::Dummy(ImVec2(0, 6 * g_dpi));
            if (ImGui::Button(tr("Rester"))) {
                g_want_close = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (accent_button(tr("Quitter"), ImVec2(0, 0), &P.danger)) {
                g_want_close  = false;
                g_force_close = true;
                ImGui::CloseCurrentPopup();
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ Image

    void update_tray(const std::vector<Song> & songs) {
        int running = 0, waiting = 0, held = 0;
        any_active = downloads_running();  // la progression d'un téléchargement demande des images régulières
        update_downloads();
        poll_language_check();
        any_active |= lang_running;
        poll_translation();
        any_active |= translate_job != nullptr;  // barre de progression de la traduction
        for (const Song & s : songs) {
            any_active |= s.status == "running" || s.waiting();
            running += s.status == "running";
            waiting += s.waiting();
            held += s.status == "queued" && s.held;
            if (s.local && s.active()) {
                watched[s.id] = true;
            } else if (watched.count(s.id) && !s.active()) {
                watched.erase(s.id);
                if (s.status == "done") {
                    unseen_ready++;
                    if (GetForegroundWindow() != hwnd) {
                        FLASHWINFO flash{ sizeof(flash), hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0 };
                        FlashWindowEx(&flash);
                    }
                }
            }
        }
        if (GetForegroundWindow() == hwnd && !IsIconic(hwnd)) unseen_ready = 0;  // vus
        // 1.0.40 : l'icône principale ne parle que de la file (en cours + en attente) ; les terminés ont la leur.
        std::string tip = APP_NAME;
        if (running + waiting == 0) tip += " · " + std::string(tr("File vide"));
        else tip += " · " + trf("File : %d morceau(x)", running + waiting);
        if (running) tip += " · " + trf("%d en cours", running);
        if (waiting) tip += " · " + trf("%d en attente", waiting);
        if (held) tip += " · " + trf("%d en pause", held);
        int unheard = 0;  // 1.0.46 : terminés pas encore écoutés (comme « Lire les nouveaux »)
        for (const Song & s : songs) unheard += s.status == "done" && !s.heard;
        if (unheard) tip += " · " + trf("Non écoutés : %d", unheard);
        tray_update(hwnd, settings.notifications, running + waiting, unheard, tip);
        // Application réduite : jauges CPU / GPU / mémoire GPU et compteurs à côté de l'horloge.
        GpuMem      m = gpu_monitor.get();
        std::string tg = trf("CPU %s · GPU %s", m.cpu < 0 ? "?" : (std::to_string((int) std::lround(m.cpu)) + " %").c_str(),
                             m.gpu < 0 ? "?" : (std::to_string((int) std::lround(m.gpu)) + " %").c_str());
        if (m.ok && m.total) {
            char mem[64];
            snprintf(mem, sizeof(mem), "%.1f / %.1f", m.used / 1073741824.0, m.total / 1073741824.0);
            tg += " · " + trf("Mémoire GPU %s Go", mem);
        }
        std::string tc = trf("Terminés : %d", unseen_ready);
        tray_gauges_update(hwnd, settings.tray_gauges && IsIconic(hwnd), m, unseen_ready, tg, tc);
    }

    void frame() {
        update_imports();  // packs .yue2pack à importer
        std::vector<Song> songs = studio->snapshot();
        static const bool test_shot_on = GetEnvironmentVariableW(L"STUDIO_TEST_SHOT", nullptr, 0) > 0;
        if (test_shot_on) for (Song & s : songs) s.style = test_clean_style(s.style);
        refresh_album_dirs(songs);  // 1.0.43 : dossiers des albums (pochettes)
        update_tray(songs);
        update_games();
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) open_help();  // F1 : aide
        update_autoplay();
        if (driver_update->state == 3 && !driver_update_announced) {
            driver_update_announced = true;
            notify(trf("Mise à jour du pilote graphique disponible (AMD Software %s) : voir Réglages › Carte graphique.", driver_update->latest.c_str()));
        }
        update_jobs();
        update_health();

        const ImGuiViewport * vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Studio YuE2", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        // En-tête de l'application
        const float head_y = ImGui::GetCursorPosY(), head_h = gauge_height();
        ImGui::SetCursorPosY(head_y + std::max(0.f, (head_h - ImGui::GetFrameHeight()) * .5f));
        ImGui::PushFont(g_header);
        ImGui::TextColored(P.accent, "Studio");
        ImGui::SameLine(0, 5 * g_dpi);
        ImGui::TextUnformatted("Audio AI");
        ImGui::PopFont();
        draw_version_badge();
        ImGui::SameLine(0, 18 * g_dpi);
        ImGui::TextColored(P.muted, tr("Modèle"));
        ImGui::SameLine(0, 6 * g_dpi);
        draw_model_combo("##modele_rapide", 120 * g_dpi, false);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Modèle utilisé pour les prochains morceaux (Q8_0 = qualité maximale, Q6_K et Q5_K_M = plus légers).\nLe modèle par défaut au démarrage se règle dans Réglages."));
        }
        ImGui::SameLine(0, 14 * g_dpi);
        ImGui::TextColored(P.muted, tr("Solveur"));
        ImGui::SameLine(0, 6 * g_dpi);
        draw_solver_combo("##solveur_rapide", 150 * g_dpi, false);
        const float gauges_x = ImGui::GetItemRectMax().x + 16 * g_dpi;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Solveur de la phase audio (NAR) pour les prochains morceaux :\nAB3 16, AB3 20, AB3 24, Euler 32, AB2 32, AB3 32, MP 32 (méthode + nombre de pas).\nMoins d'évaluations du réseau = génération plus rapide ; comparez à l'écoute.\nLe solveur par défaut au démarrage se règle dans Réglages."));
        }
        {
            const char * text = tr("Pastilles de notification");
            float        w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(text).x;
            const char * rb = tr("Réglages…");
            const char * hb = tr("Aide");
            float        bw = ImGui::CalcTextSize(rb).x + ImGui::GetStyle().FramePadding.x * 2;
            float        hw = ImGui::CalcTextSize(hb).x + ImGui::GetStyle().FramePadding.x * 2;
            const float right_x = ImGui::GetWindowWidth() - w - bw - hw - ImGui::GetStyle().ItemSpacing.x * 2 - ImGui::GetStyle().WindowPadding.x;
            ImGui::SameLine(right_x);
            if (soft_button(hb, P.teal, false)) open_help();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Ouvre l'aide détaillée de l'application (touche F1)."));
            ImGui::SameLine();
            if (soft_button(rb, driver_changed ? P.warn : P.accent, false)) open_settings = true;
            if (driver_changed && ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Le pilote graphique a changé : un test de santé est conseillé."));
            ImGui::SameLine();
            if (ImGui::Checkbox(text, &settings.notifications)) save_settings(settings, settings_path);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(tr("Icône à côté de l'horloge : pastille rouge = morceaux prêts,\npastille violette = générations en cours ou en attente."));
            }
            const float win_x = ImGui::GetWindowPos().x, win_y = ImGui::GetWindowPos().y;
            draw_gauges(ImVec2(gauges_x, win_y + head_y), win_x + right_x - 12 * g_dpi - gauges_x, head_h);
        }

        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, head_y + head_h));
        ImGui::Dummy(ImVec2(0, 4 * g_dpi));

        float avail_w = ImGui::GetContentRegionAvail().x;
        float avail_h = ImGui::GetContentRegionAvail().y - (ImGui::GetTime() < toast_until ? ImGui::GetFrameHeightWithSpacing() : 0);
        float left_w  = std::floor(avail_w * 0.47f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_dpi, 12 * g_dpi));
        ImGui::BeginChild("form", ImVec2(left_w, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_form();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("library", ImVec2(0, avail_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_library(songs);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (ImGui::GetTime() < toast_until) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.accent, "●");
            ImGui::SameLine();
            ImGui::TextUnformatted(toast.c_str());
        }
        draw_modals();
        ImGui::End();

        if (form.dirty && ImGui::GetTime() - last_draft_save > 1.0) {
            save_draft(form, draft_path);
            last_draft_save = ImGui::GetTime();
        }
    }
};

// ---------------------------------------------------------------------------- Point d'entrée

// Construit les polices pour la langue courante : Segoe UI pour le latin ; pour chaque écriture asiatique, la police
// Windows correspondante fusionnée dans la même police. L'écriture de la langue courante reçoit ses caractères
// usuels et tous ceux de son fichier de langue ; les autres écritures ne reçoivent que les noms de langue
// (pour que la liste des langues reste lisible dans toutes les langues).
static ImVector<ImWchar> g_latin_ranges;
static ImVector<ImWchar> g_cjk_ranges[4];
static void build_fonts(bool runtime) {
    ImGuiIO & io = ImGui::GetIO();
    wchar_t   windir[MAX_PATH];
    GetWindowsDirectoryW(windir, MAX_PATH);
    std::string       fonts  = narrow(windir) + "\\Fonts\\";
    const std::string script = i18n::current_script();
    io.Fonts->Clear();

    static const ImWchar text_ranges[] = { 0x0020, 0x017F, 0x0370, 0x052F, 0x1E00, 0x1EFF, 0x2010, 0x205E, 0x20A0, 0x20CF, 0x2248, 0x2248, 0 };
    static const ImWchar symbol_ranges[] = { 0x2190, 0x21FF, 0x23F8, 0x23FA, 0x25A0, 0x25FF, 0x2713, 0x2713, 0 };
    static const ImWchar punct[] = { 0x2010, 0x2027, 0x3000, 0x303F, 0x3040, 0x30FF, 0xFF00, 0xFFEF, 0 };
    struct Asian { const char * script; const char * regular; const char * bold; };
    static const Asian asian[4] = { { "ja", "YuGothM.ttc", "YuGothB.ttc" }, { "ko", "malgun.ttf", "malgunbd.ttf" },
                                    { "zh-Hans", "msyh.ttc", "msyhbd.ttc" }, { "zh-Hant", "msjh.ttc", "msjhbd.ttc" } };
    auto is_base = [](char32_t c) { return c < 0x250 || (c >= 0x370 && c <= 0x52F) || (c >= 0x1E00 && c <= 0x1EFF) || (c >= 0x2000 && c <= 0x206F) || (c >= 0x20A0 && c <= 0x20CF); };
    auto is_sym  = [](char32_t c) { return (c >= 0x2190 && c <= 0x21FF) || (c >= 0x23F8 && c <= 0x23FA) || (c >= 0x25A0 && c <= 0x25FF) || c == 0x2713 || c == 0x2248; };

    ImFontGlyphRangesBuilder latin, cjk[4];
    latin.AddRanges(text_ranges);
    for (int k = 0; k < 4; k++) {
        // noms des langues écrites dans cette écriture
        for (const i18n::Language & l : i18n::languages()) {
            if (l.script != asian[k].script) continue;
            cjk[k].AddText(l.name.c_str());
        }
    }
    for (char32_t c : i18n::glyph_text()) {
        if (c > 0xFFFF || is_sym(c)) continue;
        if (is_base(c)) latin.AddChar((ImWchar) c);
        else for (int k = 0; k < 4; k++) if (script == asian[k].script) cjk[k].AddChar((ImWchar) c);
    }
    for (int k = 0; k < 4; k++) {
        if (script != asian[k].script) continue;
        cjk[k].AddRanges(punct);
        if (script == "ja") cjk[k].AddRanges(io.Fonts->GetGlyphRangesJapanese());
        else if (script == "ko") cjk[k].AddRanges(io.Fonts->GetGlyphRangesKorean());
        else if (script == "zh-Hans") cjk[k].AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        else {
            // caractères traditionnels usuels : les 5 401 du niveau 1 de Big5 (page de codes 950)
            for (int lead = 0xA4; lead <= 0xC6; lead++) {
                for (int trail = 0x40; trail <= 0xFE; trail++) {
                    if (trail > 0x7E && trail < 0xA1) continue;
                    char    bytes[2] = { (char) lead, (char) trail };
                    wchar_t w        = 0;
                    if (MultiByteToWideChar(950, MB_ERR_INVALID_CHARS, bytes, 2, &w, 1) == 1 && w >= 0x2E80) cjk[k].AddChar((ImWchar) w);
                }
            }
        }
    }
    g_latin_ranges.clear();
    latin.BuildRanges(&g_latin_ranges);
    for (int k = 0; k < 4; k++) {
        g_cjk_ranges[k].clear();
        cjk[k].BuildRanges(&g_cjk_ranges[k]);
    }

    auto add = [&](const char * latin_file, bool bold, float size) -> ImFont * {
        ImFont * f = io.Fonts->AddFontFromFileTTF((fonts + latin_file).c_str(), size, nullptr, g_latin_ranges.Data);
        if (!f) return io.Fonts->AddFontDefault();
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF((fonts + "seguisym.ttf").c_str(), size, &merge, symbol_ranges);
        for (int k = 0; k < 4; k++) {
            if (g_cjk_ranges[k].Size <= 1) continue;
            ImFontConfig c;
            c.MergeMode   = true;
            c.OversampleH = c.OversampleV = 1;
            io.Fonts->AddFontFromFileTTF((fonts + (bold ? asian[k].bold : asian[k].regular)).c_str(), size, &c, g_cjk_ranges[k].Data);
        }
        return f;
    };
    float size = std::round(16.f * g_dpi);
    add("segoeui.ttf", false, size);
    g_bold   = add("segoeuib.ttf", true, size);
    g_header = add("segoeuib.ttf", true, std::round(21.f * g_dpi));
    if (!g_bold) g_bold = io.Fonts->Fonts[0];
    if (!g_header) g_header = io.Fonts->Fonts[0];
    if (runtime) {
        io.Fonts->Build();
        ImGui_ImplDX11_InvalidateDeviceObjects();  // la texture des polices est recréée à l'image suivante
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    Paths paths;
    if (!Paths::locate(fs::path(exe).parent_path(), paths)) {
        MessageBoxW(nullptr,
                    L"Impossible de trouver le moteur de synthèse (yue-synth.exe).\n"
                    L"Placez Studio Audio AI.exe dans le dossier du projet (à côté de yue2.cpp\\build\\Release\\).",
                    APP_NAME_W, MB_ICONERROR);
        return 1;
    }
    // Carte graphique : marque (AMD, Nvidia, Intel), nom, mémoire ; moteur CUDA installé ou non.
    gpu_profile().gpu            = detect_gpu();
    gpu_profile().cuda_installed = !paths.cuda_dir.empty();
    // Mode de test (sans fenêtre) : « --analyze fichier sortie.txt » écrit le résultat de l'analyse audio.
    {
        int      argc = 0;
        wchar_t ** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Dossier des modèles : celui du programme (1.0.30 : plus de chemin « models » codé en dur).
        if (argv && argc >= 4 && paths.models_dir.empty()) paths.use_models(paths.root / "modeles");
        fs::path models_dir = paths.models_dir.empty() ? paths.root / "modeles" : paths.models_dir;
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--lang") {  // essai : langue chantée d'un fichier
            float avg = 0;
            int   votes = 0;
            std::string code = detect_audio_language(argv[2], paths.whisper, models_dir / "voix" / whisper_file().name,
                                                     app_temp_dir() / L"langue_test", media_duration(argv[2]), avg, votes);
            FILE * f = _wfopen(argv[3], L"wb");
            if (f) {
                std::string vl;
                int         vw = 0;
                detect_audio_vocals(argv[2], paths.whisper, models_dir / "voix" / whisper_file().name,
                                    app_temp_dir() / L"langue_test", media_duration(argv[2]), vl, vw);
                fprintf(f, "%s avg=%.2f votes=%d | vocals lang=%s words=%d", code.c_str(), avg, votes, vl.c_str(), vw);
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.33) : « --ai-test plan.txt sortie.txt » lit un plan d'album écrit par une IA et vérifie la
        // recherche des mots interdits et des mots fréquents, sans interface ni IA.
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--ai-test") {
            AiAlbumRequest r;
            r.count         = 6;
            r.kind          = 2;
            r.instrumentals = 2;
            r.influence     = "AC/DC";
            r.avoid         = ai_default_avoid_words();
            r.negative      = "vocoder, autotune";
            AiAlbumPlan plan = ai_parse_album_plan(read_text(argv[2]), r);
            FILE *      f    = _wfopen(argv[3], L"wb");
            if (f) {
                fprintf(f, "ok=%d band=%s album=%s sound=%s tracks=%d\n", (int) plan.ok, plan.band.c_str(), plan.album.c_str(), plan.sound.c_str(), (int) plan.tracks.size());
                for (size_t i = 0; i < plan.tracks.size(); i++) {
                    const AiTrackPlan & t = plan.tracks[i];
                    fprintf(f, "%d. [%s] inst=%d bpm=%d mood=%s theme=%s\n", (int) i + 1, t.title.c_str(), (int) t.instrumental, t.bpm, t.mood.c_str(), t.theme.c_str());
                }
                if (plan.ok) fprintf(f, "context 2: %s\n", ai_track_context(plan, 1).c_str());
                fprintf(f, "cover_prompt: %s\n", plan.cover_prompt.c_str());  // 1.0.36
                {   // 1.0.35 : mélangé, « l'IA répartit » → jamais plus de 2 instrumentaux
                    AiAlbumRequest r0 = r;
                    r0.instrumentals  = 0;
                    AiAlbumPlan p0    = ai_parse_album_plan(read_text(argv[2]), r0);
                    int         k     = 0;
                    for (const AiTrackPlan & t : p0.tracks) k += t.instrumental ? 1 : 0;
                    fprintf(f, "mixed auto: %d instrumental(s)\n", k);
                }
                auto show = [&](const char * label, const std::vector<std::string> & v) {
                    std::string s;
                    for (const std::string & w : v) s += (s.empty() ? "" : ", ") + w;
                    fprintf(f, "%s: %s\n", label, s.c_str());
                };
                show("found (Les Rêves de la NUIT, Cœurs perdus)", ai_forbidden_found("Les Rêves de la NUIT\nCœurs perdus sous l'orage", r.avoid));
                show("found (Lumières / lumineux / Starlight)", ai_forbidden_found("Lumières et lumineux, Starlight", r.avoid));
                show("found (Shadows and Echoes)", ai_forbidden_found("Shadows and Echoes", r.avoid));
                // 1.0.34 : nom du groupe / titre de l'album, doublons, styles à éviter
                show("plan forbidden (IA)", ai_plan_forbidden(plan, r));
                {
                    AiAlbumRequest typed = r;
                    typed.band           = plan.band;
                    typed.album          = plan.album;  // tapés par l'utilisateur : jamais vérifiés
                    show("plan forbidden (tapés)", ai_plan_forbidden(plan, typed));
                }
                {
                    int removed = 0;
                    show("dedupe", ai_dedupe_words({ "Rêve", "rêves", "NUIT", "nuit", "cœur", "coeurs", "étoile", "Etoiles", "feu", "feux", "star", "stars", "ombre" }, removed));
                    fprintf(f, "dedupe removed=%d\n", removed);
                }
                // 1.0.37 : titre seul redemandé
                fprintf(f, "parse_title: [%s] [%s] [%s]\n", ai_parse_title("{\"title\": \"Ancient Ruins\"}").c_str(), ai_parse_title("Title: \"Stone Giants\"\n").c_str(),
                        ai_parse_title("<think>hmm</think>\n  Rivers of Iron  ").c_str());
                fprintf(f, "title_prompt: %s\n", ai_title_retry_prompt("Echoes of the Ancients", ai_forbidden_found("Echoes of the Ancients", r.avoid), { "echo", "night" }).c_str());
                // 1.0.37 : instrumental forcé sans voix
                fprintf(f, "force_inst: %s\n", ai_force_instrumental_style("hard rock, male vocals, Raspy Voice, choir, electric guitar, trap, tenor sax, rap, 120 BPM").c_str());
                fprintf(f, "force_inst (déjà bon): %s\n", ai_force_instrumental_style("instrumental, no vocals, ambient").c_str());
                fprintf(f, "inst_negative: %s\n", ai_instrumental_negative("autotune, Vocals").c_str());
                {
                    int         removed = 0;
                    std::string lyr     = ai_clean_lyrics("[Intro]\n[Verse]\nI walk alone tonight\n[Chorus]\nOh oh oh\n[Outro]\n", true, removed);
                    fprintf(f, "inst_lyrics (retirés=%d): %s\n", removed, lyr.c_str());
                }
                fprintf(f, "strip_negative: %s\n", ai_strip_negative("hard rock, Vocoder, electric guitar, autotune, male vocals", "vocoder, AUTOTUNE").c_str());
                show("frequent", ai_frequent_words({ "La route\nLa route brûle sous nos pas, la poussière vole", "Poussière d'acier\nLa route encore, toujours la route",
                                                     "Moteur\nPoussière et route, moteur hurlant", "Autre\nRien à voir ici" }, 3, 20));
                AiRequest q;
                q.language    = "French";
                q.influence   = "AC/DC";
                q.avoid       = { "nuit", "cœur" };
                q.used_titles = { "Route 66", "Poussière d'acier" };
                q.negative    = "vocoder, autotune";
                q.album_context = plan.ok ? ai_track_context(plan, 0) : "";
                fprintf(f, "--- prompt ---\n%s\n", ai_user_prompt(q).c_str());
                fprintf(f, "--- album prompt ---\n%s\n", ai_album_user_prompt(r).c_str());
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--analyze") {
            if (paths.transcriber.empty()) paths.transcriber = models_dir / "musique" / "SheetSage2-Q8_0.gguf";
            AudioAnalysis a;
            std::atomic<bool> cancel{ false };
            bool ok = analyze_audio(argv[2], models_dir / "analyse", app_temp_dir() / L"analyse_test", paths.transcribe,
                                    paths.transcriber, paths.whisper, models_dir / "voix" / whisper_file().name, [](const std::string &, float) {}, cancel, a);
            FILE * f = _wfopen(argv[3], L"wb");
            if (f) {
                auto line = [&](const char * fmt, auto... args) { fprintf(f, fmt, args...); fputc(10, f); };
                line("ok=%d error=%s duration=%.1f", (int) ok, a.error.c_str(), a.duration);
                line("voice=%.2f male=%.2f bright=%.2f tonal=%.2f danceable=%.2f", a.voice, a.male, a.bright, a.tonal, a.danceable);
                line("bpm=%.1f key=%s meter=%s notes=%d low=%d high=%d med=%d", a.bpm, a.key.c_str(), a.meter.c_str(), (int) a.has_vocal_notes,
                     a.vocal_low, a.vocal_high, a.vocal_median);
                for (size_t i = 0; i < a.styles.size() && i < 8; i++) line("style %s %.3f", a.styles[i].name.c_str(), a.styles[i].score);
                for (size_t i = 0; i < a.moods.size() && i < 6; i++) line("mood %s %.3f", a.moods[i].name.c_str(), a.moods[i].score);
                for (size_t i = 0; i < a.instruments.size() && i < 8; i++) line("inst %s %.3f", a.instruments[i].name.c_str(), a.instruments[i].score);
                for (auto & m : a.mood_flags) line("flag %s %.3f", m.first.c_str(), m.second);
                for (auto & sct : a.sections) line("section %s bars=%d sec=%.1f", sct.label.c_str(), sct.bars, sct.seconds);
                for (auto & c : a.chords) line("chord %s", c.c_str());
                for (auto & sg : build_suggestions(a)) line("SUG [%s] %s sel=%d (%s)", sg.group.c_str(), sg.tag.c_str(), (int) sg.selected, sg.why.c_str());
                std::string st;
                for (auto & t : suggested_structure(a, false)) st += "[" + t + "] ";
                line("structure %s", st.c_str());
                line("lyrics_lang=%s err=%s lines=%d", a.lyrics_lang.c_str(), a.lyrics_error.c_str(), (int) a.lyric_lines.size());
                line("%s", suggested_lyrics(a).c_str());
                fclose(f);
            }
            return 0;
        }
    }
    // Langues : dossier « langues » ; celle des réglages, sinon celle du système.
    i18n::init(app_resource_dir(L"langues"));  // ressources\langues (1.0.30), sinon langues
    {
        std::string code;
        migrate_settings(paths.data, paths.config);  // 1.0.41 : avant toute lecture des réglages
        std::string text = read_text(paths.config / "reglages.json");
        if (yyjson_doc * doc = yyjson_read(text.data(), text.size(), 0)) {
            yyjson_val * v = yyjson_obj_get(yyjson_doc_get_root(doc), "ui_language");
            if (yyjson_is_str(v)) code = yyjson_get_str(v);
            yyjson_doc_free(doc);
        }
        if (code.empty()) code = i18n::system_language();
        if (const wchar_t * lang = _wgetenv(L"STUDIO_TEST_LANG")) code = narrow(lang);  // essai : captures de l'aide
        if (!i18n::set_language(code)) i18n::set_language("fr");
    }
    // Premier lancement : télécharger le modèle ou indiquer le dossier qui le contient déjà.
    if (!ensure_models(instance, fs::path(exe).parent_path(), paths)) return 0;

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR                    gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, instance, LoadIconW(instance, MAKEINTRESOURCEW(1)),
                    LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"StudioYuE2", nullptr };
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    g_icon = wc.hIcon;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, APP_NAME_W L" " STUDIO_VERSION_W, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              (int) (1360 * g_dpi), (int) (900 * g_dpi), nullptr, nullptr, instance, nullptr);
    if (!create_device(hwnd)) {
        cleanup_device();
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }
    bool light     = windows_light_theme();
    g_system_light = light;
    BOOL dark  = !light;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    DragAcceptFiles(hwnd, TRUE);  // packs .yue2pack glissés sur la fenêtre

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO & io    = ImGui::GetIO();
    io.IniFilename  = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_theme(light, g_dpi);

    build_fonts(false);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    App app;
    app.hwnd          = hwnd;
    app.draft_path    = paths.cache / "brouillon.json";   // 1.0.41 : brouillon dans cache, réglages dans config
    app.settings_path = paths.config / "reglages.json";
    load_draft(app.form, app.draft_path);
    load_settings(app.settings, app.settings_path);
    app.load_last_album();  // 1.0.36 : dernier album de « Composer avec l'IA »
    g_theme = app.settings.theme;  // thème choisi (le thème par défaut suit Windows)
    g_collapsed.insert(app.settings.collapsed.begin(), app.settings.collapsed.end());
    apply_theme(light, g_dpi);
    {
        BOOL dark_title = theme_is_dark(g_theme, light);
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_title, sizeof(dark_title));
    }
    app.driver_version = gpu_driver_version();
    if (!app.driver_version.empty()) {
        if (app.settings.gpu_driver.empty()) {
            app.settings.gpu_driver = app.driver_version;  // première utilisation : version de référence
            save_settings(app.settings, app.settings_path);
        } else if (app.settings.gpu_driver != app.driver_version) {
            app.driver_changed = true;
        }
    }
    app.form.language = app.settings.language;
    {   // recherche d'un pilote AMD plus récent, en tâche de fond
        std::string cn = amd_adrenalin_version();
        if (!cn.empty()) std::thread(check_driver_update, app.driver_update, cn).detach();
    }
    app.player.set_volume(app.settings.volume);
    // Profil de calcul d'après les réglages ; le moteur CUDA (cartes Nvidia) se choisit ici, avant de créer la file.
    gpu_profile().compat  = compat_from_string(app.settings.gpu_compat);
    gpu_profile().backend = backend_from_string(app.settings.gpu_backend);
    set_gpu_limit(app.settings.gpu_limit);
    if (gpu_profile().want_cuda()) gpu_profile().cuda_active = paths.use_cuda_engine();
    Studio studio(paths, [hwnd] { PostMessageW(hwnd, WM_APP_NOTIFY, 0, 0); });
    g_studio   = &studio;
    app.studio = &studio;
    app.init_models();
    app.sysinfo = read_system_info(paths.models_dir);  // mémoire vive, processeur, disque des modèles (recommandations)
    app.init_solver();
    app.refresh_album_dirs(studio.snapshot());
    app.migrate_covers();            // 1.0.43 : pochettes rangées dans le dossier de leur album
    if (const wchar_t * g = _wgetenv(L"STUDIO_TEST_GAUGE")) app.settings.gauge_style = _wtoi(g);  // essai : style des compteurs, non enregistré
    set_gpu_limit(app.settings.gpu_limit);
    // Essai (1.0.36) : « --cover-test "Groupe" "Album" "description" sortie.txt » crée la pochette 1:1 de l'album avec le
    // moteur local (models\images de ce programme) et la range comme le ferait la fenêtre Albums, sans interface.
    {
        int        argc = 0;
        wchar_t ** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Essai (1.0.41) : « --config-test sortie.txt » écrit ce qui a été relu de config\ (réglages, styles…).
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--config-test") {
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                const Settings & s = app.settings;
                fprintf(f, "config=%s\ncache=%s\nstyles=%d structures=%d packs=%d\n"
                           "mots_a_eviter=%d derniers_morceaux=%d cles_api=%d pochettes=%d descriptions=%d\nmorceaux=%d theme=%d modele=%s\n",
                        narrow(paths.config.wstring()).c_str(), narrow(paths.cache.wstring()).c_str(), (int) s.presets.size(), (int) s.structures.size(), (int) s.packs.size(),
                        (int) s.ai_avoid.size(), (int) s.ai_recent.size(), (int) s.ai_keys.size(), (int) s.covers.size(),
                        (int) s.cover_prompts.size(), (int) studio.snapshot().size(), s.theme, s.model.c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.43) : « --audios-test sortie.txt [id groupe album piste] » modifie éventuellement un morceau, puis
        // écrit le dossier de chaque morceau et les pochettes ; « --rename-test clé groupe album sortie.txt » renomme un album.
        if (argv && argc >= 3 && (std::wstring(argv[1]) == L"--audios-test" || std::wstring(argv[1]) == L"--rename-test")) {
            bool        rename = std::wstring(argv[1]) == L"--rename-test";
            const wchar_t * out = rename ? (argc >= 6 ? argv[5] : nullptr) : argv[2];
            if (rename && argc >= 6) app.rename_album(narrow(argv[2]), narrow(argv[3]), narrow(argv[4]));
            if (!rename && argc >= 7) {
                for (const Song & s : studio.snapshot())
                    if (s.id == narrow(argv[3])) studio.edit_metadata(s.id, s.title, narrow(argv[4]), narrow(argv[5]), narrow(argv[6]));
            }
            Sleep(1500);  // étiquettes réécrites en arrière-plan
            studio.rescan();
            app.refresh_album_dirs(studio.snapshot());
            if (FILE * f = out ? _wfopen(out, L"wb") : nullptr) {
                for (const Song & s : studio.snapshot())
                    fprintf(f, "%s | %s | %s | %s | %s\n", s.id.c_str(), s.artist.c_str(), s.album.c_str(), s.title.c_str(),
                            narrow(song_folder(s.id).wstring()).c_str());
                for (const auto & [k, v] : app.settings.covers)
                {
                    std::wstring stem;
                    fs::path     dir = app.cover_dir(k, stem);
                    std::string found = app.cover_for(k);
                    fprintf(f, "pochette %s = %s -> %s  [dossier %s]\n", k.c_str(), v.c_str(), found.c_str(), narrow(dir.wstring()).c_str());
                }
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.43) : « --gen-test "Groupe" "Album" "Titre" sortie.txt » génère un instrumental de 20 s par la file du
        // Studio (comme le bouton Créer) et écrit où il a été rangé.
        // Essai (1.0.46) : « --delete-album-test "Groupe" "Album" sortie.txt » met l'album à la corbeille comme « Albums… ».
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--delete-album-test") {
            app.delete_album_artist = narrow(argv[2]);
            app.delete_album_name   = narrow(argv[3]);
            app.delete_album_key    = album_key(app.delete_album_artist, app.delete_album_name);
            fs::path dir            = album_folder(app.delete_album_artist, app.delete_album_name);
            bool     before         = fs::exists(dir);
            app.delete_album();
            studio.rescan();
            if (FILE * f = _wfopen(argv[4], L"wb")) {
                fprintf(f, "dossier=%s avant=%d apres=%d morceaux_restants=%d\n", narrow(dir.wstring()).c_str(), (int) before,
                        (int) fs::exists(dir), (int) studio.snapshot().size());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.47) : « --band-test "Groupe" "Album" sortie.txt » écrit ce que l'IA recevrait (lecture seule).
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--band-test") {
            AiAlbumRequest r;
            r.band  = narrow(argv[2]);
            r.album = narrow(argv[3]);
            app.add_band_history(r);
            if (FILE * f = _wfopen(argv[4], L"wb")) {
                fprintf(f, "complete=%d derniere_piste=%d titres=%d albums=%d\nson=%s\n", (int) r.complete, app.band_album_last_track(r.band, r.album),
                        (int) r.used_titles.size(), (int) r.used_albums.size(), r.sound.c_str());
                for (const std::string & a : r.used_albums) fprintf(f, "album: %s\n", a.c_str());
                fprintf(f, "--- consigne\n%s\n", ai_album_user_prompt(r).c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.48) : « --split-test paroles.txt sortie.txt » écrit les deux parties d'un morceau long.
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--split-test") {
            auto parts = split_long_lyrics(read_text(argv[2]));
            if (FILE * f = _wfopen(argv[3], L"wb")) {
                fprintf(f, "=== partie 1\n%s\n=== partie 2\n%s\n", parts.first.c_str(), parts.second.c_str());
                fclose(f);
            }
            return 0;
        }
        // Essai (1.0.64) : « --story-test sortie.txt » : réponses de l'IA mal fermées, lues ou refusées comme prévu.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--story-test") {
            const std::string one = "{\"first\": 0, \"last\": 0, \"prompt\": \"a\"}";
            const struct { const char * name; std::string text; size_t want; } cases[] = {
                { "correct", "{\"scenes\": [" + one + "," + one + "]}", 2 },
                { "accolade oubliee", "{\"scenes\": [" + one + "," + one + "]\n", 2 },
                { "coupee dans un texte", "{\"scenes\": [" + one + ",{\"first\": 0, \"last\": 0, \"prompt\": \"b", 2 },
                { "virgule finale", "{\"scenes\": [" + one + ",]}", 1 },
                { "texte autour", "Voici :\n{\"scenes\": [" + one + "]} Bonne chance {", 1 },
                { "aucune scene", "je ne sais pas", 0 },
            };
            int bad = 0;
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                for (const auto & c : cases) {
                    std::vector<SceneSpec> got;
                    clip_chunk_parse(c.text, -1, -1, 10, got);
                    bad += got.size() != c.want;
                    fprintf(f, "%s : %d scene(s), attendu %d\n", c.name, (int) got.size(), (int) c.want);
                }
                fprintf(f, "%s\n", bad ? "ECHEC" : "OK");
                fclose(f);
            }
            return bad;
        }
        // Essai (1.0.70) : « --lengthen-test sortie.txt » : suite des paroles, d'abord avec une fausse IA, puis avec l'IA locale.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--lengthen-test") {
            AiSong song;
            song.ok = true, song.title = "Essai", song.style = "rock";
            song.lyrics = "[Verse]\n";
            for (int i = 0; i < 30; i++) song.lyrics += "ligne " + std::to_string(i) + "\n";
            song.lyrics += "\n[Outro]";
            std::atomic<bool> stop{ false };
            ai_lengthen_song(song, 80, {}, [](const std::string &, const std::string &) {
                AiCallResult r;
                r.ok   = true;
                r.text = "[Verse]\n";
                static int call = 0;
                for (int i = 0; i < 30; i++) r.text += "suite " + std::to_string(call * 20 + i) + "\n";  // 1.0.73 : 10 lignes recopiées par réponse
                r.text += "[Chorus]\nrefrain un\nrefrain deux\n[Outro]";
                call++;
                return r;
            }, &stop);
            size_t outros = 0;
            for (size_t p = song.lyrics.find("[Outro]"); p != std::string::npos; p = song.lyrics.find("[Outro]", p + 1)) outros++;
            std::set<std::string> seen;
            int                   copies = 0;
            for (const std::string & l : ai_song_lines(song.lyrics)) copies += l.rfind("refrain", 0) != 0 && !seen.insert(l).second;
            std::string           dup = "[Verse]\nUne Ligne.\nune ligne\nautre\n\n[Bridge]\nautre\n\n[Chorus]\nautre\nautre";
            std::set<std::string> k0;
            const int             dropped = ai_drop_repeated_lines(dup, k0);  // 2 retirées : ligne en double et pont vidé
            std::string ch = "[Chorus]\nA\nB\nC\n\n[Chorus]\nA\nB\nC\nD";
            std::set<std::string> k1;
            const int ch_drop = ai_drop_repeated_lines(ch, k1, 2);  // 1 retirée : C repris
            const bool ok = ch_drop == 1 && ai_sung_lines(ch) == 6 && ai_sung_lines(song.lyrics) >= 80 && outros == 1 && song.lyrics.rfind("[Outro]") + 7 == song.lyrics.size() && copies == 0 &&
                            dropped == 2 && dup.find("[Bridge]") == std::string::npos && ai_sung_lines(dup) == 4;
            FILE * f = _wfopen(argv[2], L"wb");
            if (f) fprintf(f, "fausse IA : %d lignes, %d [Outro], %d lignes recopiees, tri %d : %s\n", ai_sung_lines(song.lyrics), (int) outros, copies, dropped, ok ? "OK" : "ECHEC");
            app.local_llm = find_local_llm(app.app_dir());
            AiRequest req = app.ai_request();
            req.length       = 3;
            req.instrumental = false;
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<int> stage{ 0 };
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(req));
                AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                const int    first = ai_sung_lines(s.lyrics);
                ai_lengthen_song(s, AI_LONG_LINES, req.avoid, [&](const std::string & sy, const std::string & u) {
                    AiCallResult r = ask(sy, u);
                    if (f) fprintf(f, "--- suite demandee : ok=%d, %d lignes lues\n%s\n", (int) r.ok, ai_sung_lines(ai_parse_song(r.text, false).lyrics), r.text.substr(0, 900).c_str());
                    return r;
                }, &stop);
                if (f) fprintf(f, "IA locale : %d lignes au depart, %d a la fin, estimation %.0f s\n%s\n", first, ai_sung_lines(s.lyrics), estimate_seconds(s.lyrics, false), s.lyrics.c_str());
            });
            if (f) fclose(f);
            return ok ? 0 : 1;
        }
        // Essai (1.0.73) : « --album-lines-test sortie.txt [n] » : plan et paroles d'un album de n chansons (4 par défaut) par
        // l'IA locale, comme le fil de l'album, sans musique ; compte les lignes de couplet reprises d'un morceau précédent.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--album-lines-test") {
            app.local_llm      = find_local_llm(app.app_dir());
            AiAlbumRequest req = app.ai_album_request();
            req.count          = argc >= 4 ? std::clamp(_wtoi(argv[3]), 2, 10) : 4;
            req.kind           = 0;
            req.band.clear(), req.album.clear(), req.used_titles.clear(), req.used_albums.clear(), req.complete = false;
            AiRequest base = app.ai_request();
            base.title.clear(), base.instrumental = false, base.length = 1;
            fs::path        model = app.ai_models_dir() / widen(app.settings.ai_local_model);
            std::error_code ec;
            auto            size   = fs::file_size(model, ec);
            int             layers = app.local_llm.vulkan ? ai_gpu_layers(ec ? 0 : (unsigned long long) size, gpu_profile().gpu.vram) : 0;
            std::atomic<int>  stage{ 0 };
            std::atomic<bool> stop{ false };
            FILE *            f        = _wfopen(argv[2], L"wb");
            int               residual = 0;
            ai_local_session(app.local_llm, model, layers, &stage, &stop, [&](const AiAsk & ask) {
                AiCallResult res  = ask(ai_album_system_prompt(), ai_album_user_prompt(req));
                AiAlbumPlan  plan = res.ok ? ai_parse_album_plan(res.text, req) : AiAlbumPlan{};
                if (f) fprintf(f, "plan : %s, %s / %s, %d pistes\n", plan.ok ? "OK" : "illisible", plan.band.c_str(), plan.album.c_str(), (int) plan.tracks.size());
                std::vector<std::string> titles, album_lines;
                for (int i = 0; plan.ok && i < (int) plan.tracks.size(); i++) {
                    AiRequest r     = base;
                    r.title         = plan.tracks[i].title;
                    if (!plan.tracks[i].theme.empty()) r.theme = plan.tracks[i].theme;
                    r.used_titles   = titles;
                    r.used_lines    = album_lines;
                    r.album_context = ai_track_context(plan, i);
                    AiCallResult a = ask(ai_system_prompt(false), ai_user_prompt(r));
                    AiSong       s = a.ok ? ai_parse_song(a.text, false) : AiSong{};
                    if (!s.ok) {
                        if (f) fprintf(f, "piste %d illisible\n", i + 1);
                        continue;
                    }
                    // même tri que le fil de l'album ; « avant » = lignes de couplet déjà chantées dans l'album
                    std::set<std::string> before = ai_known_lines(album_lines), probe = before;
                    std::string           copy   = s.lyrics;
                    const int             all    = ai_drop_repeated_lines(copy, probe);
                    std::set<std::string> self;
                    std::string           alone = s.lyrics;
                    const int             own   = ai_drop_repeated_lines(alone, self);  // recopiées dans le morceau lui-même
                    std::set<std::string> known = ai_known_lines(album_lines);
                    ai_drop_repeated_lines(s.lyrics, known);
                    std::set<std::string> again = ai_known_lines(album_lines);
                    std::string           check = s.lyrics;
                    residual += ai_drop_repeated_lines(check, again);
                    if (f) fprintf(f, "\n=== piste %d : %s : %d lignes, reprises de l'album %d, recopiees dans le morceau %d, apres tri %d lignes\n%s\n", i + 1,
                                   r.title.c_str(), ai_sung_lines(copy) + all, all - own, own, ai_sung_lines(s.lyrics), s.lyrics.c_str());
                    titles.push_back(r.title);
                    for (const std::string & l : ai_song_lines(s.lyrics))
                        if (std::find(album_lines.begin(), album_lines.end(), l) == album_lines.end()) album_lines.push_back(l);
                }
            });
            if (f) fprintf(f, "\nreste apres tri : %d : %s\n", residual, residual == 0 ? "OK" : "ECHEC"), fclose(f);
            return residual == 0 ? 0 : 1;
        }
        // Essai (1.0.48) : « --long-test sortie.txt » : instrumental court en 3 parties (40 s puis les suites), assemblées.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--long-test") {
            Song s;
            s.title             = std::string("Essai long") + tr(" (partie 1)");
            s.artist            = "Groupe Essai";
            s.style             = "instrumental, synthwave, analog synth, driving bass, 100 BPM";
            s.lyrics            = "[intro]\n[verse]";
            s.long_rest         = std::string("[chorus]") + LONG_PART_MARK + "[verse]\n[outro]";
            s.instrumental      = true;
            s.instrumental_lora = false;
            s.cot               = "off";
            s.duration          = 40;
            s.kind              = "new";
            if (argc >= 4) {  // 1.0.70 : vraie chanson en 3 parties : « --long-test sortie.txt paroles.txt »
                auto a = split_long_lyrics(read_text(argv[3]), 3);
                auto b = split_long_lyrics(a.second);
                s.lyrics           = a.first;
                s.long_rest        = b.first + LONG_PART_MARK + b.second;
                s.style            = "English, alternative rock, male vocals, driving drums, electric guitar, 125 BPM";
                s.instrumental     = false;
                s.cot              = "off";
                s.auto_duration    = true;
                s.duration         = 360;
                s.expected_seconds = estimate_seconds(s.lyrics, false);
            }
            std::string id1 = studio.submit(s, false), id2;
            auto        t0  = std::chrono::steady_clock::now();
            Song        last;
            for (;;) {
                Sleep(1000);
                for (const Song & x : studio.snapshot()) {
                    if (x.kind == "extend" && x.artist == s.artist && x.long_rest.empty() && x.created >= s.created) id2 = x.id;
                    if (!id2.empty() && x.id == id2) last = x;
                }
                if (last.status == "done" || last.status == "error" || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2400)) break;
            }
            Sleep(500);
            if (FILE * f = _wfopen(argv[2], L"wb")) {
                double p1 = 0;
                for (const Song & x : studio.snapshot()) if (x.id == id1) p1 = x.audio_seconds;
                fprintf(f, "partie1=%.1f s  assemble=%.1f s  statut=%s %s  secondes=%.0f\ndossier=%s\n", p1, last.audio_seconds, last.status.c_str(),
                        last.message.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                        narrow(song_folder(id2).wstring()).c_str());
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--gen-test") {
            Song s;
            s.title        = narrow(argv[4]);
            s.artist       = narrow(argv[2]);
            s.album        = narrow(argv[3]);
            s.track        = "1";
            s.style        = "instrumental, synthwave, analog synth, driving bass, 100 BPM";
            s.lyrics       = "[intro]\n[interlude]\n[outro]";
            s.instrumental = true;
            s.instrumental_lora = false;
            s.cot          = "off";
            s.duration     = 20;
            s.kind         = "new";
            std::string id = studio.submit(s, false);
            auto        t0 = std::chrono::steady_clock::now();
            Song        done;
            for (;;) {
                Sleep(1000);
                for (const Song & x : studio.snapshot())
                    if (x.id == id) done = x;
                if (done.status == "done" || done.status == "error" ||
                    std::chrono::steady_clock::now() - t0 > std::chrono::seconds(600)) break;
            }
            Sleep(500);
            if (FILE * f = _wfopen(argv[5], L"wb")) {
                fprintf(f, "statut=%s message=%s secondes=%.0f duree_audio=%.1f\ndossier=%s\n", done.status.c_str(), done.message.c_str(),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), done.audio_seconds,
                        narrow(song_folder(id).wstring()).c_str());
                std::error_code ec;
                for (const auto & e : fs::directory_iterator(song_folder(id), ec))
                    fprintf(f, "  %s %llu\n", narrow(e.path().filename().wstring()).c_str(), (unsigned long long) (e.is_regular_file() ? e.file_size() : 0));
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        // Essai (1.0.52) : « --clip-test "Titre" image.png 1080p|tiktok|ig sortie.mp4 » crée le clip animé d'un morceau de la
        // bibliothèque (sans l'interface) et écrit le compte rendu dans sortie.mp4.txt.
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--clip-test") {
            Song found;
            for (const Song & x : studio.snapshot())
                if (x.status == "done" && x.title == narrow(argv[2])) found = x;
            std::wstring fmt = argv[4];  // « all » : les 3 formats, argv[5] = préfixe des sorties (essai du montage en parallèle, 1.0.58)
            ClipTask     t;
            t.song          = found;
            t.image         = argv[3];
            t.output        = argv[5];
            t.width         = fmt == L"tiktok" ? 1080 : fmt == L"ig" ? 1080 : 1920;
            t.height        = fmt == L"tiktok" || fmt == L"short" ? 1920 : fmt == L"ig" ? 1080 : 1080;
            if (fmt == L"short") t.width = 1080, t.short_seconds = _wgetenv(L"STUDIO_TEST_SHORT_SECONDS") ? _wtoi(_wgetenv(L"STUDIO_TEST_SHORT_SECONDS")) : 30;  // 1.0.74 : Short de 30 s sur le refrain
            t.whisper_exe   = studio.paths().whisper;
            t.whisper_model = studio.paths().models_dir / "voix" / whisper_file().name;
            if (argc >= 7) {  // 6e paramètre : type de rendu (id de clip_styles) = scénario et images par scène
                SceneEngine eng;
                std::error_code ec3;
                eng.llm        = find_local_llm(app.app_dir());  // en ligne de commande, l'interface n'a pas encore cherché le moteur
                eng.llm_model  = app.ai_models_dir() / widen(app.settings.ai_local_model);
                auto size3     = fs::file_size(eng.llm_model, ec3);
                eng.llm_layers = eng.llm.vulkan ? ai_gpu_layers(ec3 ? 0 : (unsigned long long) size3, gpu_profile().gpu.vram) : 0;
                eng.sd_cli      = find_sd_cli(app.app_dir());
                eng.image_root  = app.image_models_dir();
                eng.image_model = &local_image_model(image_model_ready(app.image_models_dir(), local_image_model("klein")) ? "klein" : app.settings.ai_image_local);
                if (const wchar_t * im = _wgetenv(L"STUDIO_TEST_IMAGE_MODEL")) eng.image_model = &local_image_model(narrow(im));  // essai : autre moteur
                eng.offload     = App::image_offload(*eng.image_model);
                if (const wchar_t * n = _wgetenv(L"STUDIO_TEST_CLIP_IMAGES")) eng.scene_count = _wtoi(n);  // essai : nombre d'images voulu
                for (const ClipStyle & cs : clip_styles())
                    if (narrow(argv[6]) == cs.id) { eng.style_id = cs.id; eng.style_prompt = cs.prompt; }
                t.make_scenes = [eng](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out, std::string & note) {
                    return make_clip_scenes(eng, j, sg, lines, seconds, out, note);
                };
                t.make_scenario = [eng](Job & j, const Song & sg, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out, std::string & note) {
                    return make_clip_scenes(eng, j, sg, lines, seconds, out, note, true);
                };
            }
            auto job = std::make_shared<Job>();
            auto t0  = std::chrono::steady_clock::now();
            // Essai : images existantes (STUDIO_TEST_SCENES_DIR), lues sans toucher au scénario du morceau
            if (const wchar_t * dir = _wgetenv(L"STUDIO_TEST_SCENES_DIR")) {
                fs::path scene_dir = dir;
                t.make_scenes = [scene_dir](Job &, const Song &, const std::vector<ClipLine> &, double seconds, ClipScenes & out, std::string &) {
                    std::error_code ec;
                    for (const auto & e : fs::directory_iterator(scene_dir, ec))
                        if (e.path().filename().wstring().rfind(L"scene_", 0) == 0) out.images.push_back(e.path());
                    std::sort(out.images.begin(), out.images.end());
                    for (size_t k = 0; k < out.images.size(); k++) out.starts.push_back(seconds * k / out.images.size());
                    return !out.images.empty();
                };
            }
            std::vector<ClipTask> test_tasks = { t };
            if (fmt == L"all") {
                ClipTask tt = t, ig = t;
                t.output = std::wstring(argv[5]) + L"_yt.mp4";
                tt.output = std::wstring(argv[5]) + L"_tt.mp4";
                tt.width  = 1080;
                tt.height = 1920;
                ig.output = std::wstring(argv[5]) + L"_ig.mp4";
                ig.width  = ig.height = 1080;
                test_tasks = { t, tt, ig };
            }
            // Essai : un second morceau dans la même création (modèle d'images gardé chargé de l'un à l'autre)
            if (const wchar_t * second = _wgetenv(L"STUDIO_TEST_CLIP_SONG2")) {
                ClipTask t2 = t;
                for (const Song & x : studio.snapshot())
                    if (x.status == "done" && x.title == narrow(second)) t2.song = x;
                t2.output = std::wstring(argv[5]) + L"_2.mp4";
                test_tasks.push_back(t2);
            }
            start_clips(job, studio.paths(), test_tasks);
            while (job->state == 0) Sleep(500);
            std::wstring report = std::wstring(argv[5]) + L".txt";
            if (FILE * f = _wfopen(report.c_str(), L"wb")) {
                std::lock_guard<std::mutex> lock(job->mutex);
                fprintf(f, "trouve=%d etat=%d secondes=%.0f\n%s\n", (int) !found.id.empty(), (int) job->state,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), job->message.c_str());
                fclose(f);
            }
            studio.shutdown();
            return 0;
        }
        // Essai (1.0.57) : descriptions d'un album par l'IA locale, sans interface
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--desc-test") {
            std::vector<Song> all = studio.snapshot(), songs;
            std::string       key = album_key(narrow(argv[2]), narrow(argv[3])), err;
            for (const Song & s : album_songs(all, key, false))
                if (s.long_rest.empty()) songs.push_back(with_full_lyrics(s, all));
            LocalLlm llm;
            fs::path model;
            int      layers = 0;
            bool     ok     = app.local_text_ready(llm, model, layers);
            std::atomic<bool> cancel{ false };
            std::atomic<int>  written{ 0 }, stage{ 0 };
            AiCallResult      r = ok ? ai_local_session(llm, model, layers, &stage, &cancel, [&](const AiAsk & ask) {
                write_descriptions(songs, i18n::current(), ask, [](const std::string &) {}, cancel, written, err);
            }) : AiCallResult{};
            FILE * f = _wfopen(argv[4], L"wb");
            if (f) {
                fprintf(f, "morceaux=%d ready=%d ok=%d ecrits=%d erreur=%s %s\n", (int) songs.size(), (int) ok, (int) r.ok, written.load(), err.c_str(), r.error.c_str());
                fclose(f);
            }
            return 0;
        }
        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--cover-test") {
            std::string key = album_key(narrow(argv[2]), narrow(argv[3]));
            fs::path    png = app_temp_dir() / L"pochettes" / L"cover_test.png";
            std::error_code ec;
            fs::create_directories(png.parent_path(), ec);
            auto         t0 = std::chrono::steady_clock::now();
            // 1.0.50 : 7e paramètre facultatif = modèle local (« klein », « zimage », « moss »), sinon celui des réglages
            const LocalImageModel & m = local_image_model(argc >= 7 ? narrow(argv[6]) : app.settings.ai_image_local);
            AiCallResult r  = ai_image_local(find_sd_cli(app.app_dir()), app.image_models_dir(), m, narrow(argv[4]), 768, 768, png, nullptr,
                                             App::image_offload(m));
            double       s  = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            bool         installed = r.ok && app.install_cover(key, png);
            FILE *       f  = _wfopen(argv[5], L"wb");
            if (f) {
                fprintf(f, "ok=%d secondes=%.1f erreur=%s %s\nclé=%s\npochette=%s\n", (int) r.ok, s, r.error.c_str(), r.error_arg.c_str(), key.c_str(),
                        installed ? app.settings.covers[key].c_str() : "(non rangée)");
                fclose(f);
            }
            fs::remove(png, ec);
            return 0;
        }
    }
    if (const wchar_t * test_cover = _wgetenv(L"STUDIO_TEST_COVER")) {  // essais automatisés : fichier de reprise présélectionné
        app.form.cover         = test_cover;
        app.form.cover_seconds = media_duration(app.form.cover);
    }
    app.start_update_check();  // modèles et LoRA installés : y a-t-il une version plus récente ?
    app.start_app_update_check();  // Studio Audio AI : version plus récente publiée ? (rien si aucune adresse)
    app.startup_checks();          // 1.0.81 : sauvegarde du jour et fichiers manquants
    int       targc = 0;  // essai : compte rendu de la vérification (« --startup-test sortie.txt »)
    wchar_t ** targv = CommandLineToArgvW(GetCommandLineW(), &targc);
    if (targv && targc >= 3 && std::wstring(targv[1]) == L"--startup-test") {
        if (FILE * f = _wfopen(targv[2], L"wb")) {
            fprintf(f, "sauvegarde=%s nombre=%d manquants=%d\n", app.last_backup.c_str(), app.backup_count, (int) app.startup_issues.size());
            for (const std::string & s : app.startup_issues) fprintf(f, "%s\n", s.c_str());
            fclose(f);
        }
        return 0;
    }
    studio.set_fade_seconds(app.settings.fade_seconds);
    // Jauges de mémoire vidéo : la carte de calcul détectée (sur un portable, la carte Nvidia/AMD et non la puce
    // intégrée qui affiche la fenêtre) ; à défaut, celle de la fenêtre.
    if (gpu_profile().gpu.found) app.gpu_monitor.start(gpu_profile().gpu.luid, gpu_profile().gpu.vram);
    else app.gpu_monitor.start(g_adapter_luid, g_adapter_vram);

    // Boucle sobre : on ne redessine que sur événement, ou périodiquement pendant une génération/lecture.
    bool   done        = false;
    int    frames      = 3;
    double last_rescan = 0;
    while (!done) {
        if (frames <= 0) {
            bool  busy    = app.player.playing() || app.any_active;  // compte à rebours et progression
            DWORD timeout = busy ? 250 : 1000;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout, QS_ALLINPUT);
            frames = 2;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
            frames = 3;
        }
        if (done) break;

        // Commandes de l'icône de notification
        switch (g_tray_cmd) {
            case TRAY_OPEN:
                ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
                SetForegroundWindow(hwnd);
                break;
            case TRAY_RESUME_ALL: studio.resume_all(); break;
            case TRAY_HOLD_ALL: studio.hold_all(); break;
            case TRAY_QUIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
            case TRAY_READD: tray_remove(); break;  // recréée à l'image suivante
        }
        if (g_tray_cmd != TRAY_NONE) frames = 3;
        g_tray_cmd = TRAY_NONE;

        if (ImGui::GetTime() - last_rescan > 2.0) {
            studio.rescan();  // morceaux produits par la version web
            last_rescan = ImGui::GetTime();
        }
        if (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            // Fenêtre masquée : la pastille continue d'être tenue à jour.
            app.update_tray(studio.snapshot());
            app.update_games();
            frames = 0;
            continue;
        }
        g_occluded = false;
        if (g_resize_w != 0 && g_resize_h != 0) {
            cleanup_render_target();
            g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            create_render_target();
        }

        if (!app.pending_language.empty()) {
            app.apply_language();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        const float clear[4] = { P.bg.x, P.bg.y, P.bg.z, 1.f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_occluded = g_swapchain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
        frames--;
        // ImGui étale les clics rapides (appui + relâchement) sur plusieurs images : on continue
        // tant que sa file d'événements n'est pas vide, sinon un clic s'appliquerait plus tard, ailleurs.
        if (!ImGui::GetCurrentContext()->InputEventsQueue.empty()) frames = std::max(frames, 2);
    }

    if (app.form.dirty) save_draft(app.form, app.draft_path);
    for (auto & j : app.jobs) j->stop();
    app.jobs.clear();
    app.player.close();
    tray_remove();
    g_studio = nullptr;
    studio.shutdown();  // la génération en cours devient « interrompue », relançable

    release_thumbs();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    CoUninitialize();
    return 0;
}
