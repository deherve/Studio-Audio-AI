#pragma once
// 1.1.014 : types, fonctions libres (inline) et déclaration de App (fonctions dans app_*.cpp et main.cpp). Voir tools\modeles_version\split_main.py.
// Studio Audio AI (anciennement Studio YuE2), version native : interface Win32 + DirectX 11 + Dear ImGui, sans navigateur ni serveur web.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <commdlg.h>
#include <wincrypt.h>  // 1.0.92 : décodage base 64 des fichiers envoyés par l'Atelier partition
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
#include <deque>
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
#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode
#include "studio.hpp"
#include "journal.hpp"
#include "analysis.hpp"
#include "suno.hpp"
#include "fiches.hpp"
#include "score.hpp"
#include "soundfont.hpp"  // 1.0.89
#include "atelier.hpp"  // 1.0.91
#include "helpview.hpp"  // 1.1.005 : aide intégrée (disposition B)
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

inline const UINT WM_APP_NOTIFY = WM_APP + 1;
inline const UINT WM_APP_TRAY   = WM_APP + 2;
inline UINT       g_taskbar_created = 0;  // explorateur relancé : l'icône de notification doit être recréée

enum TrayCommand { TRAY_NONE, TRAY_OPEN, TRAY_RESUME_ALL, TRAY_HOLD_ALL, TRAY_QUIT, TRAY_READD };
inline int g_tray_cmd = TRAY_NONE;
inline std::vector<std::wstring> g_dropped;  // fichiers glissés sur la fenêtre (packs .yue2pack), traités à l'image suivante

// ---------------------------------------------------------------------------- Direct3D

inline ID3D11Device *           g_device      = nullptr;
inline ID3D11DeviceContext *    g_context     = nullptr;
inline IDXGISwapChain *         g_swapchain   = nullptr;
inline ID3D11RenderTargetView * g_rtv         = nullptr;
inline bool                     g_occluded    = false;
inline UINT                     g_resize_w    = 0, g_resize_h = 0;
inline Studio *                 g_studio      = nullptr;
inline bool                     g_want_close  = false;
inline bool                     g_force_close = false;
inline LUID                     g_adapter_luid{};
inline unsigned long long       g_adapter_vram = 0;

