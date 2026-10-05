#include "network.h"
#include "common.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_GZIP
#define WINHTTP_DECOMPRESSION_FLAG_GZIP 0x00000001
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_DEFLATE
#define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 0x00000002
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace zb {

namespace {

std::wstring U8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

std::string WToU8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n,
                        nullptr, nullptr);
    return out;
}

std::string LowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

int CharsetToCodepage(const std::string& charset_raw) {
    std::string c = LowerAscii(Trim(charset_raw));
    size_t semi = c.find(';');
    if (semi != std::string::npos) c = Trim(c.substr(0, semi));
    if (c.size() >= 2 && (c.front() == '"' || c.front() == '\'')) {
        c = c.substr(1, c.size() - 2);
    }
    if (c.empty() || c == "utf-8" || c == "utf8" || c == "us-ascii" ||
        c == "ascii") {
        return CP_UTF8;
    }
    if (c == "gbk" || c == "gb2312" || c == "gb18030" || c == "x-gbk") return 936;
    if (c == "big5" || c == "big-5") return 950;
    if (c == "shift_jis" || c == "shift-jis" || c == "sjis" || c == "cp932") return 932;
    if (c == "euc-jp") return 20932;
    if (c == "euc-kr" || c == "ks_c_5601-1987") return 949;
    if (c == "iso-8859-1" || c == "latin1" || c == "windows-1252") return 1252;
    if (c == "windows-1251") return 1251;
    return CP_ACP;
}

std::string CharsetFromContentType(const std::string& content_type) {
    std::string lower = LowerAscii(content_type);
    size_t pos = lower.find("charset");
    if (pos == std::string::npos) return "";
    size_t eq = content_type.find('=', pos);
    if (eq == std::string::npos) return "";
    size_t start = eq + 1;
    while (start < content_type.size() &&
           (content_type[start] == ' ' || content_type[start] == '"' ||
            content_type[start] == '\'')) {
        start++;
    }
    size_t end = start;
    while (end < content_type.size() && content_type[end] != ';' &&
           content_type[end] != '"' && content_type[end] != '\'' &&
           content_type[end] != ' ' && content_type[end] != '\r' &&
           content_type[end] != '\n') {
        end++;
    }
    return Trim(content_type.substr(start, end - start));
}

std::string CharsetFromMeta(const std::string& body) {
    size_t limit = std::min<size_t>(body.size(), 8192);
    std::string head = LowerAscii(body.substr(0, limit));
    size_t pos = head.find("charset");
    while (pos != std::string::npos) {
        size_t eq = head.find('=', pos);
        if (eq == std::string::npos) break;
        size_t start = eq + 1;
        while (start < head.size() &&
               (head[start] == ' ' || head[start] == '"' || head[start] == '\'')) {
            start++;
        }
        size_t end = start;
        while (end < head.size() && head[end] != '"' && head[end] != '\'' &&
               head[end] != '>' && head[end] != ';' && head[end] != ' ' &&
               head[end] != '\r' && head[end] != '\n') {
            end++;
        }
        std::string value = Trim(head.substr(start, end - start));
        if (!value.empty()) return value;
        pos = head.find("charset", pos + 7);
    }
    return "";
}

std::string ToUtf8(const std::string& body, const std::string& charset) {
    int cp = CharsetToCodepage(charset);
    if (cp == CP_UTF8) {
        if (body.size() >= 3 && (unsigned char)body[0] == 0xEF &&
            (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF) {
            return body.substr(3);
        }
        return body;
    }
    int n = MultiByteToWideChar(cp, 0, body.c_str(), (int)body.size(), nullptr, 0);
    if (n <= 0) return body;
    std::wstring wide((size_t)n, L'\0');
    MultiByteToWideChar(cp, 0, body.c_str(), (int)body.size(), wide.data(), n);
    return WToU8(wide);
}

std::string EnvUtf8(const char* name) {
    DWORD need = GetEnvironmentVariableA(name, nullptr, 0);
    if (need == 0) return "";
    std::string buf((size_t)need, '\0');
    DWORD got = GetEnvironmentVariableA(name, buf.data(), need);
    if (got == 0) return "";
    buf.resize((size_t)got);
    return buf;
}

std::string Win32Error(const char* what) {
    DWORD err = GetLastError();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s (Win32 错误 %lu)", what,
                  (unsigned long)err);
    return buf;
}

