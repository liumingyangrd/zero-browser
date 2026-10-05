// Minimal probe: does WinHTTP itself work in this sandbox?
// g++ -std=c++17 -Isrc tools\winhttp_probe.cpp -o build\winhttp_probe.exe -lwinhttp
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <string>

static void ShowError(const char* what) {
    DWORD e = GetLastError();
    printf("%s failed, Win32 error=%lu\n", what, (unsigned long)e);
}

int main(int argc, char** argv) {
    const char* raw = argc > 1 ? argv[1] : "http://127.0.0.1:8765/index.html";
    printf("url=%s\n", raw);

    HINTERNET session = WinHttpOpen(L"ZeroBrowserProbe/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        ShowError("WinHttpOpen(default)");
        session = WinHttpOpen(L"ZeroBrowserProbe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            ShowError("WinHttpOpen(noproxy)");
            return 1;
        }
        printf("note: default proxy open failed but no-proxy open succeeded\n");
    } else {
        printf("WinHttpOpen(default) ok\n");
    }

    std::wstring wurl;
    int len = (int)strlen(raw);
    for (int i = 0; i < len; i++) wurl.push_back((wchar_t)(unsigned char)raw[i]);

    wchar_t host[512] = {};
    wchar_t path[4096] = {};
    wchar_t extra[256] = {};
    wchar_t scheme[32] = {};
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = (DWORD)(sizeof(extra) / sizeof(extra[0]));
    uc.lpszScheme = scheme;
    uc.dwSchemeLength = (DWORD)(sizeof(scheme) / sizeof(scheme[0]));

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
        ShowError("WinHttpCrackUrl");
        WinHttpCloseHandle(session);
        return 1;
    }
    printf("scheme=%ls host=%ls port=%u path=%ls\n", scheme, host, (unsigned)uc.nPort,
           path);

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) {
        ShowError("WinHttpConnect");
        WinHttpCloseHandle(session);
        return 1;
    }
    printf("WinHttpConnect ok\n");

    bool secure = _wcsicmp(scheme, L"https") == 0;
    std::wstring fullpath = path;
    fullpath += extra;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", fullpath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        ShowError("WinHttpOpenRequest");
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return 1;
    }
    printf("WinHttpOpenRequest ok\n");

    BOOL sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent) {
        ShowError("WinHttpSendRequest");
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return 1;
    }
    printf("WinHttpSendRequest ok\n");

    if (!WinHttpReceiveResponse(req, nullptr)) {
        ShowError("WinHttpReceiveResponse");
    } else {
        DWORD status = 0, lenst = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &lenst,
                            WINHTTP_NO_HEADER_INDEX);
        printf("status=%lu\n", (unsigned long)status);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return 0;
}