inline void create_render_target() {
    ID3D11Texture2D * back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

inline void cleanup_render_target() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

inline bool create_device(HWND hwnd) {
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

inline void cleanup_device() {
    cleanup_render_target();
    if (g_swapchain) g_swapchain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

// Texte traduit en UTF-16 (menus et boîtes de dialogue Windows).
inline std::wstring wtr(const char * key) { return widen(tr(key)); }
// Filtre de boîte de dialogue : « Description\0motifs\0Tous les fichiers\0*.*\0\0 » (c_str() ajoute le dernier zéro).
inline std::wstring file_filter(const std::wstring & label, const std::wstring & patterns) {
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

inline void show_tray_menu(HWND hwnd) {
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

inline float g_dpi = 1.f;  // échelle d'affichage (DPI du moniteur)

inline LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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
inline Palette P;

inline ImVec4 hex(unsigned rgb, float a = 1.f) {
    return ImVec4(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}

inline ImVec4 alpha(const ImVec4 & c, float a) {
    return ImVec4(c.x, c.y, c.z, a);
}

// Mélange d'une couleur dans le fond : teinte légère des blocs.
inline ImVec4 tint(const ImVec4 & base, const ImVec4 & color, float amount) {
    return ImVec4(base.x + (color.x - base.x) * amount, base.y + (color.y - base.y) * amount,
                  base.z + (color.z - base.z) * amount, 1.f);
}

inline bool windows_light_theme() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

// Thèmes graphiques : 0 = Minuit violet (par défaut ; suit le mode clair/sombre de Windows), puis des thèmes fixes.
inline int  g_theme        = 0;
inline bool g_system_light = false;
// 1.1.002 : trois ambiances des maquettes (une couleur d'action, trois couleurs d'état, le reste en gris) : Nuit, Console, Clair.
inline const char * THEME_NAMES[] = { N_("Minuit violet (par défaut)"), N_("Studio gris"), N_("Papier encre"), N_("Menthe clair"),
                                      N_("Nuit"), N_("Console"), N_("Clair") };
inline const char * THEME_HINTS[] = { N_("sombre violet (ou clair selon Windows)"), N_("gris neutre et dense, ambre"), N_("clair chaud, bleu encre, coins très arrondis"),
                                      N_("clair frais, turquoise"), N_("sombre sobre, une seule couleur d'action (menthe)"),
                                      N_("graphite chaud façon table de mixage, ambre"), N_("clair sobre pour le plein jour, bleu") };
inline const int    THEME_COUNT   = 7;

// Un thème est dark ou light ; vrai si la fenêtre doit prendre la barre de titre sombre.
inline bool theme_is_dark(int theme, bool system_light) {
    return theme == 1 || theme == 4 || theme == 5 || (theme == 0 && !system_light);
}

inline void apply_theme(bool light, float dpi) {
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
    } else if (theme == 4) {  // Nuit : sombre sobre ; couleurs des blocs adoucies pour que la menthe reste la seule couleur d'action
        P = { hex(0x141519), hex(0x1b1d22), hex(0x202329), hex(0xe6e7eb), hex(0x8d92a0), hex(0x2c2f36),
              hex(0x5fd6b8), hex(0x0d1f1a), hex(0x1d3a33), hex(0xff8a8a), hex(0x7fe3c9), hex(0x262930), hex(0xf4b860),
              hex(0x8fa8c8), hex(0xa79fc9), hex(0x7cc7b5), hex(0xd9b779), hex(0xc99ab0), hex(0xd5a183), hex(0x8cc9a2) };
    } else if (theme == 5) {  // Console : graphite chaud, ambre
        P = { hex(0x1a1714), hex(0x221e1a), hex(0x28231e), hex(0xefe8df), hex(0xa39888), hex(0x3a332c),
              hex(0xf2a541), hex(0x2a1700), hex(0x3b2a12), hex(0xff7b6b), hex(0x9bd17a), hex(0x2b2621), hex(0xf2a541),
              hex(0x9fb3c8), hex(0xb7a6c9), hex(0x8fc4b3), hex(0xe0b46a), hex(0xd6a0a8), hex(0xe39a6a), hex(0x9bd17a) };
    } else if (theme == 6) {  // Clair : plein jour, bleu
        P = { hex(0xf5f6f8), hex(0xffffff), hex(0xfbfcfd), hex(0x1d2129), hex(0x5d6573), hex(0xd9dde3),
              hex(0x2f7de1), hex(0xffffff), hex(0xe2edfb), hex(0xb42318), hex(0x1d7a52), hex(0xeef0f3), hex(0x9a5b00),
              hex(0x3b6ea8), hex(0x6b5fa8), hex(0x2a8577), hex(0x9a6a12), hex(0xa8507a), hex(0xb0602a), hex(0x1d7a52) };
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
    if (theme >= 4) { child_r = 8; frame_r = 6; popup_r = 8; tab_r = 5; }  // 1.1.002 : ambiances des maquettes
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
    // 1.1.003 : couleurs qu'ImGui laissait sombres dans les thèmes clairs (texte illisible) : barres de titre des fenêtres
    // (Réglages, Albums…), en-têtes et bordures de tableaux, poignée de redimensionnement.
    c[ImGuiCol_TitleBg]              = tint(P.panel, P.accent, 0.10f);
    c[ImGuiCol_TitleBgActive]        = tint(P.panel, P.accent, 0.18f);
    c[ImGuiCol_TitleBgCollapsed]     = P.panel;
    c[ImGuiCol_MenuBarBg]            = P.panel;
    c[ImGuiCol_TableHeaderBg]        = P.chip;
    c[ImGuiCol_TableBorderStrong]    = P.line;
    c[ImGuiCol_TableBorderLight]     = alpha(P.line, 0.6f);
    c[ImGuiCol_TableRowBgAlt]        = alpha(P.chip, 0.5f);
    c[ImGuiCol_ResizeGrip]           = alpha(P.accent, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = alpha(P.accent, 0.50f);
    c[ImGuiCol_ResizeGripActive]     = P.accent;
    c[ImGuiCol_SeparatorHovered]     = alpha(P.accent, 0.6f);
    c[ImGuiCol_SeparatorActive]      = P.accent;
}

// ---------------------------------------------------------------------------- Dégradé des jauges et barres de progression
// Toutes les jauges et barres de progression vont du vert (vide) au rouge vif (plein), en passant par le jaune
// puis l'orange : la couleur dépend de la position le long de la barre, pas de la valeur affichée.
inline const ImVec4 GAUGE_STOPS[] = {
    ImVec4(34 / 255.f, 197 / 255.f, 94 / 255.f, 1),   // vert
    ImVec4(234 / 255.f, 210 / 255.f, 8 / 255.f, 1),   // jaune
    ImVec4(249 / 255.f, 115 / 255.f, 22 / 255.f, 1),  // orange
    ImVec4(255 / 255.f, 0 / 255.f, 0 / 255.f, 1),     // rouge vif
};
// Couleur à la position t (0 = début de la barre, 1 = fin).
inline ImVec4 gauge_color(float t) {
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
inline void soft_wrap(std::string & s, float width) {
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

inline std::string wrap_word_list(const std::string & text, float width, int * cursor) {
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

inline void progress_bar(float fraction, const ImVec2 & size, const char * overlay = nullptr) {
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
inline const ChipGroup STYLE_GROUPS[] = {
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

// 1.1.011 : balises réellement proposées (Réglages › Styles) : celles de STYLE_GROUPS sans les retirées, plus les ajoutées.
// Reconstruites au début d'une image (rebuild_style_tags) : les pointeurs restent valables pendant toute l'image.
inline std::deque<std::string>      g_tag_store;
inline std::vector<const char *>    g_eff_items[5];
inline bool                         g_eff_on = false;
inline const std::vector<const char *> & group_items(const ChipGroup & g) {
    return g_eff_on ? g_eff_items[std::clamp(g.color, 0, 4)] : g.items;
}

inline ImVec4 category_color(int index) {
    const ImVec4 * colors[] = { &P.violet, &P.pink, &P.orange, &P.green, &P.blue };
    return *colors[std::clamp(index, 0, 4)];
}

// Balise qui parle de la voix (chant, timbre, chœur, langue chantée) : sans paroles, elle pousse le modèle à inventer du texte.
inline bool is_voice_tag(const std::string & tag) {
    auto low = [](std::string x) {
        for (auto & c : x) c = (char) tolower((unsigned char) c);
        return x;
    };
    std::string t = low(tag);
    if (t.rfind("no ", 0) == 0 || t.rfind("without ", 0) == 0) return false;  // 1.0.37 : « no vocals » est gardé
    for (const ChipGroup & g : STYLE_GROUPS) {
        if (g.color != 2) continue;
        for (const char * item : group_items(g)) if (t == low(item)) return true;
    }
    for (const char * w : { "vocal", "voice", "choir", "falsetto", "a cappella", "acapella", "spoken", "beatbox" }) {
        if (t.find(w) != std::string::npos) return true;
    }
    return false;
}

inline const char * SONG_TAGS[] = { "[Intro]", "[Verse]", "[Pre-Chorus]", "[Chorus]", "[Bridge]", "[Interlude]", "[Outro]" };
// En instrumental, la LoRA attend des balises en minuscules, une par ligne, sans texte.
inline const char * INST_TAGS[] = { "[intro]", "[verse]", "[pre-chorus]", "[chorus]", "[bridge]", "[outro]" };
inline const char * COT_LABELS[] = { N_("Complet (mélodie + accords)"), N_("Mélodie seule"), N_("Sans partition (plus rapide)") };
inline const char * COT_VALUES[] = { "full", "melody", "off" };
inline const int    COT_FASTEST  = 2;
inline const int    COT_DEFAULT  = 0;  // 1.0.87 : « Complet » par défaut (choix de Herve 5/11 : tonalité et tempo suivis, ≈ +28 % de temps)

// Structures courantes (formes pop, AABA, strophique, build/drop…), en balises comprises par le modèle.
struct Structure {
    const char *              name;
    std::vector<const char *> sections;
};
inline const Structure STRUCTURES[] = {
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

inline std::string structure_text(const Structure & st, bool instrumental) {
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
inline std::string structure_tags(const std::string & lyrics) {
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
inline std::string arrow_text(const std::string & tags) {
    std::string out;
    for (char c : tags) out += c == ',' ? std::string(" > ") : std::string(1, c);
    return out;
}

// Paroles vierges à partir d'une liste de sections (comme structure_text pour les structures types).
inline std::string skeleton_text(const std::string & tags, bool instrumental) {
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
    int         engine = 0, cot = COT_DEFAULT, duration = 180, versions = 1, language = 0;
    int64_t     seed = -1;
    fs::path    cover;  // fichier audio source d'une reprise
    double      cover_seconds = 0;
    int         cursor = -1;
    bool        dirty  = false;
    bool        pending_structure = false;  // structure à appliquer après confirmation
    std::string pending_text, pending_name;
};

inline void set_mode(Form & f, bool instrumental) {
    f.instrumental = instrumental;
    f.cot          = f.cover.empty() ? COT_DEFAULT : (instrumental ? 0 : 1);
    f.cursor       = -1;
    f.dirty        = true;
}

inline std::string write_json(yyjson_mut_doc * doc) {
    size_t      len  = 0;
    char *      json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);
    std::string out(json ? json : "", len);
    free(json);
    yyjson_mut_doc_free(doc);
    return out;
}

inline void load_draft(Form & f, const fs::path & path) {
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

inline void save_draft(Form & f, const fs::path & path) {
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
inline std::vector<std::string> default_negative_tags() {
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
    int                 layout     = 0;         // 1.1.002 : disposition de l'écran principal : 0 actuelle, 1 A (rangée)
    int                 mark_style = 1;         // marques validé / à problème : 0 pouces, 1 coche et croix (défaut 1.0.31, livrable de Herve), 2 commutateur
    int                 card_mode  = 0;         // cartes de la bibliothèque : 0 détaillée, 1 compacte, 2 pochette
    int                 style_mode = 0;         // bloc Style : 0 classique, 1 pastilles, 2 constructeur
    bool                save_group_style = false;  // 1.1.011 : enregistrer le style sous le nom du groupe à chaque morceau lancé
    std::vector<std::string> tag_added[5], tag_hidden[5];  // 1.1.011 : Réglages › Styles : balises ajoutées / retirées par catégorie
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
    std::string                        clip_karaoke = "0", key_check_auto = "0";  // 1.0.98
    std::string                        clip_end_pos = "0", clip_end_box = "0", clip_same_seed = "0";  // 1.0.98 : texte de fin, graine commune
    std::string                        soundfont_bank;  // 1.0.97 : fichier .sf2 choisi (modeles\sons\banques)
    std::string                        night_action = "0";  // 1.0.97 : à la fin de la file : 0 rien, 1 veille, 2 extinction
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

inline std::string json_str(yyjson_val * obj, const char * key) {
    yyjson_val * v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string();
}

inline bool preset_from_json(yyjson_val * item, Preset & p) {
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
inline void preset_to_json(yyjson_mut_doc * doc, yyjson_mut_val * arr, const Preset & p, bool with_pack) {
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

inline bool structure_from_json(yyjson_val * item, UserStructure & st) {
    st.name     = json_str(item, "name");
    st.sections = json_str(item, "sections");
    st.pack     = json_str(item, "pack");
    return !st.name.empty() && !st.sections.empty();
}

inline void structure_to_json(yyjson_mut_doc * doc, yyjson_mut_val * arr, const UserStructure & st, bool with_pack) {
    yyjson_mut_val * item = yyjson_mut_arr_add_obj(doc, arr);
    yyjson_mut_obj_add_strcpy(doc, item, "name", st.name.c_str());
    yyjson_mut_obj_add_strcpy(doc, item, "sections", st.sections.c_str());
    if (with_pack && !st.pack.empty()) yyjson_mut_obj_add_strcpy(doc, item, "pack", st.pack.c_str());
}

inline PackInfo pack_info_from_json(yyjson_val * o) {
    return { json_str(o, "name"), json_str(o, "author"), json_str(o, "version"), json_str(o, "description") };
}

inline void pack_info_to_json(yyjson_mut_doc * doc, yyjson_mut_val * o, const PackInfo & info) {
    yyjson_mut_obj_add_strcpy(doc, o, "name", info.name.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "author", info.author.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "version", info.version.c_str());
    yyjson_mut_obj_add_strcpy(doc, o, "description", info.description.c_str());
}

// Lit les clés présentes dans un objet JSON de réglages (les absentes gardent leur valeur) : sert pour chacun des
// fichiers de config\ (1.0.41) comme pour l'ancien donnees\studio_reglages.json qui les contenait toutes.
inline void parse_settings(Settings & s, yyjson_val * r) {
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
    v = yyjson_obj_get(r, "layout");
    if (yyjson_is_int(v)) s.layout = std::clamp((int) yyjson_get_int(v), 0, 2);
    v = yyjson_obj_get(r, "card_mode");
    if (yyjson_is_int(v)) s.card_mode = std::clamp((int) yyjson_get_int(v), 0, 3);
    v = yyjson_obj_get(r, "style_mode");
    if (yyjson_is_int(v)) s.style_mode = std::clamp((int) yyjson_get_int(v), 0, 2);
    v = yyjson_obj_get(r, "save_group_style");
    if (yyjson_is_bool(v)) s.save_group_style = yyjson_get_bool(v);
    for (int gi = 0; gi < 5; gi++) {
        for (int kind = 0; kind < 2; kind++) {
            auto &      list = kind ? s.tag_hidden[gi] : s.tag_added[gi];
            std::string key  = std::string(kind ? "tags_hidden_" : "tags_added_") + std::to_string(gi);
            yyjson_val * ta = yyjson_obj_get(r, key.c_str());
            if (!yyjson_is_arr(ta)) continue;  // le fichier est relu pour chaque fichier de réglages : ne rien effacer s'il n'a pas la clé
            list.clear();
            size_t       ti, tm;
            yyjson_val * tv;
            yyjson_arr_foreach(ta, ti, tm, tv) {
                if (yyjson_is_str(tv)) list.push_back(yyjson_get_str(tv));
            }
        }
    }
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
        { "clip_short_seconds", &s.clip_short_seconds }, { "night_action", &s.night_action }, { "soundfont_bank", &s.soundfont_bank }, { "clip_karaoke", &s.clip_karaoke }, { "key_check_auto", &s.key_check_auto },
        { "clip_end_pos", &s.clip_end_pos }, { "clip_end_box", &s.clip_end_box }, { "clip_same_seed", &s.clip_same_seed },  // 1.0.98
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
inline const std::pair<const char *, std::vector<const char *>> SETTINGS_FILES[] = {
    { "styles.json", { "presets", "structures", "packs" } },
    { "mots_a_eviter.json", { "ai_avoid", "ai_auto_avoid", "ai_recent" } },
    { "cles_api.json", { "ai_keys" } },
};

inline bool parse_settings_file(Settings & s, const fs::path & file) {
    std::string  text = read_text(file);
    yyjson_doc * doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return false;
    parse_settings(s, yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return true;
}


// path = config\reglages.json
// 1.1.011 : recalcule les balises proposées d'après les réglages (par défaut, moins les retirées, plus les ajoutées).
inline void rebuild_style_tags(const Settings & st) {
    g_tag_store.clear();
    for (int gi = 0; gi < 5; gi++) {
        g_eff_items[gi].clear();
        auto has = [](const std::vector<std::string> & v, const std::string & t) {
            for (const std::string & x : v) if (lower(x) == lower(t)) return true;
            return false;
        };
        for (const char * item : STYLE_GROUPS[gi].items) {
            if (!has(st.tag_hidden[gi], item)) g_eff_items[gi].push_back(item);
        }
        for (const std::string & t : st.tag_added[gi]) {
            bool dup = false;
            for (const char * e : g_eff_items[gi]) dup = dup || lower(e) == lower(t);
            if (dup) continue;
            g_tag_store.push_back(t);
            g_eff_items[gi].push_back(g_tag_store.back().c_str());
        }
    }
    g_eff_on = true;
}

inline void load_settings(Settings & s, const fs::path & path) {
    parse_settings_file(s, path);
    for (const auto & [name, keys] : SETTINGS_FILES) parse_settings_file(s, path.parent_path() / name);
}

inline void save_settings(const Settings & s, const fs::path & path) {
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
    yyjson_mut_obj_add_int(doc, o, "layout", s.layout);
    yyjson_mut_obj_add_int(doc, o, "card_mode", s.card_mode);
    yyjson_mut_obj_add_int(doc, o, "style_mode", s.style_mode);
    yyjson_mut_obj_add_bool(doc, o, "save_group_style", s.save_group_style);
    for (int gi = 0; gi < 5; gi++) {
        for (int kind = 0; kind < 2; kind++) {
            const auto & list = kind ? s.tag_hidden[gi] : s.tag_added[gi];
            if (list.empty()) continue;
            std::string      key = std::string(kind ? "tags_hidden_" : "tags_added_") + std::to_string(gi);
            yyjson_mut_val * ta  = yyjson_mut_arr(doc);
            yyjson_mut_obj_add(o, yyjson_mut_strcpy(doc, key.c_str()), ta);  // clé copiée : « key » disparaît à la fin du tour
            for (const std::string & t : list) yyjson_mut_arr_add_strcpy(doc, ta, t.c_str());
        }
    }
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
        { "clip_short_seconds", &s.clip_short_seconds }, { "night_action", &s.night_action }, { "soundfont_bank", &s.soundfont_bank }, { "clip_karaoke", &s.clip_karaoke }, { "key_check_auto", &s.key_check_auto },
        { "clip_end_pos", &s.clip_end_pos }, { "clip_end_box", &s.clip_end_box }, { "clip_same_seed", &s.clip_same_seed },  // 1.0.98
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
inline void migrate_settings(const fs::path & data, const fs::path & config) {
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


inline bool read_pack(const fs::path & path, PackFile & out, std::string & error) {
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

inline bool write_pack(const fs::path & path, const PackFile & pack) {
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

inline ImFont * g_bold   = nullptr;
inline ImFont * g_header = nullptr;
void build_fonts(bool runtime);  // définie avant wWinMain


inline std::string fmt_time(double seconds) {
    int  s = (int) std::lround(std::max(0.0, seconds));
    char buf[32];
    if (s >= 60) snprintf(buf, sizeof(buf), tr("%d min %02d s"), s / 60, s % 60);
    else snprintf(buf, sizeof(buf), "%d s", s);
    return buf;
}

inline std::string fmt_clock(int ms) {
    int  s = std::max(0, ms / 1000);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

inline void muted_wrapped(const std::string & text) {
    ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// Texte coloré qui revient à la ligne dans la largeur disponible (1.0.37 : indications jaunes qui dépassaient des cartes).
inline void colored_wrapped(const ImVec4 & color, const std::string & text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

inline void label(const char * text, const char * hint = nullptr) {
    ImGui::PushFont(g_bold);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    if (hint) {
        ImGui::SameLine();
        ImGui::TextColored(P.muted, "%s", hint);
    }
}

// Petite étiquette arrondie colorée (statut, origine du morceau…).
inline void pill(const char * text, const ImVec4 & color) {
    ImVec2       size = ImGui::CalcTextSize(text);
    ImVec2       pad(7 * g_dpi, 1 * g_dpi);
    ImVec2       p  = ImGui::GetCursorScreenPos();
    float        h  = ImGui::GetTextLineHeight() + pad.y * 2;
    ImDrawList * dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(size.x + pad.x * 2, h), ImGui::GetColorU32(alpha(color, 0.18f)), h / 2);
    dl->AddText(p + pad, ImGui::GetColorU32(color), text);
    ImGui::Dummy(ImVec2(size.x + pad.x * 2, h));
}

inline float pill_width(const char * text) {
    return ImGui::CalcTextSize(text).x + 14 * g_dpi;
}

// Repère des morceaux d'album (1.0.37). 1.0.46 : palette de 24 couleurs bien séparées (teintes espacées de 105°, trois
// réglages de saturation et de luminosité en alternance) ; chaque album présent reçoit une case libre de la palette
// (g_album_slots, attribuée par ancienneté de l'album dans refresh_album_dirs) : deux albums ne partagent une couleur
// qu'au-delà de 24 albums.
inline constexpr int                ALBUM_COLORS = 24;
inline std::map<std::string, int>   g_album_slots;  // clé d'album -> case de la palette

inline ImVec4 album_palette(int i) {
    static const float sv[3][2] = { { 0.62f, 0.96f }, { 0.88f, 0.82f }, { 0.42f, 1.00f } };
    ImVec4             c(0, 0, 0, 1);
    // 1.1.003 : couleurs plus foncées sur les thèmes clairs (sinon le badge « Album » est presque illisible sur fond blanc)
    const float v = theme_is_dark(g_theme, g_system_light) ? sv[i % 3][1] : 0.58f;
    ImGui::ColorConvertHSVtoRGB((float) ((i * 105) % 360) / 360.f, sv[i % 3][0], v, c.x, c.y, c.z);
    return c;
}

inline ImVec4 album_color(const std::string & key) {
    auto it = g_album_slots.find(key);
    return album_palette(it != g_album_slots.end() ? it->second : (int) (std::hash<std::string>{}(key) % ALBUM_COLORS));
}

// Pouce dessiné en vecteur (aucune police d'emoji nécessaire) : levé et vert pour « validé », baissé
// et rouge pour « à problème ». Éteint (gris estompé) tant qu'il n'est pas actif. Renvoie vrai au clic.
inline bool thumb_button(const char * id, bool active, float size, bool down, const char * tip_on,
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
inline const char * MARK_STYLES[] = { N_("Pouces (actuel)"), N_("Coche et croix"), N_("Commutateur À écouter / Validé / Problème") };

// Coche ou croix dessinée dans un cercle : pleine et colorée une fois active. Renvoie vrai au clic.
inline bool circle_mark(const char * id, bool active, bool cross, float size, const char * tip_on, const char * tip_off) {
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
inline bool title_zone(ImVec2 a, ImVec2 b, const std::string & tip) {
    if (!ImGui::IsMouseHoveringRect(a, b) || !ImGui::IsWindowHovered()) return false;
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s", tip.c_str());
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

inline float marks_width(int style, float size) {
    if (style != 2) return size * 2 + 4 * g_dpi;
    float pad = 9 * g_dpi, w = 0;
    for (const char * label : { tr("À écouter"), tr("Validé"), tr("Problème") }) w += ImGui::CalcTextSize(label).x + pad * 2;
    return w;
}

// Dessine les marques d'un morceau terminé au curseur. Renvoie : 0 rien, 1 « validé », 2 « à problème »,
// 3 « à écouter » (retire les deux marques).
inline int draw_marks(int style, bool validated, bool flagged, float size) {
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

inline bool accent_button(const char * text, const ImVec2 & size = ImVec2(0, 0), const ImVec4 * color = nullptr) {
    ImVec4 c = color ? *color : P.accent;
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(c, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(c, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text, P.accent_ink);
    bool clicked = ImGui::Button(text, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

inline bool soft_button(const char * text, const ImVec4 & color, bool compact = true) {
    ImGui::PushStyleColor(ImGuiCol_Button, alpha(color, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(color, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(color, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool clicked = compact ? ImGui::SmallButton(text) : ImGui::Button(text);
    ImGui::PopStyleColor(4);
    return clicked;
}

inline bool danger_button(const char * text) {
    return soft_button(text, P.danger);
}

// Blocs du formulaire repliables : l'état (identifiant des blocs repliés) est conservé entre les sessions.
inline std::set<std::string> g_collapsed;
inline bool                  g_collapsed_dirty = false;

// Bloc du formulaire : fond teinté, bordure et titre à la couleur du bloc. Un clic sur la ligne de titre le replie ou
// le déplie ; replié, il affiche un résumé (summary) à la place de l'indication. Renvoie vrai si le bloc est déplié.
inline bool begin_section(const char * id, const char * title, const ImVec4 & color, const char * hint = nullptr,
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

inline void end_section() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, 2 * g_dpi));
}

// Menu contextuel du clic droit pour les champs de texte (ImGui n'en fournit pas).
struct TextMenu {
    ImGuiID id = 0;
    int     a = 0, b = 0;  // sélection (octets UTF-8)
};
inline TextMenu g_text_menu;

// Menu du champ « graine » (clic droit) : coller une graine copiée dans la bibliothèque, la copier, ou revenir au tirage aléatoire.
inline bool seed_context_menu(int64_t & seed) {
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

inline bool text_context_menu(std::string & value) {
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
inline bool finish(bool r, std::string * s, ImGuiInputTextFlags flags) {
    if (flags & ImGuiInputTextFlags_ReadOnly) return r;
    bool m = text_context_menu(*s);
    return (flags & ImGuiInputTextFlags_EnterReturnsTrue) ? r : (r || m);
}
inline bool InputText(const char * label, std::string * s, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback cb = nullptr,
                      void * ud = nullptr) {
    return finish(ImGui::InputText(label, s, flags, cb, ud), s, flags);
}
inline bool InputTextWithHint(const char * label, const char * hint, std::string * s, ImGuiInputTextFlags flags = 0,
                              ImGuiInputTextCallback cb = nullptr, void * ud = nullptr) {
    return finish(ImGui::InputTextWithHint(label, hint, s, flags, cb, ud), s, flags);
}
inline bool InputTextMultiline(const char * label, std::string * s, const ImVec2 & size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0,
                               ImGuiInputTextCallback cb = nullptr, void * ud = nullptr) {
    return finish(ImGui::InputTextMultiline(label, s, size, flags, cb, ud), s, flags);
}
}  // namespace tx

// 1.0.48 : paroles d'un morceau long coupées au début de la section la plus proche de 1/parts des lignes
// (jamais avant la 2e section). Seconde partie vide si les paroles ont moins de deux sections.
inline std::pair<std::string, std::string> split_long_lyrics(const std::string & lyrics, size_t parts = 2) {
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
inline bool clear_button(const char * id, std::string & value) {
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
inline bool input_line(const char * id, const char * hint, std::string & value, float width = -1) {
    float w = (width < 0 ? ImGui::GetContentRegionAvail().x : width) - ImGui::GetFrameHeight() -
              ImGui::GetStyle().ItemSpacing.x * 0.5f;
    ImGui::SetNextItemWidth(w);
    bool changed = ImGui::InputTextWithHint(id, hint, &value);
    changed |= text_context_menu(value);
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    changed |= clear_button(id, value);
    return changed;
}

inline std::vector<std::string> split_tags(const std::string & style) {
    return split_style(style);
}

inline bool is_bpm_tag(const std::string & tag) {
    return tag.size() > 4 && lower(tag).compare(tag.size() - 3, 3, "bpm") == 0;
}

// Balises d'une catégorie, à sa couleur : présentes en surbrillance, un clic ajoute ou retire.
// Un seul tempo à la fois : choisir un BPM remplace le précédent.
inline bool style_chips(const std::vector<const char *> & items, std::string & style, const ImVec4 & color) {
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
inline int tag_row(const char * const * items, int count, const ImVec4 & color) {
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

inline int lyrics_callback(ImGuiInputTextCallbackData * data) {
    ((Form *) data->UserData)->cursor = data->CursorPos;
    return 0;
}

inline std::wstring save_dialog(HWND owner, const Song & song, const char * ext) {
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
inline HICON           g_icon = nullptr;
inline NOTIFYICONDATAW g_tray{};
inline HICON           g_tray_badge = nullptr;
inline std::string     g_tray_key;

inline HICON render_badge(int count, COLORREF color) {
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

inline void tray_gauges_remove();

inline void tray_remove() {
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
inline constexpr bool TRAY_SEPARATE_COUNT = false;

inline void tray_update(HWND hwnd, bool enabled, int active, int unheard, const std::string & tooltip) {
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
inline NOTIFYICONDATAW g_tray_gauge{}, g_tray_count{};
inline HICON           g_gauge_icon = nullptr, g_count_icon = nullptr;
inline std::string     g_gauge_key;

inline HICON render_gauges(double cpu, double gpu, double vram) {
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

inline void tray_gauges_remove() {
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

inline void tray_gauges_update(HWND hwnd, bool show, const GpuMem & m, int finished, const std::string & tip_gauges,
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
inline bool detect_game(const std::string & list, std::string & which) {
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
inline std::string amd_adrenalin_version() {
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
inline void check_driver_update(std::shared_ptr<DriverUpdate> up, std::string installed_cn) {
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
inline const char * MODEL_FILES_KNOWN[] = { "YuE2-3B-Q8_0.gguf", "YuE2-3B-Q6_K.gguf", "YuE2-3B-Q5_K_M.gguf" };
inline const char * MODEL_BASE_URL      = "https://huggingface.co/Serveurperso/YuE2-GGUF/resolve/main/";

// Adresse « dernière version » d'un fichier de LoRA du catalogue : la révision figée (/resolve/<sha>/) devient « main ».
inline std::string latest_url(const std::string & pinned) {
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
};

inline void check_remote_sizes(std::shared_ptr<RemoteSizes> rs, std::vector<std::pair<std::string, std::string>> items) {
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

inline std::string gpu_driver_version() {
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
inline std::map<std::wstring, Thumb> g_thumbs;

inline bool thumb_texture(const fs::path & file, ImTextureID & tex, ImVec2 & size, int max_px) {
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

inline void release_thumbs() {
    for (auto & [k, t] : g_thumbs) {
        if (t.srv) t.srv->Release();
    }
    g_thumbs.clear();
}

inline const char * SORTS[] = { N_("Plus récents"), N_("Plus anciens"), N_("Titre A → Z"), N_("N° de piste"), N_("Durée (longs d'abord)"),
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
    void apply_language();
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
    std::map<std::string, CustomLora> custom_loras;
    static CustomLora read_custom_lora(const fs::path & dir, const std::string & file, unsigned long long size);
    static long long json_stamp(const fs::path & dir, const std::string & file);
    const CustomLora * custom_lora(const std::string & file) const;
    std::string custom_label(const std::string & file) const;
    static const char * custom_kind(const CustomLora * c);
    static std::string custom_description(const CustomLora & c);
    std::string lora_trigger(const std::string & file) const;
    static constexpr const char *             INSTRUMENTAL_FILE = "ar_lora_inst_v3abc.bf16.safetensors";
    void scan_loras(bool force = false);

    struct LoraRef {
        const LoraEntry * entry = nullptr;
        const LoraFile *  file  = nullptr;
    };
    static LoraRef lora_ref(const std::string & file);
    static std::string lora_label(const std::string & file);
    bool lora_installed(const LoraEntry & e) const;
    static std::string fmt_mb(unsigned long long bytes);
    void style_trigger(const std::string & word, bool present);

    bool triggers_synced = false;
    void sync_lora_triggers();
    LoraUse * find_lora_use(const std::string & file);
    void set_lora(const std::string & file, bool on);
    static double structure_fade_seconds(const std::string & lyrics, bool instrumental);
    static bool is_new(const Song & s);
    void        mark_heard(const Song & s);
    void apply_mark(const Song & s, int code);
    std::vector<std::pair<std::string, double>> chosen_loras() const;

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
    std::vector<std::string> pack_groups(bool styles) const;
    int find_preset(const std::string & name, const std::string & pack) const;
    int find_structure(const std::string & name, const std::string & pack) const;
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
    fs::path ask_pack();
    void import_pack(const fs::path & path);
    std::string free_pack_name(const std::string & name) const;
    bool import_has_conflict() const;
    void finish_import();
    void delete_pack(const std::string & name, bool quiet = false);
    void begin_export();
    bool do_export();
    void draw_packs_settings();
    void draw_pack_modals();
    void draw_structure_modals();
    void update_imports();
    std::string preset_summary(const Preset & p) const;
    void apply_preset(const Preset & p);
    bool downloads_running() const;
    unsigned long long model_local_size(const std::string & file) const;
    bool model_update_available(const std::string & file);
    bool lora_update_available(const LoraEntry & e);
    void start_update_check();
    void start_model_update(const std::string & file);
    void start_lora_update(const LoraEntry & e);
    void update_downloads();
    void start_lora_download(const LoraEntry & e);
    fs::path analysis_dir() const;
    void     refresh_analysis();
    unsigned long long analysis_local_size(const AnalysisFile & f) const;
    unsigned long long analysis_total_bytes() const;
    int analysis_update_count();
    void start_analysis_download(bool update);
    void check_analysis_updates();
    void delete_analysis_files();
    fs::path whisper_dir() const;
    unsigned long long whisper_local_size() const;
    void refresh_whisper();
    bool whisper_update_available();
    void start_whisper_download(bool update);
    void delete_whisper_file();

    // ---- Langue chantée des morceaux de la bibliothèque, vérifiée à l'écoute (Whisper) à la demande
    std::atomic<bool> lang_running{ false }, lang_stop{ false };
    std::atomic<int>  lang_checked{ 0 }, lang_fixed{ 0 }, lang_total{ 0 }, lang_done_flag{ 0 };
    std::string busy_reason();
    int language_check_pending();
    void poll_language_check();
    void start_language_check();
    void start_analysis();
    static std::string analysis_lyrics(const AnalysisJob & job);
    static bool analysis_has_lyrics(const AnalysisJob & job);
    void apply_analysis(bool generate);
    void draw_suggestion_group(const char * title, std::initializer_list<const char *> groups, const ImVec4 & color);
    void draw_web_lyrics_status(const AnalysisJob & job);
    void draw_analysis_panel();

    GpuMonitor gpu_monitor;
    static std::string fmt_gb(unsigned long long bytes);

    // 1.0.67 : compteur de voiture : arc en dégradé vert → rouge, aiguille, valeur et nom. Valeurs relues toutes les
    // 2 secondes : l'aiguille rejoint la nouvelle valeur en douceur. limit : repère orange (GPU seulement, 100 = aucun).
    float gauge_anim[4] = { 0, 0, 0, 0 };
    float gauge_height() const;
    void draw_dial(int index, ImVec2 p, float w, float h, const char * label, const std::string & value, float fraction, int limit, const std::string & tip);

    // Quatre compteurs dans l'en-tête : mémoire vidéo utilisée, débordement vers la mémoire système,
    // utilisation du processeur (CPU) et de la carte graphique (GPU). Valeurs relues toutes les 2 secondes.
    // Moyenne glissante de l'utilisation GPU (16 dernières secondes) : avec une limite, un long morceau alterne des pas
    // de plusieurs secondes à 100 % et des pauses ; seule la moyenne montre l'effet de la limite.
    std::vector<std::pair<double, double>> gpu_history;
    double gpu_average(double now, double value);
    void draw_gauges(ImVec2 p, float width, float height);

    // Modèles YuE2-3B disponibles (Q8_0, Q6_K, Q5_K_M…) : le choix rapide de la barre du haut s'applique aux
    // prochains morceaux ; le modèle par défaut (Réglages) est celui pris à chaque démarrage.
    std::vector<std::string> models;
    std::string              current_model;
    static std::string model_label(const std::string & file);
    void init_models();
    // Solveur de la phase NAR : méthode + nombre de pas (voir solver_specs : AB3 16 … MP 32, du plus rapide au plus lent).
    std::string current_solver = "ab2_32";
    static const char * solver_label(const std::string & s);
    static std::string solver_key(const std::string & s);
    // ---- Recommandations selon le matériel (carte, mémoire vive, processeur, disque des modèles)
    SystemInfo sysinfo;  // lu au démarrage
    struct Advice {
        int         level;  // 0 bon, 1 conseil, 2 attention
        std::string text;
    };
    std::vector<Advice> build_advice(std::string & rec_model, std::string & rec_solver) const;
    void draw_advice();

    // ---- Vérification de la mémoire vidéo avant une génération
    bool                    open_vram = false;  // ouvrir l'avertissement « mémoire vidéo insuffisante »
    std::vector<GpuProcess> vram_hogs;          // programmes qui occupent la carte
    double                  vram_need = 0, vram_free = 0;
    bool vram_check_ok();
    void request_submit(bool held);
    void draw_vram_popup();
    void draw_gpu_limit();
    void draw_gpu_profile();
    void init_solver();
    void draw_solver_combo(const char * id, float width, bool as_default);
    void draw_model_combo(const char * id, float width, bool as_default);
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
    int clip_images_for(const Song & s);
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
    std::shared_ptr<UpdateInfo>   app_update    = std::make_shared<UpdateInfo>();
    std::string update_address() const;
    void start_app_update_check();
    void draw_version_badge();
    void draw_model_licences();
    void draw_update_settings();

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
    fs::path backups_dir() const;
    std::map<std::string, unsigned long long> model_files() const;
    void startup_checks();
    void draw_startup_issues();
    std::shared_ptr<RemoteSizes>  remote = std::make_shared<RemoteSizes>();  // fichiers à jour (modèles, LoRA)
    bool                          updates_announced = false;
    bool        driver_update_announced = false;
    double      last_health_seen = 0;
    static std::string wrap_style(const std::string & style, float width);
    void notify(const std::string & text);
    void clear_form();
    static bool has_lyrics_text(const std::string & lyrics);
    static int guess_language(const std::string & lyrics);
    void adapt(const std::string & title_block, const std::string & style_block, const std::string & lyrics_block, bool instrumental);
    void pick_cover();
    void submit(bool held);
    void reuse(const Song & s);
    void rerender(const Song & s, bool same_seed);
    void recompose(const Song & s);
    void draw_form_toolbar();

    // Tempo du style : curseur de 60 à 260 BPM, par pas de 1, sur une barre colorée par mouvement (Largo … Presto).
    // Il lit et écrit la balise « 120 BPM » du style (une seule à la fois) ; sans balise, le tempo est « non défini ».
    static constexpr int TEMPO_MIN = 60, TEMPO_MAX = 260;
    static const char * tempo_name(int bpm);

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
    fs::path app_dir() const;
    fs::path translate_dir() const;
    void refresh_translator(bool force = false);
    void test_translate_web();
    bool web_translation_possible() const;
    bool translate_lang_usable(const TranslateLang & l) const;
    void start_translation(const std::string & lyrics);
    void poll_translation();
    void draw_translate_row(std::string & lyrics);
    void draw_translate_row_inner(std::string & lyrics);
    void draw_translate_settings();
    void draw_translate_settings_inner();
    bool has_negative_tag(const std::string & tag) const;
    void remember_negative_tags(const std::string & field);
    bool draw_negative_chips(std::string * field, int owner);
    void draw_negative_tags_settings();
    void draw_negative_tags_settings_inner();
    void draw_negative_style();
    void draw_negative_style_inner();
    bool draw_tempo_slider(std::string & style);
    void draw_song_section();
    void draw_style_section();
    void draw_lyrics_section();
    void draw_lora_section();
    void draw_generation_section();
    void draw_generate_buttons();
    void draw_generation_options(bool open, std::string & lyrics);

    // ---- 1.0.88 : onglet « Partition ». Partition préparée par yue-plan d'après le formulaire « Nouveau morceau »
    // (style, paroles, mode), retouchée (transposition, tempo, texte), puis donnée au modèle pour générer le morceau.
    struct PlanJob {
        std::thread       thread;
        std::atomic<bool> done{ false };
        std::string       abc, error, label;
        double            started = 0, expected = 60;  // durée attendue pour la barre (s)
        bool              recognized = false;          // 1.0.89 : partition reconnue dans un enregistrement
        ~PlanJob() { if (thread.joinable()) thread.join(); }
    };
    std::unique_ptr<PlanJob> plan_job;
    std::string              score_abc, score_original, score_source;  // partition en cours, partition d'origine, d'où elle vient
    int                      score_mode = 0;                            // 0 Complet, 1 Mélodie seule
    int                      score_tempo_edit = 0;
    bool                     plan_busy() const;
    void start_plan();
    void poll_plan();
    void start_recognize(const fs::path & audio, const std::string & label);
    fs::path ask_audio_for_score();
    void open_abc_file();
    void use_song_score(const Song & s);
    void draw_score_tab();
    void draw_form();
    void draw_form_body();

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
    int                    form_tab_request     = -1;     // onglet du formulaire à sélectionner (0 Nouveau morceau, 1 IA, 2 Partition)
    std::string            submit_abc;                    // 1.0.88 : partition fournie pour le prochain submit()
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
    fs::path ai_models_dir() const;
    fs::path llama_dir() const;
    void refresh_local_llm(bool force = false);
    std::vector<std::string> ai_local_files() const;
    bool ai_model_installed(const AiModelFile & m) const;
    std::string ai_model_name(const std::string & file) const;
    AiFit ai_fit(unsigned long long bytes) const;
    bool ai_engine_ready(std::string & why, std::string & label);
    std::string ai_service_model(const std::string & id);
    AiEndpoint ai_endpoint(const std::string & id);
    AiRequest ai_request() const;
    bool ai_local_busy() const;
    std::vector<std::string> ai_auto_words() const;
    std::vector<std::string> ai_avoid_words() const;
    void remember_ai_song(const AiSong & s, bool instrumental);
    std::string ai_summary() const;
    std::string ai_album_summary() const;
    void start_ai_compose();
    void poll_ai_job();
    std::string ai_song_style(const std::string & raw) const;
    void fill_form_with(Form & f, const AiSong & s, bool inst);
    void send_ai_song();

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
    void open_ai_style_save(const std::string & style, const std::string & name);
    void open_song_style_save(const Song & s);
    std::string ai_style_negative() const;
    std::vector<LoraUse> ai_style_loras() const;
    bool personal_preset_exists(const std::string & name) const;
    void store_ai_style(bool replace);
    void auto_group_style(const std::string & group, const std::string & style);
    void draw_group_style_checkbox();
    void open_group_style(const std::vector<Song> & all, const std::string & artist);

    // ---- 1.1.011 : Réglages › Styles : ajouter / retirer des balises de Genres, Ambiance, Voix, Instruments, Son
    bool        tags_dirty = true;  // la liste effective est refaite au début de l'image
    std::string style_tag_new[5];
    static bool tag_in(const std::vector<std::string> & v, const std::string & t);
    static void tag_erase(std::vector<std::string> & v, const std::string & t);
    void style_tags_changed();
    bool style_tags_customized() const;
    bool style_tag_chip(const std::string & text, const ImVec4 & color, float avail, float & x, bool first, int id);
    void draw_style_tags_settings();
    void draw_ai_style_modal();
    fs::path last_album_file() const;
    void save_last_album() const;
    void load_last_album();

    // ---- Pochettes générées (1.0.36)
    // Une tâche = un emplacement d'image (pochette 1:1 = clé d'album, « #youtube » 16:9, « #tiktok » 9:16) et sa description.
    struct CoverTask {
        std::string slot, prompt, label;
        int         w = 768, h = 768;
        std::string model;  // 1.0.51 : modèle local choisi pour cette fois (« klein », « zimage », « moss ») ; vide = Réglages
        fs::path    reference;  // 1.1.016 : image de référence du groupe (vide = aucune)
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
    fs::path    image_models_dir() const;
    std::string image_service_model(const std::string & id) const;
    AiEndpoint image_endpoint(const std::string & id);
    bool image_engine_ready(std::string & why, std::string & label);
    static bool image_offload(const LocalImageModel & m);
    bool image_model_menu(std::string & model);
    static void cover_size(const std::string & slot, int & w, int & h);
    std::string default_cover_prompt(const std::string & album, const std::string & style) const;
    std::string cover_prompt_for(const std::string & key, const std::string & album, const std::string & style) const;

    // 1.0.43 : dossier et nom (sans extension) de l'image d'un emplacement. Album : son dossier sous Audios
    // (pochette, pochette_youtube, pochette_tiktok) ; morceau sans album (« titre:<id> ») : son dossier ; sinon
    // donnees\pochettes avec un nom tiré de l'emplacement (album pas encore connu).
    std::map<std::string, fs::path> album_dirs;
    void refresh_album_dirs(const std::vector<Song> & songs);
    fs::path cover_dir(const std::string & slot, std::wstring & name) const;
    void migrate_covers();
    bool install_cover(const std::string & slot, const fs::path & img);
    bool local_text_ready(LocalLlm & llm, fs::path & model, int & layers);
    void start_descriptions(const std::string & key, const std::vector<Song> & all);
    void poll_descriptions();
    void start_covers(std::vector<CoverTask> tasks);
    void poll_covers();
    void draw_cover_progress();
    void draw_cover_splash();
    std::string album_image_prompt(const std::string & key, const std::string & album, const std::string & artist);
    std::vector<CoverTask> album_cover_tasks(const std::string & key, const std::string & album, const std::string & prompt, bool youtube, bool tiktok, bool skip_existing, int & skipped);
    AiAlbumRequest ai_album_request() const;
    static std::string bare_title(std::string t);
    int band_album_last_track(const std::string & band, const std::string & album) const;
    void add_band_history(AiAlbumRequest & r) const;
    void start_ai_album();

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
    void auto_log(const std::string & line);
    void auto_album_step();
    void poll_ai_album();
    bool form_has_content(bool instrumental) const;
    std::string ai_song_text(const AiSong & s, bool instrumental) const;
    std::string ai_text_for_parse(const std::string & text) const;
    void draw_ai_tab();
    void draw_ai_tab_inner();
    void start_ai_model_download(const AiModelFile & m);
    void start_llama_download();
    bool finish_ai_download(DownloadJob & j);
    void poll_llama_extract();
    void ai_engine_radio(const char * label, const std::string & id);
    void draw_llama_download();
    void draw_ai_settings();
    void draw_ai_settings_inner();
    bool draw_ai_model_picker(const std::string & target, std::string & model);
    void draw_player(const Song & s);

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
    static ImVec4 status_color(const Song & s);
    static const char * status_label(const Song & s);
    void draw_song_stats(const Song & s, const ImVec4 & color);
    void draw_song(const Song & s, int queue_pos, bool collapsible = false);

    // ------------------------------------------------------------------ Affichages de carte : compacte, pochette

    std::set<std::string> expanded_cards;
    static std::string ellipsize(const std::string & text, float width);
    void toggle_play(const Song & s);
    void export_song(const Song & s, const char * format);
    void draw_song_menu(const Song & s);
    void draw_song_actions_c(const Song & s);
    std::string song_summary(const Song & s, bool with_lang = true) const;

    // ---- 1.0.87 : partition ABC écrite par le modèle (score.abc), lue pour un résumé lisible : tonalité, tempo, mesure,
    // accords de chaque section (dans l'ordre, sans répétition consécutive).
    mutable std::map<std::string, std::pair<double, ScoreInfo>> score_cache;
    const ScoreInfo & score_info(const Song & s) const;
    static std::string key_name(const std::string & key);
    std::string score_view_id;  // morceau dont la partition est affichée
    bool        open_score_view = false;
    bool        score_test_done = false;
    bool        atelier_test_done = false;
    // ---- 1.0.91 : Atelier partition (fenêtre WebView2 + abcjs). abc = partition ; name = nom de fichier proposé ; dir = dossier
    // proposé pour les exports (dossier du morceau, sinon vide).
    fs::path atelier_dir;
    double   atelier_ready_at = 0;  // page de l'atelier prête (essai PDF)
    bool     atelier_pdf_done = false, atelier_export_done = false, atelier_import_done = false;
    void open_atelier(const std::string & abc, const std::string & title, const std::string & subtitle, const std::string & name, const fs::path & dir, const std::string & lyrics = std::string());
    void atelier_message(const std::string & m);
    fs::path ask_export_path(const std::string & name, const std::string & ext);

    // ====================================================================== 1.0.97 : outils audio
    std::vector<Song>    tool_songs;        // morceaux visés par la fenêtre « Mastering »
    std::string          tool_label;
    bool                 open_master = false, master_mp3 = true;
    int                  master_preset = 0;
    std::shared_ptr<DescJob> pub_job;       // fiches de publication en cours (IA locale)
    std::vector<std::string> pub_ids;       // morceaux de la dernière série de fiches
    bool                 open_pub = false;
    int                  pub_sel = 0;
    std::string          pub_text;
    std::shared_ptr<Job> dup_job;
    std::shared_ptr<std::vector<DupPair>> dup_found;
    bool                 open_dup = false, dup_shown = true;
    struct TrialJob {
        std::vector<Song>        songs;
        std::vector<std::string> problems;
        int                      best = 0;
        std::atomic<bool>        finished{ false };
    };
    std::vector<std::vector<std::string>> trial_groups;   // versions d'un même envoi (essais), en attente de leur fin
    std::vector<std::shared_ptr<TrialJob>> trial_jobs;
    bool                 night_busy = false;  // la file a travaillé depuis le dernier arrêt automatique
    double               night_deadline = 0;  // heure ImGui de l'arrêt programmé ; 0 = aucun
    int                  night_done = 0, night_failed = 0;
    double               night_start = 0;
    std::vector<Song> done_songs_of(const std::vector<Song> & all, const std::string & key);
    bool stems_engine(fs::path & exe, fs::path & model) const;
    void tool_started(const std::string & what);
    bool stems_dl_wait = false;
    void poll_stems_download();
    void draw_tools_menu(const std::vector<Song> & songs, const std::string & label, bool album);
    void start_publication(const std::vector<Song> & songs);
    void load_pub_text();
    void poll_publication();
    void draw_pub_modal();
    void draw_master_modal();
    void start_dups(const std::vector<Song> & all);
    void draw_dup_modal();
    void poll_trials();
    void poll_night();
    void draw_night_modal();

    // ====================================================================== 1.0.98
    // ---- Aide interrogeable : les rubriques de l'aide intégrée les plus proches de la question sont données à l'IA locale
    struct HelpSection { std::string title, id, text; };
    std::vector<HelpSection> help_sections;
    std::string              help_loaded_lang;
    bool                     open_helpq = false;
    std::string              help_q, help_answer, help_error;
    std::vector<int>         help_hits;
    struct HelpJob {
        std::atomic<bool> finished{ false }, cancel{ false };
        std::atomic<int>  stage{ 0 };
        std::string       text, error;
    };
    std::shared_ptr<HelpJob> help_job;
    static std::string fold_text(const std::string & s);
    fs::path help_page_path();
    void load_help_sections();
    void start_help_question();
    void poll_help();
    std::wstring help_url(const std::string & anchor = std::string());
    bool help_focus = false;
    void draw_help_page();
    void draw_help_question();

    // ---- Espace disque et ménage (Réglages › Système) : tailles, puis envoi à la Corbeille sur confirmation
    struct DiskItem {
        std::string           label;
        std::vector<fs::path> paths;
        unsigned long long    bytes = 0;
        bool                  deletable = false, checked = false;
    };
    struct DiskScan {
        std::atomic<bool>     finished{ false };
        std::vector<DiskItem> items;
    };
    std::shared_ptr<DiskScan> disk_scan;
    std::vector<DiskItem>     disk_items;
    bool                      disk_confirm = false;
    std::string               disk_msg;
    static unsigned long long tree_bytes(const fs::path & p);
    void start_disk_scan();
    static std::string fmt_bytes(unsigned long long b);
    void draw_disk_panel();

    // ---- Contrôle tonalité et tempo : SheetSage2 réécoute le morceau et compare à la partition (ou au BPM du style)
    struct KeyJob {
        std::atomic<bool>        finished{ false }, cancel{ false };
        std::atomic<int>         done{ 0 };
        int                      total = 0;
        std::mutex               mutex;
        std::string              report, error, details;
        std::vector<std::string> flagged;
    };
    std::shared_ptr<KeyJob> key_job;
    static int style_bpm(const std::string & style);
    void start_key_check(const std::vector<Song> & songs, const std::string & label);
    void poll_key_check();

    // ---- Mise à jour en un clic : téléchargement de la release GitHub, empreinte SHA-256, sauvegarde, copie des programmes
    // sans toucher aux réglages (config), aux morceaux (Audios), aux modèles et aux LoRA (modeles), puis relance.
    struct UpdateJob {
        std::atomic<unsigned long long> done{ 0 }, total{ 0 };
        std::atomic<int>                state{ 0 };  // 0 en cours, 1 prêt à installer, 2 erreur
        std::atomic<bool>               cancel{ false };
        std::string                     error, extracted;
    };
    std::shared_ptr<UpdateJob> update_job;
    bool                       update_confirm = false;
    static std::string sha256_file(const fs::path & file);
    void start_update_download(const std::shared_ptr<UpdateInfo> & info);
    bool launch_update_install(const std::string & src_dir, const std::string & from_version, std::string & error);
    void draw_update_panel();

    // ---- 1.0.97 : partition de tous les instruments : SheetSage2 transcrit chaque stem (voix, autres, basse) ; les portées sont assemblées
    // mesure par mesure dans « score_stems.abc » et ouvertes dans l'Atelier partition (toute note modifiable). La batterie est ignorée.
    struct StemScoreJob {
        std::atomic<bool> finished{ false };
        std::atomic<int>  done{ 0 };
        std::mutex        mutex;
        std::string       id, abc, notes, error;
    };
    std::shared_ptr<StemScoreJob> stem_score_job;
    bool song_has_stems(const Song & s) const;
    void open_stem_score(const Song & s, const std::string & abc);
    void start_stem_score(const Song & s);
    void poll_stem_score();

    // ====================================================================== 1.0.97 : sons de l'Atelier partition
    // Instruments (échantillons FluidR3 GM en mp3, MIT) téléchargés dans ressources\partition\sons ; banques SoundFont 2 dans modeles\sons\banques
    // pour la lecture et l'export audio de qualité (TinySoundFont). Tout est téléchargé au clic de l'utilisateur.
    struct SoundInst { const char * id; const char * label; int program; };
    static const std::vector<SoundInst> & sound_instruments();
    fs::path sons_dir() const;
    bool     instrument_installed(const std::string & id) const;
    std::set<std::string> sound_dl_wait;
    void download_instrument(const std::string & id);
    void poll_sound_downloads();
    struct SoundBank { const char * id; const char * label; const char * url; const char * file; unsigned long long bytes; const char * license_url; const char * license_file; };
    static const std::vector<SoundBank> & sound_banks();
    fs::path bank_dir() const;
    fs::path current_bank() const;
    void download_bank(const SoundBank & b);
    struct RenderJob {
        std::atomic<bool> finished{ false };
        bool              ok = false, play = false;
        std::string       error;
        fs::path          out;
    };
    std::shared_ptr<RenderJob> render_job;
    int                        render_seq = 0;
    void poll_render();
    static std::string decode_b64(const std::string & data);
    void start_render(const std::string & midi_b64, bool play, const std::string & name);
    void draw_sound_panel();

    // 1.0.95 : fenêtre « Paroles et structure » d'un morceau (texte sélectionnable, Ctrl+C, clic droit, boutons de copie).
    std::string lyrics_view_id;
    bool        open_lyrics_view = false;
    static std::string lyrics_structure(const std::string & lyrics);
    void lyrics_box(const char * id, const std::string & text, float h);
    void draw_lyrics_view();
    static const char * lyrics_button_label(const Song & s, bool compact);
    float lyrics_button_width(const Song & s) const;
    // ====================================================================== 1.1.006 : refaire à partir d'ici
    // Le début du morceau est gardé (codes du moteur jusqu'au point choisi), tout ce qui suit est recomposé. Pour une chanson,
    // le point est le début d'une section, calé par les paroles synchronisées (paroles.lrc, Whisper) ; pour un instrumental,
    // une seconde choisie au curseur. Les paroles données au moteur sont celles des sections à partir de la partie refaite.
    struct RedoSection {
        std::string name;   // nom de la section, avec son numéro d'ordre (« Chorus 2 »)
        double      start;  // début de sa première ligne chantée (s)
        int         line;   // indice de cette ligne parmi les lignes chantées
    };
    static std::string redo_lyrics_text(const Song & s);
    static std::vector<RedoSection> redo_sections(const Song & s);
    static std::string redo_lyrics_from(const std::string & text, int line);
    std::string redo_id;            // morceau à refaire (fenêtre ouverte)
    bool        open_redo = false;
    int         redo_pick = -1;     // section choisie
    int         redo_pick_test = -1;  // essai (STUDIO_TEST_SHOT « redopick ») : section choisie à l'ouverture
    float       redo_seconds = 30;  // instrumental : point choisi au curseur
    std::shared_ptr<Job> redo_lrc_job;
    static double redo_cut(double section_start);
    void draw_redo_modal();
    void ask_redo(const Song & s);
    void lyrics_button(const Song & s, bool compact = false);
    void draw_score_view();
    float clip_logos_width(const Song & s) const;
    void draw_clip_logos(const Song & s);
    void album_badge(const Song & s);
    void album_stripe(const Song & s);
    void draw_song_compact(const Song & s);
    static std::string cover_key(const std::string & id, const std::string & artist, const std::string & album);
    void draw_cover_art(const std::string & key, const std::string & title, float side);
    void draw_song_cover_compact(const Song & s);
    void draw_song_cover(const Song & s, bool details = false, bool collapsible = false);
    bool passes_filters(const Song & s) const;

    bool queue_open = false;
    void draw_queue_bar(const std::vector<Song> & songs);
    void draw_filters(const std::vector<Song> & songs);
    void draw_library(const std::vector<Song> & songs);
    void update_games();
    fs::path playable_file(const std::string & id) const;
    void play_song(const std::string & id);

    // Enchaîne le morceau suivant de la liste affichée quand celui en cours se termine.
    // 1.0.46 : lecture des nouveaux (morceaux terminés pas encore écoutés), dans l'ordre affiché.
    bool                     new_mode = false;
    std::vector<std::string> new_queue;
    void start_new_playback(const std::vector<const Song *> & shown);
    void next_new_song();
    void update_autoplay();

    // 1.0.83 : contrôle qualité (un album, ou toute la bibliothèque si key est vide)
    std::shared_ptr<Job>                          quality_job;
    std::shared_ptr<std::vector<QualityFinding>> quality_found;
    void start_quality_check(const std::vector<Song> & all, const std::string & key, const std::string & label);
    void update_quality();
    void update_jobs();
    void update_health();
    fs::path pochettes_dir() const;

    // Existence d'un fichier, mémorisée 3 secondes (1.0.30) : les cartes de la bibliothèque la demandent à chaque image
    // (pochettes, codes du re-rendu) ; sans ce cache, plusieurs accès disque par carte et par image.
    // 1.1.013 : sous Audios, la réponse reste valable tant que Windows ne signale aucun changement dans ce dossier (fs_gen,
    // avancé par la boucle principale, au plus toutes les 3 s) et 60 s au plus ; avant, tout était revérifié toutes les 3 s :
    // ≈ 1 800 accès disque d'un coup pour 148 morceaux, soit une image saccadée toutes les 3 s dans la bibliothèque.
    // Ailleurs (ou sans surveillance possible) : 3 secondes, comme avant.
    unsigned     fs_gen = 0;        // avancé quand le dossier Audios a changé
    std::wstring fs_watch_root;     // dossier surveillé, en minuscules, avec « \ » final ; vide = pas de surveillance
    struct FsEntry { int value; double at; unsigned gen; bool watched; };
    bool fs_entry_valid(const FsEntry & e, double now) const;
    bool fs_watched(const std::wstring & path) const;
    mutable std::map<std::wstring, FsEntry> exists_cache;
    bool cached_exists(const fs::path & p) const;

    // 1.0.51 : nombre de vidéos (.mp4) d'un dossier, mémorisé 3 secondes comme cached_exists (voyants de la fenêtre Albums).
    mutable std::map<std::wstring, FsEntry> video_cache;
    int video_count(const fs::path & dir) const;
    std::string album_open;          // 1.0.51 : album déplié dans la fenêtre Albums (un seul à la fois)
    int         album_tab     = 0;   // onglet de la fiche dépliée : 0 = Pochettes, 1 = Vidéos
    bool        album_scroll  = false;  // essai : faire défiler la liste jusqu'à l'album déplié
    int         album_tab_request = -1; // essai : onglet à sélectionner ; album_menu_test : ouvrir le menu des modèles
    bool        album_menu_test   = false;
    float       prompt_wrap_w = 0;
    std::string cover_for(const std::string & key) const;
    fs::path ask_image(const wchar_t * title);
    fs::path ask_save(const std::wstring & name, const wchar_t * label, const wchar_t * ext);
    static std::string slot_youtube(const std::string & key);
    static std::string slot_tiktok(const std::string & key);
    bool set_image(const std::string & slot, const wchar_t * title);
    std::shared_ptr<Job> new_job(const std::string & name);
    void open_video_dialog(const std::vector<Song> & songs, const std::string & title, const std::string & key, bool clip = false);
    void compute_video_recs(const std::vector<Song> & songs);
    void open_video_dialog_images(const std::vector<Song> & songs, const std::string & key, bool clip);
    std::vector<Song> songs_missing_short(const std::vector<Song> & all, const std::string & key) const;
    void start_missing_shorts(const std::vector<Song> & songs, const std::string & label);
    std::vector<Song> shorts_pending;  // série en attente de confirmation
    std::string       shorts_label;
    bool              shorts_open = false;
    bool              shorts_test = false;
    std::string       quality_test;  // essai STUDIO_TEST_ALBUMS=qc:<clé>
    bool              quality_no_flag = false;
    void draw_shorts_confirm();
    void ask_missing_shorts(const std::vector<Song> & all, const std::string & key, const std::string & label);
    void create_videos();
    void draw_video_network(const char * label, const char * format, bool & enabled, fs::path & img, const std::string & slot, float max_w, float max_h);
    void draw_video_modal();
    void export_album(const std::string & key, const std::string & artist, const std::string & album, const std::vector<Song> & all);
    void draw_jobs();

    // Albums : regroupement par groupe + album, pochette, export, vidéo.
    struct AlbumInfo {
        std::string key, artist, album;
        int         done = 0, validated = 0;
        int         pending = 0;  // 1.0.36 : morceaux en file ou en pause (album « en attente »)
        std::string style;        // style d'un de ses morceaux (description de pochette par défaut)
    };
    std::vector<AlbumInfo> list_albums(const std::vector<Song> & songs) const;
    void draw_image_slot(const std::string & slot, const char * label, float w, float h, const std::string * prompt = nullptr, const std::string & album = std::string());

    // ---- 1.0.46 : aperçu agrandi d'une image, toutes les pochettes d'un album, suppression d'un album
    fs::path               preview_image;
    bool                   preview_open = false, all_covers_open = false, delete_album_open = false;
    static std::string test_clean_style(const std::string & style);
    float                  test_scroll = 0;         // essai : défilement des Réglages pour les captures de l'aide
    int                    test_scroll_frames = 0;
    std::vector<CoverTask> all_covers_tasks;
    std::string            all_covers_key, all_covers_label, delete_album_key, delete_album_artist, delete_album_name;
    void delete_album();
    void draw_album_tool_modals();

    // 1.0.41 : renommer le groupe et/ou le titre d'un album. Étiquettes des morceaux réécrites (edit_metadata),
    // pochettes, images des vidéos, description de pochette et dernier album suivent la nouvelle clé.
    std::string rename_key, rename_artist, rename_title;
    bool        rename_open = false;
    void rename_album(const std::string & old_key, const std::string & artist, const std::string & album);
    void draw_rename_album_modal();
    void draw_albums_modal();
    void draw_albums_body(bool page);

    // Toute suppression définitive de fichier passe par ici (1.0.32) : refusée hors du dossier du programme, pour
    // qu'une copie du Studio n'efface jamais les modèles ou LoRA d'une autre (incident de la 1.0.31).
    bool remove_refused = false;
    bool remove_inside(const fs::path & p, std::error_code & ec);
    void path_line(const fs::path & p);
    void delete_model_file(const std::string & file);
    void delete_lora_files(const std::vector<std::string> & files, const std::string & name);
    bool delete_button(const char * tip);
    void draw_lora_catalog();
    void open_help();
    void draw_whisper_settings();
    void draw_analysis_settings();
    void begin_card(const ImVec4 & color, const char * id);
    void end_card();
    ImVec4 card_color;
    void draw_settings_modal();
    void draw_settings_body(bool page);
    void sort_shown(std::vector<const Song *> & v) const;
    void draw_sort_bar();
    bool modal_buttons(const char * ok_label, bool ok_enabled, const ImVec4 * ok_color = nullptr);
    void draw_modals();
    void update_tray(const std::vector<Song> & songs);

    // Disposition B (1.1.003) : menu de gauche. Pages : création (onglets Nouveau morceau / Composer avec l'IA / Partition)
    // et bibliothèque ; Albums, Réglages et l'aide gardent leurs fenêtres.
    int b_page     = 1;  // 0 création, 1 bibliothèque, 2 réglages, 3 aide (1.1.005)
    int lib_tab    = 0;  // page bibliothèque : 0 morceaux, 1 albums (1.1.005)
    int lib_tab_request = -1;  // onglet à sélectionner (menu de gauche, « Albums… »)
    int settings_cat = 0;  // catégorie des Réglages (fenêtre ou page)
    static constexpr const char * SET_CATS[] = { N_("Interface"), N_("Génération"), N_("Analyse et paroles"), N_("LoRA"), N_("Système"), N_("Bibliothèque"), N_("IA (paroles)"), N_("Styles") };
    static constexpr int SET_CAT_COUNT = 8;
    int form_shown = 0;
    void draw_nav(const std::vector<Song> & songs);
    void draw_fixed_player(const std::vector<Song> & songs, float height);

    // 1.1.004 : changement de thème appliqué ici, hors de toute fenêtre. Appliqué au milieu d'une image (Réglages ouverts),
    // les PopStyleColor en cours remettaient les anciennes couleurs (fonds sombres restés sur un thème clair).
    bool theme_pending = false;
    void frame();
};