// --- Minimal browser-level cookie jar -------------------------------------
// 真实站点（洛谷 / B 站）依赖登录 Cookie 跨请求保持状态。这里按 RFC 6265 的
// 常用子集实现：host 匹配、路径前缀、Secure 与 HttpOnly、过期时间。
// 只负责“哪些 Cookie 能发给哪个请求”，没有做同源策略以外的复杂校验。

struct Cookie {
    std::string name;
    std::string value;
    std::string path = "/";
    std::string host;       // 设置该 Cookie 的域名
    bool secure = false;
    bool http_only = false;
    long long expires = 0;  // 0 = session; >0 = Unix 时间戳
};

std::mutex g_cookie_mtx;
std::vector<Cookie> g_cookies;

std::string HostOf(const std::string& url) {
    // 只识别 http(s)://host[:port]，端口不参与 Cookie 匹配。
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return "";
    size_t start = scheme + 3;
    size_t slash = url.find('/', start);
    size_t end = slash == std::string::npos ? url.size() : slash;
    size_t colon = url.rfind(':', end);
    size_t at = url.rfind('@', end);
    if (at != std::string::npos && at > start && at < end) start = at + 1;
    if (colon != std::string::npos && colon > start && colon < end) end = colon;
    std::string host = LowerAscii(url.substr(start, end - start));
    while (!host.empty() && host.front() == '.') host.erase(0, 1);
    return host;
}

std::string PathOf(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return "/";
    size_t slash = url.find('/', scheme + 3);
    if (slash == std::string::npos) return "/";
    size_t q = url.find('?', slash);
    size_t end = q == std::string::npos ? url.size() : q;
    return url.substr(slash, end - slash);
}

bool DomainMatches(const std::string& host, const std::string& rule) {
    if (host == rule) return true;
    if (rule.size() >= 2 && rule.front() == '.') {
        std::string base = rule.substr(1);
        if (host == base) return true;
        return host.size() > base.size() &&
               host.compare(host.size() - base.size(), base.size(), base) == 0 &&
               host[host.size() - base.size() - 1] == '.';
    }
    return false;
}

bool PathMatches(const std::string& req_path, const std::string& cookie_path) {
    if (cookie_path.empty() || cookie_path == "/") return true;
    if (req_path == cookie_path) return true;
    if (req_path.size() > cookie_path.size() &&
        req_path.compare(0, cookie_path.size(), cookie_path) == 0 &&
        cookie_path.back() == '/') {
        return true;
    }
    if (req_path.size() > cookie_path.size() &&
        req_path.compare(0, cookie_path.size(), cookie_path) == 0 &&
        req_path[cookie_path.size()] == '/') {
        return true;
    }
    return false;
}

long long NowSeconds() { return (long long)time(nullptr); }

void PurgeExpiredCookies() {
    long long now = NowSeconds();
    g_cookies.erase(
        std::remove_if(g_cookies.begin(), g_cookies.end(),
                       [&](const Cookie& c) {
                           return c.expires > 0 && c.expires < now;
                       }),
        g_cookies.end());
}

std::string CookieHeaderFor(const std::string& url, bool secure) {
    std::lock_guard<std::mutex> lock(g_cookie_mtx);
    PurgeExpiredCookies();
    std::string host = HostOf(url);
    std::string path = PathOf(url);
    std::string header;
    for (const Cookie& c : g_cookies) {
        if (c.expires > 0 && c.expires < NowSeconds()) continue;
        if (c.secure && !secure) continue;
        if (!DomainMatches(host, c.host)) continue;
        if (!PathMatches(path, c.path)) continue;
        if (!header.empty()) header += "; ";
        header += c.name + "=" + c.value;
    }
    return header;
}

long long ParseCookieExpiry(const std::string& rest_lower) {
    size_t ma = rest_lower.find("max-age=");
    if (ma != std::string::npos) {
        std::string v = Trim(rest_lower.substr(ma + 8));
        size_t end = v.find(';');
        v = v.substr(0, end == std::string::npos ? v.size() : end);
        long long secs = _atoi64(v.c_str());
        return secs > 0 ? NowSeconds() + secs : 0;
    }
    size_t ex = rest_lower.find("expires=");
    if (ex != std::string::npos) {
        std::string v = Trim(rest_lower.substr(ex + 8));
        size_t end = v.find(';');
        v = v.substr(0, end == std::string::npos ? v.size() : end);
        if (!v.empty() && std::isdigit((unsigned char)v[0])) {
            long long ts = _atoi64(v.c_str());
            return ts > 0 ? ts : 0;
        }
        return 0;
    }
    return 0;
}

