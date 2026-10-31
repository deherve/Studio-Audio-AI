// Requêtes HTTPS courtes (voir net.hpp).
#include "net.hpp"

#include <windows.h>
#include <winhttp.h>

#include "studio.hpp"  // widen
#include "version.hpp"

int https_get(const std::wstring & host, const std::wstring & path, std::string & body) {
    body.clear();
    int       status = 0;
    HINTERNET s      = WinHttpOpen(L"StudioAudioAI/" STUDIO_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 4000, 4000, 6000, 8000);  // hors ligne : abandon en quelques secondes
    HINTERNET c = WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, size = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        status = (int) code;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail > 0 && body.size() < 4 * 1024 * 1024) {  // 4 Mo au plus
            std::string chunk(avail, '\0');
            DWORD       got = 0;
            if (!WinHttpReadData(r, chunk.data(), avail, &got) || got == 0) break;
            body.append(chunk.data(), got);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return status;
}

int https_get_url(const std::string & url, std::string & body) {
    body.clear();
    const std::string prefix = "https://";
    if (url.compare(0, prefix.size(), prefix) != 0) return 0;
    std::string rest  = url.substr(prefix.size());
    size_t      slash = rest.find('/');
    std::string host  = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path  = slash == std::string::npos ? "/" : rest.substr(slash);
    if (host.empty()) return 0;
    return https_get(widen(host), widen(path), body);
}

int http_request(const std::string & method, const std::string & url, const std::vector<std::pair<std::string, std::string>> & headers,
                 const std::string & body, std::string & out, int timeout_ms) {
    out.clear();
    // Découpe de l'adresse : protocole, hôte, port, chemin.
    std::wstring    wurl = widen(url);
    URL_COMPONENTS  uc{ sizeof(uc) };
    wchar_t         host[256] = {}, path[2048] = {};
    uc.lpszHostName     = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = 2048;
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo     = extra;  // « ?param=… »
    uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return 0;
    bool         secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring target = std::wstring(path) + extra;
    if (target.empty()) target = L"/";

    int       status = 0;
    HINTERNET s      = WinHttpOpen(L"StudioAudioAI/" STUDIO_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    // Résolution et connexion courtes (serveur absent : réponse vite), envoi 30 s, réponse : timeout_ms.
    WinHttpSetTimeouts(s, 5000, 5000, 30000, timeout_ms);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, widen(method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         secure ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    std::wstring head;
    for (const auto & [name, value] : headers) head += widen(name) + L": " + widen(value) + L"\r\n";
    if (!body.empty()) head += L"Content-Type: application/json\r\n";
    if (r && WinHttpSendRequest(r, head.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : head.c_str(), head.empty() ? 0 : (DWORD) -1L,
                                body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID) body.data(), (DWORD) body.size(), (DWORD) body.size(), 0) &&
        WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, size = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        status      = (int) code;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail > 0 && out.size() < 8 * 1024 * 1024) {  // 8 Mo au plus
            std::string chunk(avail, '\0');
            DWORD       got = 0;
            if (!WinHttpReadData(r, chunk.data(), avail, &got) || got == 0) break;
            out.append(chunk.data(), got);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return status;
}
