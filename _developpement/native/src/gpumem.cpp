#include "gpumem.hpp"

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

// Valeur du compteur pour la carte désignée ; à défaut, l'instance la plus chargée.
bool read_counter(PDH_HCOUNTER counter, const wchar_t * wanted, unsigned long long & value) {
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &size, &count, nullptr) != (PDH_STATUS) PDH_MORE_DATA || size == 0) {
        return false;
    }
    std::vector<char> buffer(size);
    auto *            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &size, &count, items) != ERROR_SUCCESS) {
        return false;
    }
    bool               found = false;
    unsigned long long best  = 0;
    for (DWORD i = 0; i < count; i++) {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        unsigned long long v = (unsigned long long) std::max<LONGLONG>(0, items[i].FmtValue.largeValue);
        if (_wcsicmp(items[i].szName, wanted) == 0) {
            value = v;
            return true;
        }
        best  = std::max(best, v);
        found = true;
    }
    if (found) value = best;
    return found;
}

// Utilisation du processeur : une seule instance (_Total) ; « % Processor Utility » peut dépasser 100 (turbo).
bool read_cpu(PDH_HCOUNTER counter, double & value) {
    PDH_FMT_COUNTERVALUE v{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &v) != ERROR_SUCCESS) return false;
    if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) return false;
    value = std::clamp(v.doubleValue, 0.0, 100.0);
    return true;
}

// Utilisation de la carte : les instances « pid_1234_luid_0x…_0x…_phys_0_eng_3_engtype_3D » donnent la part de
// chaque moteur (3D, Compute, Copy, Video…) par programme. Comme le Gestionnaire des tâches, on additionne les
// programmes pour chaque moteur de notre carte, puis on garde le moteur le plus chargé.
bool read_gpu(PDH_HCOUNTER counter, const wchar_t * luid, double & value) {
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) != (PDH_STATUS) PDH_MORE_DATA || size == 0) {
        return false;
    }
    std::vector<char> buffer(size);
    auto *            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) {
        return false;
    }
    std::vector<std::pair<std::wstring, double>> engines;  // moteur (« _eng_3_engtype_3D ») -> total de tous les programmes
    for (DWORD i = 0; i < count; i++) {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        const wchar_t * name = items[i].szName;
        if (!wcsstr(name, luid)) continue;  // autre carte (puce intégrée…)
        const wchar_t * eng = wcsstr(name, L"_eng_");
        if (!eng) continue;
        std::wstring key(eng);
        auto it = std::find_if(engines.begin(), engines.end(), [&](const auto & e) { return e.first == key; });
        if (it == engines.end()) engines.push_back({ key, items[i].FmtValue.doubleValue });
        else it->second += items[i].FmtValue.doubleValue;
    }
    double best = 0;
    for (const auto & e : engines) best = std::max(best, e.second);
    value = std::clamp(best, 0.0, 100.0);
    return true;
}

}  // namespace

void GpuMonitor::start(LUID luid, unsigned long long total) {
    luid_  = luid;
    total_ = total;
    thread_ = std::thread([this] { run(); });
}

GpuMem GpuMonitor::get() const {
    GpuMem m;
    m.used   = used_;
    m.shared = shared_;
    m.total  = total_;
    m.ok     = ok_;
    m.cpu    = cpu_;
    m.gpu    = gpu_;
    return m;
}

GpuMonitor::~GpuMonitor() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void GpuMonitor::run() {
    PDH_HQUERY   query = nullptr;
    PDH_HCOUNTER dedicated = nullptr, shared = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &dedicated) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Shared Usage", 0, &shared) != ERROR_SUCCESS) {
        if (query) PdhCloseQuery(query);
        return;  // compteurs absents : les barres afficheront « indisponible »
    }
    wchar_t wanted[64];
    swprintf(wanted, 64, L"luid_0x%08lX_0x%08lX_phys_0", (unsigned long) luid_.HighPart, (unsigned long) luid_.LowPart);
    // Taux d'utilisation (facultatifs : sans eux, les barres CPU / GPU affichent « indisponible »).
    PDH_HCOUNTER cpu = nullptr, engine = nullptr;
    if (PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Utility", 0, &cpu) != ERROR_SUCCESS) {
        cpu = nullptr;  // anciens Windows : compteur classique
        if (PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% Processor Time", 0, &cpu) != ERROR_SUCCESS) cpu = nullptr;
    }
    if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engine) != ERROR_SUCCESS) engine = nullptr;
    wchar_t luid_only[48];  // « luid_0x…_0x… » : partie commune aux noms des moteurs de la carte
    swprintf(luid_only, 48, L"luid_0x%08lX_0x%08lX", (unsigned long) luid_.HighPart, (unsigned long) luid_.LowPart);
    while (!stop_) {
        if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
            unsigned long long d = 0, s = 0;
            bool               a = read_counter(dedicated, wanted, d);
            bool               b = read_counter(shared, wanted, s);
            if (a) {
                used_   = d;
                shared_ = b ? s : 0;
                ok_     = true;
            }
            double c = 0, g = 0;
            if (cpu && read_cpu(cpu, c)) cpu_ = c;
            if (engine && read_gpu(engine, luid_only, g)) gpu_ = g;
        }
        for (int i = 0; i < 20 && !stop_; i++) Sleep(100);
    }
    PdhCloseQuery(query);
}
