// Carte graphique du PC et profil de calcul du moteur (voir gpuinfo.hpp).
#include "gpuinfo.hpp"

#include <dxgi.h>

#include <cstdio>
#include <string>

#include "studio.hpp"  // app_temp_dir

// Identifiants PCI des fabricants (champ VendorId de DXGI).
static const UINT VENDOR_AMD       = 0x1002;
static const UINT VENDOR_NVIDIA    = 0x10DE;
static const UINT VENDOR_INTEL     = 0x8086;
static const UINT VENDOR_MICROSOFT = 0x1414;  // « Microsoft Basic Render Driver » (carte logicielle)

// Texte UTF-16 de Windows -> UTF-8.
static std::string to_utf8(const wchar_t * w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

GpuInfo detect_gpu() {
    GpuInfo         best;
    IDXGIFactory1 * factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **) &factory))) return best;
    IDXGIAdapter1 * adapter = nullptr;
    // Parcourt toutes les cartes et garde celle qui a le plus de mémoire dédiée.
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        adapter->Release();
        if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || d.VendorId == VENDOR_MICROSOFT) continue;
        if (best.found && d.DedicatedVideoMemory <= best.vram) continue;
        best.found = true;
        best.vram  = d.DedicatedVideoMemory;
        best.luid  = d.AdapterLuid;
        best.name  = to_utf8(d.Description);
        best.vendor = d.VendorId == VENDOR_AMD      ? GpuVendor::Amd
                      : d.VendorId == VENDOR_NVIDIA ? GpuVendor::Nvidia
                      : d.VendorId == VENDOR_INTEL  ? GpuVendor::Intel
                                                    : GpuVendor::Unknown;
    }
    factory->Release();
    return best;
}

const char * gpu_vendor_name(GpuVendor v) {
    switch (v) {
        case GpuVendor::Amd: return "AMD";
        case GpuVendor::Nvidia: return "Nvidia";
        case GpuVendor::Intel: return "Intel";
        default: return "?";
    }
}

GpuProfile & gpu_profile() {
    static GpuProfile p;  // unique pour toute l'application
    return p;
}

bool GpuProfile::stable() const {
    int c = compat.load();
    if (c == COMPAT_STABLE) return true;
    if (c == COMPAT_FAST) return false;
    // Automatique : seules les cartes Nvidia passent en mode rapide. AMD a besoin des noyaux FP32
    // (grésillements constatés sur RX 6700 XT) ; Intel et cartes inconnues : prudence.
    return gpu.vendor != GpuVendor::Nvidia;
}

bool GpuProfile::want_cuda() const {
    if (!cuda_installed) return false;
    int b = backend.load();
    if (b == BACKEND_CUDA) return true;
    if (b == BACKEND_VULKAN) return false;
    return gpu.vendor == GpuVendor::Nvidia;  // automatique : CUDA seulement sur une carte Nvidia
}

int compat_from_string(const std::string & s) {
    if (s == "stable") return COMPAT_STABLE;
    if (s == "fast") return COMPAT_FAST;
    return COMPAT_AUTO;
}

const char * compat_to_string(int c) {
    return c == COMPAT_STABLE ? "stable" : c == COMPAT_FAST ? "fast" : "auto";
}

int backend_from_string(const std::string & s) {
    if (s == "vulkan") return BACKEND_VULKAN;
    if (s == "cuda") return BACKEND_CUDA;
    return BACKEND_AUTO;
}

const char * backend_to_string(int b) {
    return b == BACKEND_VULKAN ? "vulkan" : b == BACKEND_CUDA ? "cuda" : "auto";
}

// Fichier relu par le moteur : <app>\donnees\temp\studio_yue2_gpu_limit.txt (1.0.30 ; %TEMP% avant), un nombre,
// 100 = aucune limite. Son chemin est transmis au moteur par la variable YUE2_GPU_LIMIT_FILE.
static std::wstring gpu_limit_file() {
    return (app_temp_dir() / L"studio_yue2_gpu_limit.txt").wstring();
}

void set_gpu_limit(int percent) {
    percent                 = percent < 10 ? 10 : percent > 100 ? 100 : percent;
    gpu_profile().gpu_limit = percent;
    FILE * f                = _wfopen(gpu_limit_file().c_str(), L"wb");
    if (f) {
        fprintf(f, "%d\n", percent);
        fclose(f);
    }
}

void apply_gpu_env() {
    // Mode stable : le pilote Vulkan AMD (RDNA2) donne des résultats instables avec les noyaux en demi-précision
    // (FP16) et le produit scalaire entier de ggml (écart de 30 à 80 % du signal d'un rendu à l'autre) :
    // saturation, parasites, compositions ratées. Ces trois variables imposent les noyaux FP32.
    // Mode rapide : on les retire (nullptr) pour laisser ggml choisir les noyaux les plus rapides.
    const wchar_t * v = gpu_profile().stable() ? L"1" : nullptr;
    SetEnvironmentVariableW(L"GGML_VK_DISABLE_F16", v);
    SetEnvironmentVariableW(L"GGML_VK_DISABLE_INTEGER_DOT_PRODUCT", v);
    SetEnvironmentVariableW(L"GGML_VK_DISABLE_MMVQ", v);
    // Limite d'utilisation de la carte : le moteur (modifié, patches\yue2cpp_gpu_limit.patch) fait une pause entre
    // deux pas de la phase audio, proportionnelle à la durée du pas (80 % : 0,25 × la durée du pas).
    SetEnvironmentVariableW(L"YUE2_GPU_LIMIT_FILE", gpu_limit_file().c_str());
}

const wchar_t * engine_flags() {
    return gpu_profile().cuda_active ? L"--clamp-fp16" : L"--no-fa --clamp-fp16";
}