void AbsorbSetCookie(const std::string& url, const std::string& raw) {
    if (raw.empty()) return;
    size_t semi = raw.find(';');
    std::string nv = raw.substr(0, semi == std::string::npos ? raw.size() : semi);
    size_t eq = nv.find('=');
    if (eq == std::string::npos) return;
    Cookie c;
    c.name = Trim(nv.substr(0, eq));
    c.value = Trim(nv.substr(eq + 1));
    if (c.name.empty()) return;
    c.host = HostOf(url);
    if (c.host.empty()) return;

    std::string rest = semi == std::string::npos ? std::string()
                                                  : raw.substr(semi + 1);
    std::string lower = LowerAscii(rest);
    c.secure = lower.find("secure") != std::string::npos;
    c.http_only = lower.find("httponly") != std::string::npos;
    size_t path_at = lower.find("path=");
    if (path_at != std::string::npos) {
        std::string p = rest.substr(path_at + 5);
        size_t p_end = p.find(';');
        p = Trim(p.substr(0, p_end == std::string::npos ? p.size() : p_end));
        if (!p.empty()) c.path = p;
    }
    c.expires = ParseCookieExpiry(lower);

    std::lock_guard<std::mutex> lock(g_cookie_mtx);
    g_cookies.erase(
        std::remove_if(g_cookies.begin(), g_cookies.end(),
                       [&](const Cookie& old) {
                           return old.host == c.host && old.name == c.name &&
                                  old.path == c.path;
                       }),
        g_cookies.end());
    g_cookies.push_back(std::move(c));
}

void AbsorbSetCookies(const std::string& url,
                      const std::vector<std::string>& headers) {
    for (const auto& h : headers) AbsorbSetCookie(url, h);
}

}  // namespace

// 每个线程复用一个 WinHTTP 会话。会话复用才能启用连接保活（keep-alive）与
// TLS 会话缓存：否则每个图片/样式都要重新握手，真实站点几十个资源要几十秒。
HINTERNET AcquireSession(DWORD access_type, const wchar_t* named_proxy,
                         int timeout_ms) {
    struct Cache {
        HINTERNET handle = nullptr;
        std::wstring proxy;
        DWORD access = 0;
    };
    static thread_local Cache cache;
    std::wstring want = named_proxy ? named_proxy : L"";
    if (cache.handle && cache.proxy == want && cache.access == access_type) {
        WinHttpSetTimeouts(cache.handle, timeout_ms, timeout_ms, timeout_ms,
                           timeout_ms * 2);
        return cache.handle;
    }
    if (cache.handle) {
        WinHttpCloseHandle(cache.handle);
        cache.handle = nullptr;
    }
    HINTERNET session = WinHttpOpen(
        L"ZeroBrowser/0.1.1 (self-built engine; system TLS transport)",
        access_type, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return nullptr;
    if (named_proxy && *named_proxy) {
        WINHTTP_PROXY_INFO proxy{};
        proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        proxy.lpszProxy = const_cast<wchar_t*>(named_proxy);
        proxy.lpszProxyBypass = nullptr;
        WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));
    }
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms,
                       timeout_ms * 2);
    cache.handle = session;
    cache.proxy = want;
    cache.access = access_type;
    return session;
}

bool FetchOnce(const std::string& url, FetchResult* result, int timeout_ms,
               DWORD access_type, const wchar_t* named_proxy,
               bool binary = false, bool use_cookies = false,
               const std::string& referer = std::string()) {
    if (!result) return false;
    *result = FetchResult{};
    std::wstring wurl = U8ToW(url);

    HINTERNET session = AcquireSession(access_type, named_proxy, timeout_ms);
    if (!session) {
        result->error = Win32Error("WinHttpOpen 失败");
        return false;
    }
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms,
                       timeout_ms * 2);

    wchar_t host[512] = {};
    wchar_t path[4096] = {};
    wchar_t extra[4096] = {};
    wchar_t scheme[32] = {};
    URL_COMPONENTS uc{};
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
        result->error = "无法解析 URL";
        return false;
    }

    bool secure = (_wcsicmp(scheme, L"https") == 0);
    std::wstring path_with_extra = path;
    path_with_extra += extra;

    HINTERNET connect = WinHttpConnect(session, host, (INTERNET_PORT)uc.nPort, 0);
    if (!connect) {
        result->error = Win32Error("WinHttpConnect 失败");
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", path_with_extra.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        result->error = Win32Error("WinHttpOpenRequest 失败");
        WinHttpCloseHandle(connect);
        return false;
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                     sizeof(redirect_policy));

