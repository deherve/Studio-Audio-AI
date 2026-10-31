// Suivi de la carte et du processeur (compteurs de performance de Windows), relu toutes les 2 secondes :
// mémoire vidéo, débordement, taux d'utilisation du processeur (CPU) et de la carte (GPU).
#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

struct GpuMem {
    unsigned long long used   = 0;  // mémoire dédiée occupée sur la carte (tous programmes)
    unsigned long long total  = 0;  // mémoire dédiée de la carte
    unsigned long long shared = 0;  // mémoire système utilisée par la carte : le débordement
    bool               ok     = false;
    double             cpu    = -1;  // utilisation du processeur en % (0 à 100) ; -1 = indisponible
    double             gpu    = -1;  // utilisation de la carte en % (moteur le plus chargé, comme le Gestionnaire des tâches) ; -1 = indisponible
};

class GpuMonitor {
  public:
    // luid : identifiant de la carte utilisée par l'application ; total : sa mémoire dédiée en octets.
    void   start(LUID luid, unsigned long long total);
    GpuMem get() const;
    ~GpuMonitor();

  private:
    void                          run();
    LUID                          luid_{};
    unsigned long long            total_ = 0;
    std::atomic<unsigned long long> used_{ 0 }, shared_{ 0 };
    std::atomic<bool>             ok_{ false }, stop_{ false };
    std::atomic<double>           cpu_{ -1 }, gpu_{ -1 };
    std::thread                   thread_;
};
