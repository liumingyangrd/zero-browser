#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <cstdio>

int main() {
    HINTERNET session = WinHttpOpen(L"min-probe", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { std::printf("open fail %lu\n", GetLastError()); return 1; }
    HINTERNET conn = WinHttpConnect(session, L"example.com",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) { std::printf("connect fail %lu\n", GetLastError()); return 1; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", L"/", nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) { std::printf("request fail %lu\n", GetLastError()); return 1; }
    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    std::printf("send=%d err=%lu\n", ok ? 1 : 0, GetLastError());
    if (ok) {
        ok = WinHttpReceiveResponse(req, nullptr);
        std::printf("recv=%d err=%lu\n", ok ? 1 : 0, GetLastError());
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                            WINHTTP_NO_HEADER_INDEX);
        std::printf("status=%lu\n", status);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return 0;
}