#ifdef WINHTTP_NO_CLIENT_CERT_CONTEXT
    WinHttpSetOption(request, WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
                     WINHTTP_NO_CLIENT_CERT_CONTEXT, 0);
#endif

    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP |
                       WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompress,
                     sizeof(decompress));

    const wchar_t* headers =
        L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
        L"text/plain;q=0.8,*/*;q=0.5\r\n"
        L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
        L"Accept-Encoding: gzip, deflate\r\n";
    WinHttpAddRequestHeaders(request, headers, (DWORD)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // 很多 CDN / 图床在缺少 Referer 时直接 403，子资源请求要带上来源页。
    if (!referer.empty()) {
        std::wstring wref = U8ToW("Referer: " + referer + "\r\n");
        WinHttpAddRequestHeaders(request, wref.c_str(), (DWORD)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD |
                                     WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (use_cookies) {
        std::string cookie_header = CookieHeaderFor(url, secure);
        if (!cookie_header.empty()) {
            std::wstring wcookie = U8ToW("Cookie: " + cookie_header + "\r\n");
            WinHttpAddRequestHeaders(request, wcookie.c_str(), (DWORD)-1L,
                                     WINHTTP_ADDREQ_FLAG_ADD |
                                         WINHTTP_ADDREQ_FLAG_REPLACE);
        }
    }

    BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent) {
        result->error = Win32Error("请求发送失败");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        result->error = Win32Error("接收响应失败");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        return false;
    }

    DWORD status = 0;
    DWORD status_len = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                        WINHTTP_NO_HEADER_INDEX);
    result->status = (int)status;

    wchar_t final_url[4096] = {};
    DWORD final_len = sizeof(final_url);
    if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, final_url, &final_len)) {
        result->final_url = WToU8(final_url);
    }
    if (result->final_url.empty()) result->final_url = url;

    wchar_t content_type[512] = {};
    DWORD ct_len = sizeof(content_type);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE,
                            WINHTTP_HEADER_NAME_BY_INDEX, content_type,
                            &ct_len, WINHTTP_NO_HEADER_INDEX)) {
        result->content_type = WToU8(content_type);
    }

    if (use_cookies) {
        // 一次取回完整原始响应头，自解析所有 Set-Cookie 行。
        // 不依赖 WinHttpQueryHeaders 的索引枚举：MinGW 的头文件签名与
        // Windows SDK 不一致，会漏掉同一名字的后续响应头。
        wchar_t raw_buf[16384] = {};
        DWORD raw_len = sizeof(raw_buf);
        if (WinHttpQueryHeaders(
                request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                WINHTTP_HEADER_NAME_BY_INDEX, raw_buf, &raw_len,
                WINHTTP_NO_HEADER_INDEX)) {
            std::string raw = WToU8(raw_buf);
            std::string lower = LowerAscii(raw);
            size_t pos = 0;
            while (true) {
                size_t hit = lower.find("set-cookie:", pos);
                if (hit == std::string::npos) break;
                size_t line_end = raw.find("\r\n", hit);
                if (line_end == std::string::npos) line_end = raw.find('\n', hit);
                if (line_end == std::string::npos) line_end = raw.size();
                std::string value = Trim(raw.substr(hit + 11,
                                                    line_end - hit - 11));
                if (!value.empty()) result->set_cookie.push_back(value);
                pos = line_end + 1;
            }
        }
    }

    std::string body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
        std::string chunk((size_t)available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0) {
            break;
        }
        body.append(chunk.data(), (size_t)read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    // 注意：session 由线程级缓存持有，这里不能关闭，否则下一次请求会用到野句柄。

    if (use_cookies) {
        AbsorbSetCookies(result->final_url.empty() ? url : result->final_url,
                         result->set_cookie);
    }

    std::string charset = CharsetFromContentType(result->content_type);
    if (charset.empty()) charset = CharsetFromMeta(body);
    result->html = binary ? body : ToUtf8(body, charset);
    result->ok = (status >= 200 && status < 400);
    if (!result->ok && result->html.empty() && result->error.empty()) {
        result->error = "服务器返回 HTTP " + std::to_string((int)status);
    }
    return true;
}

