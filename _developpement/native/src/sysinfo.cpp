// Informations sur le PC et programmes qui occupent la carte graphique (voir sysinfo.hpp).
#include "sysinfo.hpp"

#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <winioctl.h>

#include <algorithm>
#include <cwchar>
#include <map>

#include "studio.hpp"  // narrow

SystemInfo read_system_info(const fs::path & models_dir) {
    SystemInfo info;
    MEMORYSTATUSEX mem{ sizeof(mem) };
    if (GlobalMemoryStatusEx(&mem)) info.ram_total = mem.ullTotalPhys;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    info.threads = (int) si.dwNumberOfProcessors;

    // Nom du processeur (registre de Windows).
    wchar_t name[128] = {};
    DWORD   size      = sizeof(name);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ, nullptr,
                     name, &size) == ERROR_SUCCESS) {
        info.cpu_name = narrow(name);
        while (!info.cpu_name.empty() && info.cpu_name.back() == ' ') info.cpu_name.pop_back();
    }

    // Disque des modèles : « \\.\C: », puis question au pilote : le disque a-t-il un temps de recherche ?
    std::error_code ec;
    fs::path        abs = fs::absolute(models_dir, ec);
    std::wstring    root = abs.root_name().wstring();  // « C: »
    if (root.size() == 2 && root[1] == L':') {
        info.models_drive = narrow(root);
        std::wstring device = L"\\\\.\\" + root;
        HANDLE       h = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            STORAGE_PROPERTY_QUERY        q{};
            DEVICE_SEEK_PENALTY_DESCRIPTOR d{};
            DWORD                         got = 0;
            q.PropertyId = StorageDeviceSeekPenaltyProperty;
            q.QueryType  = PropertyStandardQuery;
            if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &got, nullptr) && got >= sizeof(d)) {
                info.models_disk = d.IncursSeekPenalty ? DiskKind::Hdd : DiskKind::Ssd;
            }
            CloseHandle(h);
        }
    }
    return info;
}

std::vector<GpuProcess> gpu_memory_by_process(LUID luid, size_t max_count) {
    std::vector<GpuProcess> out;
    PDH_HQUERY              query   = nullptr;
    PDH_HCOUNTER            counter = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return out;
    // Compteur instantané (pas de moyenne) : une seule lecture suffit.
    if (PdhAddEnglishCounterW(query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &counter) != ERROR_SUCCESS ||
        PdhCollectQueryData(query) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return out;
    }
    wchar_t wanted[48];  // instances « pid_1234_luid_0x…_0x…_phys_0 »
    swprintf(wanted, 48, L"luid_0x%08lX_0x%08lX", (unsigned long) luid.HighPart, (unsigned long) luid.LowPart);
    std::map<unsigned long, unsigned long long> by_pid;
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &size, &count, nullptr) == (PDH_STATUS) PDH_MORE_DATA && size > 0) {
        std::vector<char> buffer(size);
        auto *            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &size, &count, items) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < count; i++) {
                if (!wcsstr(items[i].szName, wanted)) continue;
                unsigned long pid = 0;
                if (swscanf(items[i].szName, L"pid_%lu_", &pid) != 1) continue;
                by_pid[pid] += (unsigned long long) std::max<LONGLONG>(0, items[i].FmtValue.largeValue);
            }
        }
    }
    PdhCloseQuery(query);

    // Noms des programmes (instantané de la liste des processus).
    std::map<unsigned long, std::string> names;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{ sizeof(e) };
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) names[e.th32ProcessID] = narrow(e.szExeFile);
        CloseHandle(snap);
    }
    for (auto & [pid, bytes] : by_pid) {
        if (bytes < 50ull * 1048576) continue;  // moins de 50 Mo : négligeable
        GpuProcess p;
        p.pid       = pid;
        p.dedicated = bytes;
        p.name      = names.count(pid) ? names[pid] : "pid " + std::to_string(pid);
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const GpuProcess & a, const GpuProcess & b) { return a.dedicated > b.dedicated; });
    if (out.size() > max_count) out.resize(max_count);
    return out;
}
