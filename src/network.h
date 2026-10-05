#pragma once

#include <string>
#include <vector>

namespace zb {

struct FetchResult {
    bool ok = false;
    int status = 0;
    std::string html;         // UTF-8 normalized
    std::string final_url;
    std::string content_type;
    std::string error;
    // 原始 Set-Cookie 响应头文本（不含前缀），用于浏览器级 Cookie 管理。
    std::vector<std::string> set_cookie;
};

// HTTPS/HTTP transport built on the Windows system TLS stack (WinHTTP).
// WinHTTP is used only as a byte pipe: redirects, gzip/deflate and TLS are
// handled here; HTML parsing, CSS, layout and painting remain self-built.
bool FetchUrl(const std::string& url, FetchResult* result, int timeout_ms = 15000);

// Same transport, but leaves the body bytes untouched (for media/downloads).
bool FetchBinary(const std::string& url, FetchResult* result,
                 int timeout_ms = 30000);

// Cookie-aware versions: they attach matching cookies and absorb Set-Cookie
// headers through the process-global jar. Real browser pages should use these
// so login/state cookies survive across HTML/CSS/image requests.
bool FetchUrlWithCookies(const std::string& url, FetchResult* result,
                         int timeout_ms = 15000);
bool FetchBinaryWithCookies(const std::string& url, FetchResult* result,
                            int timeout_ms = 30000);

// Diagnostic helpers for the network layer.
std::vector<std::string> CookieJarDump();

}  // namespace zb