bool FetchWithStrategies(const std::string& url, FetchResult* result,
                         int timeout_ms, bool binary, bool use_cookies,
                         const std::string& referer = std::string()) {
    if (!result) return false;

    std::string env = EnvUtf8("ZB_PROXY");
    std::wstring env_proxy = U8ToW(env);

    struct Attempt {
        DWORD type;
        const wchar_t* proxy;
        const char* label;
    };
    std::vector<Attempt> attempts;
    if (!env_proxy.empty()) {
        attempts.push_back({WINHTTP_ACCESS_TYPE_NAMED_PROXY, env_proxy.c_str(),
                            "ZB_PROXY 指定代理"});
    }
    attempts.push_back({WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, "自动代理"});
    attempts.push_back({WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, "默认代理"});
    attempts.push_back({WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, "直连"});

    std::string all_errors;
    for (const auto& attempt : attempts) {
        FetchResult one;
        bool ok = FetchOnce(url, &one, timeout_ms, attempt.type, attempt.proxy,
                            binary, use_cookies, referer);
        if (ok && (one.status > 0 || !one.html.empty())) {
            *result = one;
            return true;
        }
        std::string detail = one.error.empty() ? "无响应" : one.error;
        if (!all_errors.empty()) all_errors += " | ";
        all_errors += std::string(attempt.label) + ": " + detail;
    }

    *result = FetchResult{};
    if (all_errors.empty()) all_errors = "所有连接方式都失败";
    if (env_proxy.empty()) {
        all_errors += "（可设置环境变量 ZB_PROXY=http://127.0.0.1:端口 指定代理）";
    }
    result->error = all_errors;
    return false;
}

bool FetchUrl(const std::string& url, FetchResult* result, int timeout_ms) {
    return FetchWithStrategies(url, result, timeout_ms, false, false);
}

bool FetchBinary(const std::string& url, FetchResult* result, int timeout_ms) {
    return FetchWithStrategies(url, result, timeout_ms, true, false);
}

bool FetchUrlWithCookies(const std::string& url, FetchResult* result,
                         int timeout_ms, const std::string& referer) {
    return FetchWithStrategies(url, result, timeout_ms, false, true, referer);
}

bool FetchBinaryWithCookies(const std::string& url, FetchResult* result,
                            int timeout_ms, const std::string& referer) {
    return FetchWithStrategies(url, result, timeout_ms, true, true, referer);
}

void FetchManyParallel(const std::vector<std::string>& urls, int threads,
                       std::vector<FetchResult>* out, int timeout_ms,
                       bool binary, const std::string& referer) {
    if (!out) return;
    out->assign(urls.size(), FetchResult{});
    if (urls.empty()) return;
    if (threads < 1) threads = 1;
    if (threads > (int)urls.size()) threads = (int)urls.size();

    std::atomic<size_t> next{0};
    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= urls.size()) break;
            FetchResult res;
            // 子资源统一带 Referer（指向来源页），否则很多 CDN 直接 403。
            FetchWithStrategies(urls[i], &res, timeout_ms, binary, true, referer);
            (*out)[i] = std::move(res);
        }
    };
    std::vector<std::thread> pool;
    pool.reserve((size_t)threads);
    for (int t = 0; t < threads - 1; ++t) pool.emplace_back(worker);
    worker();  // 当前线程也干活，避免多等一个 RTT
    for (auto& th : pool) th.join();
}

std::vector<std::string> CookieJarDump() {
    std::lock_guard<std::mutex> lock(g_cookie_mtx);
    std::vector<std::string> out;
    for (const Cookie& c : g_cookies) {
        std::string line = c.host + "\t" + c.path + "\t" + c.name + "=" +
                           c.value + (c.secure ? "\tsecure" : "") +
                           (c.http_only ? "\thttpOnly" : "");
        out.push_back(line);
    }
    return out;
}

void CookieJarAbsorbText(const std::string& url,
                         const std::string& set_cookie) {
    if (set_cookie.empty()) return;
    AbsorbSetCookie(url, set_cookie);
}

}  // namespace zb