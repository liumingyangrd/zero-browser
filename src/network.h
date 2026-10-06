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
// referer 非空时会带上 Referer 头：很多 CDN / 图床没有 Referer 会直接 403。
bool FetchUrlWithCookies(const std::string& url, FetchResult* result,
                         int timeout_ms = 15000,
                         const std::string& referer = std::string());
bool FetchBinaryWithCookies(const std::string& url, FetchResult* result,
                            int timeout_ms = 30000,
                            const std::string& referer = std::string());

// POST（表单提交用）：body 作为请求体发出，Content-Type 默认
// application/x-www-form-urlencoded，并带上 Cookie jar。
// 登录这类"提交后再跳转"的流程必须走这条，GET 只能用于搜索框那类查询。
bool FetchUrlPostWithCookies(const std::string& url, const std::string& body,
                             const std::string& content_type,
                             FetchResult* result, int timeout_ms = 15000,
                             const std::string& referer = std::string());

// 并行抓取一批 URL（内部用固定数量的工作线程 + 复用 WinHTTP 会话）。
// 真实站点一次要取几十个图片/样式，串行会把首屏拖到几十秒。
// referer 会作为这些子资源请求的 Referer 发送。
void FetchManyParallel(const std::vector<std::string>& urls, int threads,
                       std::vector<FetchResult>* out, int timeout_ms = 15000,
                       bool binary = true,
                       const std::string& referer = std::string());

// Diagnostic helpers for the network layer.
std::vector<std::string> CookieJarDump();

// 把一条 Set-Cookie 形式的文本（"name=value; path=/; max-age=300"）写入 jar。
// 用于处理「JS 先设置 Cookie 再重载」的反爬挑战页（见 app.cpp 的挑战处理）。
void CookieJarAbsorbText(const std::string& url, const std::string& set_cookie);

}  // namespace zb