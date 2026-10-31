// Carte graphique du PC (AMD, Nvidia, Intel) et profil de calcul du moteur yue2.cpp.
//
// Pourquoi : les réglages qui rendent le moteur stable sur une carte AMD (noyaux FP32, sans flash-attention)
// ne servent à rien sur une carte Nvidia et la ralentissent. L'application reconnaît donc la marque de la carte
// au démarrage et choisit les réglages adaptés ; l'utilisateur peut forcer un autre choix dans les Réglages.
#pragma once

#include <windows.h>

#include <atomic>
#include <string>

enum class GpuVendor { Unknown, Amd, Nvidia, Intel };

struct GpuInfo {
    GpuVendor          vendor = GpuVendor::Unknown;
    std::string        name;      // nom complet de la carte, ex. « AMD Radeon RX 6700 XT » (UTF-8)
    unsigned long long vram = 0;  // mémoire vidéo dédiée, en octets
    LUID               luid{};    // identifiant Windows de la carte (sert aux compteurs de mémoire vidéo)
    bool               found = false;
};

// Carte la plus puissante du PC : celle qui a le plus de mémoire dédiée (sur un portable, la carte Nvidia ou AMD
// plutôt que la puce Intel intégrée). La carte logicielle de Microsoft est ignorée.
GpuInfo detect_gpu();

// « AMD », « Nvidia », « Intel » ou « ? ».
const char * gpu_vendor_name(GpuVendor v);

// Réglage « Compatibilité de la carte ».
enum GpuCompat : int {
    COMPAT_AUTO   = 0,  // stable sur AMD et Intel, rapide sur Nvidia
    COMPAT_STABLE = 1,  // noyaux FP32 imposés (plus lent, sans grésillement)
    COMPAT_FAST   = 2,  // noyaux FP16 (plus rapide ; peut grésiller sur certaines cartes AMD)
};
// Réglage « Moteur de calcul ».
enum GpuBackend : int {
    BACKEND_AUTO   = 0,  // CUDA si la carte est Nvidia et que le moteur CUDA est installé, sinon Vulkan
    BACKEND_VULKAN = 1,  // Vulkan (toutes les cartes)
    BACKEND_CUDA   = 2,  // CUDA (cartes Nvidia seulement, dossier « moteur_cuda »)
};

// Profil de calcul de la session. Les deux réglages sont atomiques : la file de génération les lit
// depuis son propre thread pendant que l'interface peut les modifier.
struct GpuProfile {
    GpuInfo          gpu;
    std::atomic<int> compat{ COMPAT_AUTO };
    std::atomic<int> backend{ BACKEND_AUTO };
    // Limite d'utilisation de la carte en % (100 = aucune limite) : le moteur est mis en pause par intervalles
    // très courts (voir run_process dans studio.cpp). Modifiable pendant une génération.
    std::atomic<int> gpu_limit{ 100 };
    bool             cuda_installed = false;  // moteur CUDA présent à côté de l'application
    bool             cuda_active    = false;  // moteur CUDA utilisé pendant cette session (décidé au démarrage)

    // Vrai : on impose les noyaux FP32 au moteur Vulkan (variables GGML_VK_*).
    bool stable() const;
    // Vrai si les réglages demandent le moteur CUDA (pris en compte au prochain démarrage).
    bool want_cuda() const;
};
GpuProfile & gpu_profile();

// Change la limite d'utilisation de la carte (10 à 100 %) et l'écrit dans un petit fichier temporaire que le moteur
// yue2.cpp relit à chaque pas de la phase audio (variable YUE2_GPU_LIMIT_FILE) : la limite s'applique dès le pas suivant.
void set_gpu_limit(int percent);

// Valeurs mémorisées dans studio_reglages.json (« auto », « stable », « fast » / « auto », « vulkan », « cuda »).
int          compat_from_string(const std::string & s);
const char * compat_to_string(int c);
int          backend_from_string(const std::string & s);
const char * backend_to_string(int b);

// Pose ou retire les variables GGML_VK_* selon le profil. Elles sont héritées par les programmes lancés
// ensuite (yue-synth, yue-transcribe, whisper-cli) : à appeler juste avant chaque lancement.
void apply_gpu_env();

// Options de ligne de commande de yue-synth selon le moteur :
//  - Vulkan : « --no-fa --clamp-fp16 » (flash-attention Vulkan très lente sur AMD ; sans le clamp, valeurs hors
//    plage FP16 et silence) ;
//  - CUDA   : « --clamp-fp16 » (la flash-attention CUDA est rapide et fiable).
const wchar_t * engine_flags();
