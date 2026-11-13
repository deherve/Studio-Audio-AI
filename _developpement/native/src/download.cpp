#include "download.hpp"

#include "i18n.hpp"

#include <winhttp.h>

#include <algorithm>

unsigned long long http_remote_size(const std::wstring & url) {
    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t         host[256] = {}, path[2048] = {}, extra[1024] = {};
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = 256;
    uc.lpszUrlPath       = path;
    uc.dwUrlPathLength   = 2048;
    uc.lpszExtraInfo     = extra;
    uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 0;
    std::wstring       resource = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    unsigned long long size     = 0;
    HINTERNET          session  = WinHttpOpen(L"StudioAudioAI/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) return 0;
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req     = connect ? WinHttpOpenRequest(connect, L"GET", resource.c_str(), nullptr, nullptr, nullptr,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    if (req && WinHttpSendRequest(req, L"Range: bytes=0-0\r\n", (DWORD) -1L, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        wchar_t text[128] = {};
        DWORD   tl        = sizeof(text);
        if (status == 206 && WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_RANGE, nullptr, text, &tl, nullptr)) {
            const wchar_t * slash = wcsrchr(text, L'/');  // « bytes 0-0/2940000000 »
            if (slash) size = _wcstoui64(slash + 1, nullptr, 10);
        } else if (status == 200) {
            tl = sizeof(text);
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, text, &tl, nullptr)) size = _wcstoui64(text, nullptr, 10);
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return size;
}

bool http_get_text(const std::wstring & url, std::string & body) {
    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t         host[256] = {}, path[2048] = {};
    uc.lpszHostName     = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = 2048;
    wchar_t extra[1024] = {};
    uc.lpszExtraInfo     = extra;
    uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    // le chemin et la requête (?a=b) forment la ressource
    std::wstring resource = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) + std::wstring(uc.lpszExtraInfo ? uc.lpszExtraInfo : L"", uc.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"StudioAudioAI/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req     = connect ? WinHttpOpenRequest(connect, L"GET", resource.c_str(), nullptr, nullptr, nullptr,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    bool good = false;
    if (req && WinHttpSendRequest(req, L"Accept: application/json\r\n", (DWORD) -1L, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        if (status == 200) {
            body.clear();
            std::vector<char> buf(32 * 1024);
            good = true;
            for (;;) {
                DWORD got = 0;
                if (!WinHttpReadData(req, buf.data(), (DWORD) buf.size(), &got)) {
                    good = false;
                    break;
                }
                if (got == 0) break;
                body.append(buf.data(), got);
                if (body.size() > (1u << 22)) break;  // 4 Mo suffisent largement
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return good;
}

bool http_fetch(const std::wstring & url, const fs::path & dest, unsigned long long expected,
                std::atomic<unsigned long long> & done, std::atomic<bool> & cancel, std::wstring & error) {
    if (url.rfind(L"file:///", 0) == 0 && _wgetenv(L"STUDIO_TEST_UPDATE")) {  // 1.0.98 : essai de la mise à jour sans réseau : copie d'un fichier local
        std::error_code ec;
        fs::copy_file(fs::path(url.substr(8)), dest, fs::copy_options::overwrite_existing, ec);
        if (ec) { error = L"copie impossible"; return false; }
        done = fs::file_size(dest, ec);
        return true;
    }
    fs::path part = dest;
    part += L".part";
    std::error_code ec;
    unsigned long long have = fs::exists(part, ec) ? (unsigned long long) fs::file_size(part, ec) : 0;
    if (expected && have >= expected) {
        fs::remove(part, ec);
        if (done.load() >= have) done -= have;  // 1.1.013 : ces octets ne comptent plus (ni dépassement, ni passage sous zéro)
        have = 0;
    }
    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t         host[256] = {}, path[2048] = {};
    uc.lpszHostName     = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error = L"Adresse invalide.";
        return false;
    }
    HINTERNET session = WinHttpOpen(L"StudioAudioAI/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) return false;
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req     = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, nullptr, nullptr,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    bool   good = false;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (req) {
        std::wstring headers = have > 0 ? L"Range: bytes=" + std::to_wstring(have) + L"-" : L"";
        if (WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), (DWORD) -1L,
                               nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, len = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
            if (status == 200 && have > 0) {  // le serveur ignore la reprise : on repart de zéro
                if (done.load() >= have) done -= have;  // 1.1.013 : jamais sous zéro (appelants dont le compteur part de 0)
                have = 0;
            }
            if (status == 200 || status == 206) {
                file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, have > 0 ? OPEN_EXISTING : CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER pos;
                    pos.QuadPart = (LONGLONG) have;
                    SetFilePointerEx(file, pos, nullptr, FILE_BEGIN);
                    std::vector<char> buf(256 * 1024);
                    good = true;
                    for (;;) {
                        if (cancel) {
                            good = false;
                            break;
                        }
                        DWORD got = 0;
                        if (!WinHttpReadData(req, buf.data(), (DWORD) buf.size(), &got)) {
                            good = false;
                            break;
                        }
                        if (got == 0) break;
                        DWORD written = 0;
                        if (!WriteFile(file, buf.data(), got, &written, nullptr) || written != got) {
                            good  = false;
                            error = L"Écriture impossible (disque plein ?).";
                            break;
                        }
                        done += got;
                    }
                } else {
                    error = L"Fichier impossible à créer : " + part.wstring();
                }
            } else {
                error = L"Réponse du serveur : " + std::to_wstring(status);
            }
        }
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (req) WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (!good) return false;
    if (!fs::exists(part, ec) || (expected && fs::file_size(part, ec) != expected)) {
        error = L"Fichier incomplet : " + dest.filename().wstring();
        return false;
    }
    return MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

DownloadJob::~DownloadJob() {
    cancel = true;
    if (thread.joinable()) thread.join();
}

std::shared_ptr<DownloadJob> Downloader::find(const std::string & key) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
        if ((*it)->key == key) return *it;
    }
    return nullptr;
}

std::shared_ptr<DownloadJob> Downloader::start(const std::string & key, const std::string & label, const fs::path & dir,
                                               const std::vector<DlFile> & files) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto & j : jobs_) {
        if (j->key == key && j->state == 0) return j;  // déjà en cours
    }
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](auto & j) { return j->key == key; }), jobs_.end());
    auto job   = std::make_shared<DownloadJob>();
    job->key   = key;
    job->label = label;
    std::error_code ec;
    fs::create_directories(dir, ec);
    unsigned long long total = 0;
    for (const DlFile & f : files) total += f.bytes;
    job->total = total;
    DownloadJob * raw = job.get();
    job->thread = std::thread([raw, dir, files] {
        std::error_code ec2;
        for (const DlFile & f : files) {
            if (raw->cancel) break;
            fs::path dest(dir / std::wstring(f.file.begin(), f.file.end()));
            if (f.bytes && fs::exists(dest, ec2) && fs::file_size(dest, ec2) == f.bytes) {
                raw->done += f.bytes;
                continue;
            }
            fs::path part = dest;
            part += L".part";
            // Les octets déjà reçus d'un téléchargement interrompu comptent d'emblée.
            if (fs::exists(part, ec2)) raw->done += (unsigned long long) fs::file_size(part, ec2);
            std::wstring error;
            bool         ok = false;
            for (int attempt = 0; attempt < 4 && !ok && !raw->cancel; attempt++) {
                // 1.1.013 : le compteur du travail est donné directement à http_fetch : la barre avance pendant le fichier
                // (avant : un compteur à part, reporté seulement à la fin de chaque fichier ; un modèle de 3,5 Go restait à 0).
                ok = http_fetch(std::wstring(f.url.begin(), f.url.end()), dest, f.bytes, raw->done, raw->cancel, error);
                if (!ok && !raw->cancel) Sleep(1500);
            }
            if (!ok) {
                if (raw->cancel) {
                    raw->state = 3;
                } else {
                    int n = WideCharToMultiByte(CP_UTF8, 0, error.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    std::string msg(std::max(0, n - 1), '\0');
                    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, error.c_str(), -1, msg.data(), n, nullptr, nullptr);
                    raw->error = msg.empty() ? trf("Téléchargement impossible : %s", f.file.c_str()) : msg;
                    raw->state = 2;
                }
                return;
            }
        }
        raw->state = raw->cancel ? 3 : 1;
    });
    jobs_.push_back(job);
    return job;
}

void Downloader::cancel_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto & j : jobs_) j->cancel = true;
}
