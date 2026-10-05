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
    // Raw Set-Cookie response header text (without the prefix), used for
    // browser-level cookie management.
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
// When referer is non-empty a Referer header is sent: many CDNs / image hosts
// answer with a plain 403 when Referer is missing.
bool FetchUrlWithCookies(const std::string& url, FetchResult* result,
                         int timeout_ms = 15000,
                         const std::string& referer = std::string());
bool FetchBinaryWithCookies(const std::string& url, FetchResult* result,
                            int timeout_ms = 30000,
                            const std::string& referer = std::string());

// Fetch a batch of URLs in parallel (internally uses a fixed number of worker
// threads and reuses the WinHTTP session).
// A real site needs dozens of images/stylesheets at once; serial fetching pushes
// first paint out to tens of seconds.
// referer is sent as the Referer header for these subresource requests.
void FetchManyParallel(const std::vector<std::string>& urls, int threads,
                       std::vector<FetchResult>* out, int timeout_ms = 15000,
                       bool binary = true,
                       const std::string& referer = std::string());

// Diagnostic helpers for the network layer.
std::vector<std::string> CookieJarDump();

// Write a single Set-Cookie style text ("name=value; path=/; max-age=300")
// into the jar. Used to handle anti-bot challenge pages that set a cookie from
// script and then reload (see the challenge handling in app.cpp).
void CookieJarAbsorbText(const std::string& url, const std::string& set_cookie);

}  // namespace zb