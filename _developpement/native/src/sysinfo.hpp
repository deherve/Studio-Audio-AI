// Informations sur le PC pour les recommandations (mémoire vive, processeur, type de disque) et liste des
// programmes qui occupent la mémoire de la carte graphique (vérification avant une génération).
#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class DiskKind { Unknown, Ssd, Hdd };

struct SystemInfo {
    unsigned long long ram_total = 0;  // mémoire vive installée, en octets
    int                threads   = 0;  // cœurs logiques du processeur
    std::string        cpu_name;       // « AMD Ryzen 7 5700X 8-Core Processor » (registre)
    DiskKind           models_disk = DiskKind::Unknown;  // disque qui contient les modèles
    std::string        models_drive;   // « C: »
};

// Lit la mémoire vive, le processeur et le type du disque qui contient models_dir (SSD ou disque dur :
// Windows indique si le disque a un « temps de recherche », ce qui est le cas des disques durs à plateaux).
SystemInfo read_system_info(const fs::path & models_dir);

// Programme qui occupe de la mémoire sur la carte graphique.
struct GpuProcess {
    unsigned long      pid = 0;
    std::string        name;       // « chrome.exe »
    unsigned long long dedicated = 0;  // mémoire vidéo dédiée occupée, en octets
};

// Programmes qui occupent la mémoire de la carte luid, du plus gourmand au moins gourmand (au plus max_count).
// Compteurs « GPU Process Memory » de Windows ; liste vide s'ils sont indisponibles.
std::vector<GpuProcess> gpu_memory_by_process(LUID luid, size_t max_count);
