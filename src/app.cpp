#include "app.h"

#include "js_dom.h"
#include "network.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <vector>

namespace zb {

namespace {

const wchar_t* kClassW = L"ZeroBrowserSelfBuilt";

const UINT kMsgNavigationDone = WM_APP + 1;
// 资源（图片 + 外链脚本）取完后的第二阶段消息，见 StartNavigate。
const UINT kMsgAssetsDone = WM_APP + 2;

// 工具栏与地址栏的统一几何。绘制、命中测试、光标定位必须共用这几个常量，
// 否则改一处忘一处就会出现「看到的位置点不中」这类问题。
const int kToolbarTop = 38;
const int kToolbarY = 44;
const int kToolbarH = 40;
const int kBtnSize = 30;
const int kBtnHeight = 28;
const int kAddrX = 140;
const int kAddrH = 30;
const int kAddrPadX = 12;
const int kAddrFont = 15;

// 取 UTF-8 文本里 i 之前一个码点的起始下标（i 需落在码点边界上）。
// 必须先把空串和 i==0 挡掉：否则 i 被钳成 0 之后 i-1 会下溢成 SIZE_MAX，
// 下一行的 s[j] 就是越界读（曾经导致「地址栏为空时一输入就崩溃」）。
size_t Utf8PrevIndex(const std::string& s, size_t i) {
    if (s.empty()) return 0;
    if (i > s.size()) i = s.size();
    if (i == 0) return 0;
    size_t j = i - 1;
    while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--;
    return j;
}

// 把下标吸附到不超过 i 的码点边界上（i 落在边界上时原样返回）。
size_t Utf8SnapToBoundary(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

// 取 i 处码点之后的下标。
size_t Utf8NextIndex(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    unsigned char c = (unsigned char)s[i];
    size_t len = 1;
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    if (i + len > s.size()) len = 1;
    return i + len;
}

// ---- 地址栏剪贴板 ----------------------------------------------------------
// 右键菜单命令 ID。用 TrackPopupMenu 的 TPM_RETURNCMD 直接取返回值，不需要
// WM_COMMAND 分发（窗口类没有菜单资源，多一层转发只会多一处出错的地方）。
const UINT kMenuCut = 1;
const UINT kMenuCopy = 2;
const UINT kMenuPaste = 3;
const UINT kMenuSelectAll = 4;

// 粘贴长度上限：地址栏是单行输入框，粘几 MB 文本只会把界面拖死。
const size_t kClipboardMaxBytes = 8192;

// 地址栏只能放单行：丢掉换行、制表符、其它 C0/C1 控制字符与 U+2028/2029。
// 这一步不能省：剪贴板文本常带尾随换行，直接拼进 URL 后 ResolveUrl 与
// WinHttpOpen 会拿到带 \n 的地址（导航失败，或请求行被污染）。
// 按字节扫描即可，ASCII 与多字节序列的首字节互不冲突，不会切坏 UTF-8。
std::string SanitizeSingleLine(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c == 0x7F) {  // C0 控制字符（含 \r \n \t）
            i += 1;
            continue;
        }
        if (c == 0xC2 && i + 1 < text.size()) {  // U+0080..U+009F（C1 控制字符）
            unsigned char d = (unsigned char)text[i + 1];
            if (d >= 0x80 && d <= 0x9F) {
                i += 2;
                continue;
            }
        }
        if (c == 0xE2 && i + 2 < text.size() &&  // U+2028 / U+2029 行分隔符
            (unsigned char)text[i + 1] == 0x80 &&
            ((unsigned char)text[i + 2] == 0xA8 ||
             (unsigned char)text[i + 2] == 0xA9)) {
            i += 3;
            continue;
        }
        out.push_back(text[i]);
        i += 1;
    }
    if (out.size() > kClipboardMaxBytes) {
        out.resize(Utf8SnapToBoundary(out, kClipboardMaxBytes));
    }
    return out;
}

// 读剪贴板文本并转 UTF-8：优先 CF_UNICODETEXT，退回 CF_TEXT（按系统 ANSI 代码页解）。
// 打不开剪贴板（被别的进程占用）时返回空串，绝不让插入路径读到未初始化数据。
std::string ReadClipboardText(HWND owner) {
    std::string raw;
    if (OpenClipboard(owner)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                raw = WideToUtf8(std::wstring(p));
                GlobalUnlock(h);
            }
        } else if (HANDLE h = GetClipboardData(CF_TEXT)) {
            if (const char* p = (const char*)GlobalLock(h)) {
                int n = MultiByteToWideChar(CP_ACP, 0, p, -1, nullptr, 0);
                if (n > 1) {
                    // 必须整块 n 个 wchar_t 的缓冲：字符串 size 给 n-1 再让 API
                    // 写 n 个（含结尾 \0）会越界一个 wchar_t。
                    std::vector<wchar_t> buf((size_t)n);
                    MultiByteToWideChar(CP_ACP, 0, p, -1, buf.data(), n);
                    raw = WideToUtf8(std::wstring(buf.data()));
                }
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
    }
    return SanitizeSingleLine(raw);
}

// 写剪贴板（CF_UNICODETEXT）。SetClipboardData 成功后内存归系统所有，
// 不能再 GlobalFree —— 只有失败时才由本进程释放，否则就是双重释放。
bool WriteClipboardText(HWND owner, const std::string& utf8) {
    std::wstring wide = Utf8ToWide(utf8);
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* p = GlobalLock(h)) {
                std::memcpy(p, wide.c_str(), bytes);
                GlobalUnlock(h);
                ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
            }
            if (!ok) GlobalFree(h);
        }
    }
    CloseClipboard();
    return ok;
}

bool ClipboardHasText() {
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE ||
           IsClipboardFormatAvailable(CF_TEXT) != FALSE;
}

// 把文本截断到 max_w 像素内，超长以 … 结尾。按码点切，不会切坏汉字。
std::string EllipsizeText(Canvas& canvas, const std::string& text,
                          int font_size, bool bold, int max_w) {
    if (max_w <= 0) return "";
    if ((int)canvas.MeasureText(text, font_size, bold) <= max_w) return text;
    const std::string dots = "…";
    int dots_w = (int)canvas.MeasureText(dots, font_size, bold);
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t next = Utf8NextIndex(text, i);
        std::string candidate = out + text.substr(i, next - i);
        if ((int)canvas.MeasureText(candidate, font_size, bold) + dots_w >
            max_w) {
            break;
        }
        out = candidate;
        i = next;
    }
    return out + dots;
}

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t type = 0x4D42;
    uint32_t size = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t off_bits = 54;
};
struct BmpInfoHeader {
    uint32_t size = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bit_count = 32;
    uint32_t compression = 0;
    uint32_t size_image = 0;
    int32_t x_ppm = 0;
    int32_t y_ppm = 0;
    uint32_t clr_used = 0;
    uint32_t clr_important = 0;
};
#pragma pack(pop)

// 写 32 位 BMP。GDI 的 DIB 里 alpha 通道通常是 0，直接存盘会被图片查看器
// 当成全透明，所以这里把 alpha 统一补成 255，方便转 PNG 后肉眼核对。
bool SaveBgraBmp(const std::string& path, const uint8_t* bgra, int w, int h) {
    if (!bgra || w <= 0 || h <= 0) return false;
    std::vector<uint8_t> copy((size_t)w * h * 4);
    for (size_t i = 0; i < copy.size(); i += 4) {
        copy[i + 0] = bgra[i + 0];
        copy[i + 1] = bgra[i + 1];
        copy[i + 2] = bgra[i + 2];
        copy[i + 3] = 255;
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    BmpFileHeader fh;
    BmpInfoHeader ih;
    ih.width = w;
    ih.height = -h;  // top-down
    ih.size_image = (uint32_t)copy.size();
    fh.size = sizeof(fh) + sizeof(ih) + ih.size_image;
    f.write((const char*)&fh, sizeof(fh));
    f.write((const char*)&ih, sizeof(ih));
    f.write((const char*)copy.data(), (std::streamsize)copy.size());
    return f.good();
}

void LogStartup(const std::string& msg) {
#ifdef ZB_DEBUG
    char module[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, module, MAX_PATH);
    std::string path = module;
    size_t slash = path.find_last_of("\\/");
    path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) +
           "zero-debug.log";
    std::ofstream f(path, std::ios::app);
    f << msg << "\n";
#else
    (void)msg;
#endif
}

struct NavResult {
    int tab_index = -1;
    int seq = -1;
    std::string requested;
    std::string html;
    std::string final_url;
    std::string error;
    bool add_history = false;
    std::map<std::string, std::shared_ptr<Image>> images;
    // 外链脚本正文（<script src>），与图片一样由导航线程取回。
    std::map<std::string, std::string> scripts;
    // 分段耗时（毫秒）：定位"加载慢"到底慢在哪一段。
    // true 表示这是"资源阶段"的回执（图片 + 外链脚本），不是导航完成。
    bool assets_phase = false;
    double ms_html = 0;
    double ms_images = 0;
    double ms_scripts = 0;
    int image_count = 0;
    int script_count = 0;
};

// 导航分段耗时（毫秒）。用户反馈"加载慢"时必须能一眼看出慢在哪一段：
// HTML 传输 / 图片 + 背景图 / 外链脚本。
static void LogNavTiming(const NavResult& r) {
    std::printf("[nav] total=%.0fms html=%.0fms images=%.0fms(%d) scripts=%.0fms(%d)\n",
                r.ms_html + r.ms_images + r.ms_scripts, r.ms_html, r.ms_images,
                r.image_count, r.ms_scripts, r.script_count);
}


bool FileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return (bool)f;
}

std::string NormalizeUrlInput(const std::string& raw) {
    std::string u = Trim(raw);
    if (u.empty()) return "browser://home";
    if (StartsWith(u, "browser://") || StartsWith(u, "about:") ||
        StartsWith(u, "file://") || StartsWith(u, "data:") ||
        StartsWith(u, "http://") || StartsWith(u, "https://")) {
        return u;
    }
    if (FileExists(u)) return u;
    bool looks_like_host = u.find(' ') == std::string::npos &&
                           (u.find('.') != std::string::npos ||
                            StartsWith(u, "localhost"));
    if (looks_like_host) return "https://" + u;
    return u;
}

std::string ReadFileUtf8(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

std::string PercentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]);
            int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string StripFragment(const std::string& url) {
    size_t p = url.find('#');
    return p == std::string::npos ? url : url.substr(0, p);
}

std::string NormalizePath(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    std::vector<std::string> out;
    for (const auto& p : parts) {
        if (p == ".") continue;
        if (p == "..") {
            if (!out.empty()) out.pop_back();
            continue;
        }
        out.push_back(p);
    }
    std::string res;
    for (const auto& p : out) {
        res += "/";
        res += p;
    }
    return res.empty() ? "/" : res;
}

std::string ResolveUrl(const std::string& base, const std::string& href_raw) {
    std::string href = Trim(href_raw);
    if (href.empty()) return StripFragment(base);
    size_t frag = href.find('#');
    if (frag != std::string::npos) href = href.substr(0, frag);
    if (href.empty()) return StripFragment(base);
    if (href.find("://") != std::string::npos || StartsWith(href, "data:") ||
        StartsWith(href, "about:") || StartsWith(href, "browser:") ||
        StartsWith(href, "mailto:") || StartsWith(href, "javascript:")) {
        return href;
    }
    std::string base_clean = StripFragment(base);
    size_t scheme = base_clean.find("://");
    if (scheme == std::string::npos) {
        size_t slash = base_clean.find_last_of("/\\");
        std::string dir = slash == std::string::npos
                              ? ""
                              : base_clean.substr(0, slash + 1);
        return dir + href;
    }
    size_t origin_start = scheme + 3;
    size_t slash = base_clean.find('/', origin_start);
    std::string origin = slash == std::string::npos
                             ? base_clean
                             : base_clean.substr(0, slash);
    if (StartsWith(href, "//")) {
        return base_clean.substr(0, scheme + 1) + href;
    }
    std::string path_part = href;
    std::string query;
    size_t q = path_part.find('?');
    if (q != std::string::npos) {
        query = path_part.substr(q);
        path_part = path_part.substr(0, q);
    }
    if (StartsWith(path_part, "/")) {
        return origin + NormalizePath(path_part) + query;
    }
    std::string base_path = slash == std::string::npos
                                ? "/"
                                : base_clean.substr(slash);
    size_t bq = base_path.find('?');
    if (bq != std::string::npos) base_path = base_path.substr(0, bq);
    size_t last = base_path.find_last_of('/');
    std::string dir = last == std::string::npos
                          ? "/"
                          : base_path.substr(0, last + 1);
    return origin + NormalizePath(dir + path_part) + query;
}

// 表单编码：x-www-form-urlencoded（空格用 +，其余非字母数字按 %XX）。
std::string FormEncode(const std::string& v) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : v) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '*') {
            out.push_back((char)c);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string ErrorHtml(const std::string& url, const std::string& error) {
    std::string e = error.empty() ? "无法加载页面" : error;
    std::string escaped;
    for (char c : e) {
        if (c == '&') escaped += "&amp;";
        else if (c == '<') escaped += "&lt;";
        else if (c == '>') escaped += "&gt;";
        else escaped.push_back(c);
    }
    return
        "<!DOCTYPE html><html><head><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f8fafc;color:#0f172a;"
        "font-family:Segoe UI;padding:48px}h1{font-size:26px;color:#dc2626}"
        "p{font-size:15px;line-height:1.7;color:#475569}.box{background:#ffffff;"
        "border:1px solid #e2e8f0;border-radius:10px;padding:20px;max-width:720px}"
        "code{background:#e2e8f0;padding:2px 6px;border-radius:4px}"
        "</style></head><body><div class=\"box\"><h1>页面加载失败</h1>"
        "<p>地址: <code>" + url + "</code></p><p>" + escaped + "</p></div></body></html>";
}

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

std::string ExtractAttr(const std::string& tag, const std::string& name) {
    std::string lower = ToLowerAscii(tag);
    size_t p = lower.find(name + "=");
    if (p == std::string::npos) return "";
    size_t v = p + name.size() + 1;
    while (v < tag.size() && (tag[v] == ' ' || tag[v] == '\t' ||
                              tag[v] == '\n' || tag[v] == '\r')) {
        v++;
    }
    if (v >= tag.size()) return "";
    if (tag[v] == '"' || tag[v] == '\'') {
        char q = tag[v++];
        size_t e = tag.find(q, v);
        return e == std::string::npos ? tag.substr(v) : tag.substr(v, e - v);
    }
    size_t e = v;
    while (e < tag.size() && tag[e] != ' ' && tag[e] != '\t' &&
           tag[e] != '>' && tag[e] != '\n' && tag[e] != '\r') {
        e++;
    }
    return tag.substr(v, e - v);
}

std::string ReadFileBinary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

std::string Base64Decode(const std::string& s) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> map(128, -1);
    for (int i = 0; i < 64; i++) map[(unsigned char)table[i]] = i;
    std::string out;
    int bits = 0;
    int acc = 0;
    for (char raw : s) {
        unsigned char c = (unsigned char)raw;
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        if (c >= 128 || map[c] < 0) return "";
        acc = (acc << 6) | map[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((acc >> bits) & 0xFF));
        }
    }
    return out;
}

// 在导航线程里扫描 <script src>，取回正文文本。
// 与图片一样只做传输：脚本的解析与执行在页面线程（js_dom）。
void LoadScriptsForHtml(const std::string& html, const std::string& base_url,
                        std::map<std::string, std::string>& out) {
    std::string lower = ToLowerAscii(html);
    size_t pos = 0;
    std::vector<std::string> urls;
    while (true) {
        size_t tag = lower.find("<script", pos);
        if (tag == std::string::npos) break;
        size_t end = lower.find('>', tag);
        if (end == std::string::npos) break;
        std::string head = html.substr(tag, end - tag + 1);
        pos = end + 1;
        std::string src = Trim(ExtractAttr(head, "src"));
        if (src.empty()) continue;
        std::string type = Lower(Trim(ExtractAttr(head, "type")));
        if (!type.empty() && type.find("javascript") == std::string::npos &&
            type != "module") {
            continue;
        }
        std::string abs = ResolveUrl(base_url, src);
        if (!StartsWith(abs, "http://") && !StartsWith(abs, "https://") &&
            !StartsWith(abs, "file://")) {
            continue;
        }
        if (out.count(abs)) continue;
        bool dup = false;
        for (const std::string& u : urls) {
            if (u == abs) dup = true;
        }
        if (!dup) urls.push_back(abs);
    }
    if (urls.empty()) return;
    std::vector<std::string> bodies(urls.size());
    std::vector<std::thread> pool;
    size_t workers = std::min<size_t>(8, urls.size());
    for (size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&, w]() {
            for (size_t i = w; i < urls.size(); i += workers) {
                if (StartsWith(urls[i], "file://")) {
                    bodies[i] = ReadFileUtf8(urls[i].substr(7));
                    continue;
                }
                FetchResult res;
                FetchUrlWithCookies(urls[i], &res, 10000);
                if (res.ok) bodies[i] = res.html;
            }
        });
    }
    for (auto& t : pool) t.join();
    for (size_t i = 0; i < urls.size(); ++i) {
        if (!bodies[i].empty()) out[urls[i]] = bodies[i];
    }
}

// 在导航线程里扫描 <img>，抓取并解码图片字节。只做传输/解码：
// HTML 结构、布局、绘制仍由浏览器自己负责。
void LoadImagesForHtml(const std::string& html, const std::string& base_url,
                       std::map<std::string, std::shared_ptr<Image>>& out) {
    // 先把需要抓的 URL 收集齐，再并行取回。
    // 早期实现是边扫边串行 fetch：每个资源都新建 WinHTTP 会话（重复 TLS 握手），
    // 真实站点几十个图片/CSS url() 会把首屏拖到几十秒甚至永远加载不完。
    std::vector<std::string> remote;
    auto need_remote = [&](const std::string& abs) {
        if (!StartsWith(abs, "http://") && !StartsWith(abs, "https://")) {
            return false;
        }
        if (out.count(abs)) return false;
        for (const auto& u : remote) {
            if (u == abs) return false;
        }
        remote.push_back(abs);
        return true;
    };

    std::string lower = ToLowerAscii(html);
    size_t pos = 0;
    while (true) {
        size_t img = lower.find("<img", pos);
        if (img == std::string::npos) break;
        size_t end = lower.find('>', img);
        if (end == std::string::npos) break;
        std::string tag = html.substr(img, end - img + 1);
        // 与 dom.h 的 ImageSourceOf 保持一致：src 为空/占位时回退到懒加载属性，
        // 否则 B 站这类站点的缩略图（真实地址在 data-src）会全部缺失。
        std::string src;
        static const char* kSrcAttrs[] = {
            "src",           "data-src",       "data-original",
            "data-lazy-src", "data-actualsrc", "data-original-src",
            "data-echo"};
        for (const char* a : kSrcAttrs) {
            std::string v = Trim(ExtractAttr(tag, a));
            if (v.empty()) continue;
            std::string lv = ToLowerAscii(v);
            if (lv.find("blank.gif") != std::string::npos ||
                lv.find("placeholder") != std::string::npos ||
                lv.find("r0lgodlhaqab") != std::string::npos) {
                continue;
            }
            src = v;
            break;
        }
        if (src.empty()) {
            std::string ss = Trim(ExtractAttr(tag, "data-srcset"));
            if (ss.empty()) ss = Trim(ExtractAttr(tag, "srcset"));
            if (!ss.empty()) {
                size_t comma = ss.find(',');
                std::string first = Trim(ss.substr(0, comma));
                size_t sp = first.find(' ');
                src = sp == std::string::npos ? first : first.substr(0, sp);
            }
        }
        pos = end + 1;
        if (src.empty()) continue;
        std::string abs = ResolveUrl(base_url, src);
        if (out.count(abs)) continue;

        std::shared_ptr<Image> image;
        if (StartsWith(src, "data:image/")) {
            size_t comma = src.find(',');
            if (comma != std::string::npos) {
                image = DecodeImage(Base64Decode(src.substr(comma + 1)));
            }
        } else if (StartsWith(abs, "file://")) {
            std::string path = abs.substr(7);
            if (path.size() > 2 && (path[0] == '/' || path[0] == '\\') &&
                path[2] == ':') {
                path = path.substr(1);
            }
            path = PercentDecode(path);
            std::string bytes = ReadFileBinary(path);
            if (!bytes.empty()) image = DecodeImage(bytes);
        } else {
            need_remote(abs);
        }

        if (image) {
            image->source_url = abs;
            out[abs] = std::move(image);
        }
    }

    // CSS 里的 background-image: url(...)（外部样式表已内联进 <style>）。
    // 相对路径以页面地址为基准（外部 CSS 内部的相对 url 已在内联时改写为绝对）。
    std::string lower_all = ToLowerAscii(html);
    pos = 0;
    while (true) {
        size_t up = lower_all.find("url(", pos);
        if (up == std::string::npos) break;
        size_t open = html.find('(', up);
        if (open == std::string::npos) break;
        size_t close = html.find(')', open);
        if (close == std::string::npos) break;
        std::string u = html.substr(open + 1, close - open - 1);
        pos = close + 1;
        if (u.size() >= 2 &&
            ((u.front() == '"' && u.back() == '"') ||
             (u.front() == '\'' && u.back() == '\''))) {
            u = u.substr(1, u.size() - 2);
        }
        u = Trim(u);
        if (u.empty() || StartsWith(u, "data:")) continue;
        std::string abs = ResolveUrl(base_url, u);
        if (out.count(abs)) continue;
        if (StartsWith(abs, "file://")) {
            std::string path = abs.substr(7);
            if (path.size() > 2 && (path[0] == '/' || path[0] == '\\') &&
                path[2] == ':') {
                path = path.substr(1);
            }
            path = PercentDecode(path);
            std::string bytes = ReadFileBinary(path);
            if (!bytes.empty()) {
                auto image = DecodeImage(bytes);
                if (image) {
                    image->source_url = abs;
                    out[abs] = std::move(image);
                }
            }
        } else {
            need_remote(abs);
        }
    }

    if (remote.empty()) return;
    std::vector<FetchResult> results;
    // 并发与超时直接决定"首屏要等多久"：实测某真实站点首页 31 张图，
    // 6 线程 + 15s 超时需要 6.8s 才能抓完。提到 12 线程、单资源 10s 超时，
    // 避免一张慢图拖住整页（本项目目前仍是"等齐再画"，见 README 踩坑）。
    FetchManyParallel(remote, 12, &results, 10000, true, base_url);
    for (size_t i = 0; i < remote.size() && i < results.size(); ++i) {
        if (results[i].html.empty()) continue;
        auto image = DecodeImage(results[i].html);
        if (image) {
            image->source_url = remote[i];
            out[remote[i]] = std::move(image);
        }
    }
}

// 把外部 CSS 里的相对 url(...) 补成绝对地址。
// 外部样式表会被内联进 <style>，之后 url() 会按“页面地址”解析，
// 而 CSS 里的相对路径本应以“CSS 文件自身的地址”为基准——不修就会出现
// 背景图/字体全部 404 的情况（真实站点几乎都是相对路径）。
std::string AbsolutizeCssUrls(const std::string& css,
                              const std::string& css_url) {
    std::string out;
    std::string lower = ToLowerAscii(css);
    size_t pos = 0;
    while (true) {
        size_t up = lower.find("url(", pos);
        if (up == std::string::npos) {
            out += css.substr(pos);
            break;
        }
        out += css.substr(pos, up - pos);
        size_t open = css.find('(', up);
        size_t close = css.find(')', open);
        if (open == std::string::npos || close == std::string::npos) {
            out += css.substr(up);
            break;
        }
        std::string v = Trim(css.substr(open + 1, close - open - 1));
        std::string quote;
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') ||
                              (v.front() == '\'' && v.back() == '\''))) {
            quote = v.substr(0, 1);
            v = v.substr(1, v.size() - 2);
        }
        std::string lv = ToLowerAscii(v);
        if (!v.empty() && !StartsWith(lv, "data:") && !StartsWith(lv, "http://") &&
            !StartsWith(lv, "https://") && !StartsWith(v, "//")) {
            v = ResolveUrl(css_url, v);
        }
        out += "url(" + quote + v + quote + ")";
        pos = close + 1;
    }
    return out;
}

// Pull in external stylesheets so the self-built CSS engine sees them.
std::string InlineExternalStylesheets(const std::string& html,
                                      const std::string& base_url) {
    std::string lower = ToLowerAscii(html);
    std::string injected;
    size_t pos = 0;
    int count = 0;
    while (count < 6) {
        size_t link = lower.find("<link", pos);
        if (link == std::string::npos) break;
        size_t end = lower.find('>', link);
        if (end == std::string::npos) break;
        std::string tag = html.substr(link, end - link + 1);
        std::string tag_lower = lower.substr(link, end - link + 1);
        pos = end + 1;
        if (tag_lower.find("stylesheet") == std::string::npos) continue;
        std::string href = ExtractAttr(tag, "href");
        if (href.empty() || StartsWith(href, "data:")) continue;
        std::string css_url = ResolveUrl(base_url, href);
        if (!StartsWith(css_url, "http://") && !StartsWith(css_url, "https://")) {
            continue;
        }
        FetchResult css;
        FetchUrlWithCookies(css_url, &css, 10000, base_url);
        if (!css.html.empty()) {
            injected += "\n<style>\n";
            injected += AbsolutizeCssUrls(css.html, css_url);
            injected += "\n</style>\n";
            count++;
        }
    }
    if (injected.empty()) return html;
    size_t head_end = lower.find("</head>");
    std::string out = html;
    if (head_end != std::string::npos) {
        out.insert(head_end, injected);
    } else {
        out = injected + out;
    }
    return out;
}

// 处理「用 JS 设置 Cookie 再重载」的反爬挑战页（洛谷 / 部分国内站点使用）。
// 这不是 JS 引擎，只识别挑战页里那几种固定写法：
//   var X = ["\x61\x62", ...]          字符串数组（含 \xNN / \uNNNN 转义）
//   xxx.cookie = "name=value; ..."     写 Cookie（含 xxx[Y[0]].cookie 这类间接写法）
//   window.open("URL","_self") / location.href="URL" / location.replace("URL")
// 命中后由 LoadUrlSource 把 Cookie 写进 jar 并重新抓取，等价于浏览器执行这段脚本。
struct CookieChallenge {
    bool found = false;
    std::string cookie;
    std::string redirect;
};

std::string UnescapeJsString(const std::string& s) {
    std::string out;
    auto hexval = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        char c = s[++i];
        if (c == 'x' && i + 2 < s.size()) {
            int hi = hexval(s[i + 1]);
            int lo = hexval(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        if (c == 'n') { out.push_back('\n'); continue; }
        if (c == 'r') { out.push_back('\r'); continue; }
        if (c == 't') { out.push_back('\t'); continue; }
        out.push_back(c);
    }
    return out;
}

// 取 key 之后第一个引号串（跳过 = 与空白），并做 JS 转义还原。
std::string FirstQuotedAfter(const std::string& html, size_t from) {
    size_t q1 = html.find('"', from);
    if (q1 == std::string::npos) return "";
    size_t q2 = html.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return UnescapeJsString(html.substr(q1 + 1, q2 - q1 - 1));
}

CookieChallenge DetectCookieChallenge(const std::string& html) {
    CookieChallenge ch;
    // 挑战页都很小；限制体积，避免在正常页面里误伤。
    if (html.empty() || html.size() > 8192) return ch;
    std::string lower = ToLowerAscii(html);
    if (lower.find("cookie") == std::string::npos) return ch;

    // .cookie = "..." 赋值（直接或经数组间接引用都归结为“找到那段字符串”）
    size_t ck = lower.find(".cookie");
    while (ck != std::string::npos) {
        std::string v = FirstQuotedAfter(html, ck);
        if (v.find('=') != std::string::npos) {
            ch.cookie = v;
            ch.found = true;
        }
        ck = lower.find(".cookie", ck + 7);
    }

    // 跳转目标
    const char* keys[] = {"window.open(", "location.href", "location.replace(",
                          "location.assign(", "document.location"};
    for (const char* key : keys) {
        size_t p = lower.find(ToLowerAscii(key));
        if (p == std::string::npos) continue;
        std::string u = FirstQuotedAfter(html, p);
        if (!u.empty()) {
            ch.redirect = u;
            ch.found = true;
            break;
        }
    }
    return ch;
}

bool LoadUrlSource(const std::string& raw_url, std::string* html,
                   std::string* final_url, std::string* error) {
    std::string u = NormalizeUrlInput(raw_url);
    if (u.empty() || StartsWith(u, "browser://") || StartsWith(u, "about:")) {
        *html = BuiltinHtml(u.empty() ? "home" : u);
        *final_url = u.empty() ? "browser://home" : u;
        error->clear();
        return true;
    }
    if (StartsWith(u, "file://")) {
        std::string path = u.substr(7);
        if (path.size() > 2 && (path[0] == '/' || path[0] == '\\') &&
            path[2] == ':') {
            path = path.substr(1);
        }
        path = PercentDecode(path);
        std::string data = ReadFileUtf8(path);
        if (data.empty()) {
            *error = "文件不存在或为空: " + path;
            *html = ErrorHtml(u, *error);
            *final_url = u;
            return false;
        }
        *html = data;
        *final_url = u;
        error->clear();
        return true;
    }
    if (StartsWith(u, "data:text/html")) {
        size_t comma = u.find(',');
        if (comma == std::string::npos) {
            *error = "data URL 缺少逗号";
            *html = ErrorHtml(u, *error);
            *final_url = u;
            return false;
        }
        *html = PercentDecode(u.substr(comma + 1));
        *final_url = u;
        error->clear();
        return true;
    }
    if (StartsWith(u, "http://") || StartsWith(u, "https://")) {
        FetchResult res;
        FetchUrlWithCookies(u, &res, 15000);
        if (!res.html.empty()) {
            std::string final = res.final_url.empty() ? u : res.final_url;
            // 反爬挑战页（洛谷等）：响应是个小页面，脚本里设置 Cookie 再重载。
            // 我们不是 JS 引擎，只识别这种固定写法，把 Cookie 写进 jar 后重新抓一次。
            for (int round = 0; round < 2; ++round) {
                CookieChallenge ch = DetectCookieChallenge(res.html);
                if (!ch.found || ch.redirect.empty()) break;
                if (!ch.cookie.empty()) CookieJarAbsorbText(final, ch.cookie);
                std::string next = ResolveUrl(final, ch.redirect);
                if (next.empty() || next == final) break;
                FetchResult again;
                if (!FetchUrlWithCookies(next, &again, 15000, final)) break;
                if (again.html.size() <= res.html.size()) break;
                res = again;
                final = again.final_url.empty() ? next : again.final_url;
            }
            *html = InlineExternalStylesheets(res.html, final);
            *final_url = final;
            error->clear();
            return true;
        }
        *error = res.error.empty()
                     ? "服务器返回 HTTP " + std::to_string(res.status)
                     : res.error;
        *html = ErrorHtml(u, *error);
        *final_url = u;
        return false;
    }

    std::string data = ReadFileUtf8(u);
    if (!data.empty()) {
        *html = data;
        *final_url = u;
        error->clear();
        return true;
    }
    *error = "无法解析这个地址，支持 browser://、file://、data:text/html、http://、https://";
    *html = ErrorHtml(u, *error);
    *final_url = u;
    return false;
}

}  // namespace

BrowserApp::BrowserApp(HINSTANCE inst) : inst_(inst) {}

void BrowserApp::Log(const std::string& msg) {
    LogStartup(msg);
}

bool BrowserApp::CreateMainWindow(bool visible, int width, int height) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &BrowserApp::StaticWndProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconA(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassW;
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc)) {
        LogStartup("RegisterClassExW failed err=" + std::to_string(GetLastError()));
        return false;
    }

    hwnd_ = CreateWindowExW(
        0, kClassW, L"Zero Browser", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, inst_, this);
    if (!hwnd_) {
        LogStartup("CreateWindowExW failed err=" + std::to_string(GetLastError()));
        return false;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);

    measure_dc_ = CreateCompatibleDC(nullptr);
    measure_canvas_ = new GdiCanvas(measure_dc_, 1, 1);

    SetTimer(hwnd_, caret_timer_, 450, nullptr);
    SetTimer(hwnd_, video_timer_, 33, nullptr);
    // JS 定时器心跳。60ms 是 setTimeout(fn,0) 与 requestAnimationFrame 的
    // 实际粒度下限；页面没有脚本时 OnJsTick 只做一次判断就返回。
    SetTimer(hwnd_, js_timer_, 60, nullptr);
    TabState home;
    home.url = "browser://home";
    tabs_.push_back(std::move(home));
    if (visible) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    } else {
        // 隐藏窗口不会收到 WM_PAINT，但 WM_SIZE / WM_TIMER 照常工作，
        // 足以驱动布局、定时器与媒体线程；画面由 HeadlessShot 主动渲染。
        SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void BrowserApp::PumpMessages(int ms) {
    DWORD end = GetTickCount() + (DWORD)(ms < 0 ? 0 : ms);
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if ((long)(GetTickCount() - end) >= 0) break;
        Sleep(5);
    }
}

namespace {

// 收集布局树里所有 <video> 盒子，供诊断输出精确坐标。
void CollectVideoBoxes(const Box* box, std::vector<const Box*>* out) {
    if (!box || !out) return;
    if (box->node && box->node->tag == "video") out->push_back(box);
    for (const auto& c : box->children) CollectVideoBoxes(c.get(), out);
}

// 转储布局树：同时给出“文档坐标”和渲染器实际使用的“屏幕坐标”。
// fixed 子树使用视口局部坐标，转储时也照此处理，避免误判。
void DumpBoxTree(const Box* box, const Rect& viewport, int scroll, int depth,
                 bool fixed_ctx = false) {
    if (!box || depth > 12) return;
    bool is_fixed = fixed_ctx || box->fixed;
    int screen_x = viewport.x + box->rect.x;
    int screen_y =
        viewport.y + box->rect.y - (is_fixed ? 0 : scroll);
    std::printf(
        "%*sbox tag=%s doc=%d,%d,%d,%d screen=%d,%d bg=%s color=%s%s\n",
        depth * 2, "", box->node ? box->node->tag.c_str() : "(anon)",
        box->rect.x, box->rect.y, box->rect.w, box->rect.h, screen_x, screen_y,
        box->style.background.empty() ? "-" : box->style.background.c_str(),
        box->style.color.c_str(), is_fixed ? " fixed" : "");
    for (const auto& r : box->runs) {
        int rx = viewport.x + r.rect.x;
        int ry = viewport.y + r.rect.y - (is_fixed ? 0 : scroll);
        std::printf("%*s  run %s%s%s rect=%d,%d,%d,%d screen=%d,%d\n",
                    depth * 2, "",
                    r.image ? "img " : (r.image_missing ? "img-missing " : "text "),
                    r.image ? "" : (r.image_missing ? "" : "\""),
                    r.image ? "" : (r.image_missing ? "" : r.text.c_str()),
                    r.rect.x, r.rect.y, r.rect.w, r.rect.h, rx, ry);
    }
    for (const auto& c : box->children)
        DumpBoxTree(c.get(), viewport, scroll, depth + 1, is_fixed);
}

}  // namespace

void BrowserApp::LogMediaState(const char* tag) const {
    if (active_ < 0 || active_ >= (int)tabs_.size()) return;
    const TabState& tab = tabs_[active_];
    std::printf("[%s] url=%s tabs=%d content_height=%d scroll=%d\n", tag,
                tab.url.c_str(), (int)tabs_.size(), tab.page.ContentHeight(),
                tab.scroll);
    std::printf("  viewport=%d,%d,%d,%d window=%dx%d\n", page_view_.x,
                page_view_.y, page_view_.w, page_view_.h, width_, height_);
    std::vector<const Box*> videos;
    CollectVideoBoxes(tab.page.RootBox(), &videos);
    for (size_t i = 0; i < videos.size(); ++i) {
        const Box& b = *videos[i];
        // 同时给出“文档坐标”和“窗口坐标”，方便直接算点击位置。
        std::printf(
            "  videoBox[%zu] doc=%d,%d,%d,%d screen=%d,%d,%d,%d "
            "controls_y_from=%d\n",
            i, b.rect.x, b.rect.y, b.rect.w, b.rect.h, page_view_.x + b.rect.x,
            page_view_.y + b.rect.y - tab.scroll, b.rect.w, b.rect.h,
            page_view_.y + b.rect.y + b.rect.h - 34 - tab.scroll);
    }
    for (const auto& kv : tab.page.MediaPlayers()) {
        MediaPlayer* player = kv.second.get();
        if (!player) continue;
        MediaInfo info = player->Info();
        std::vector<uint8_t> frame;
        int fw = 0;
        int fh = 0;
        bool has = player->CopyFrame(&frame, &fw, &fh);
        std::printf(
            "  media node=<%s> playing=%d ready=%d failed=%d muted=%d "
            "pos=%.2f dur=%.2f frame=%dx%d has_frame=%d audio=%d%s%s\n",
            kv.first && kv.first->tag.size() ? kv.first->tag.c_str() : "?",
            player->IsPlaying() ? 1 : 0, player->IsReady() ? 1 : 0,
            player->Failed() ? 1 : 0, player->Muted() ? 1 : 0,
            player->Position(), player->Duration(), fw, fh, has ? 1 : 0,
            info.has_audio ? 1 : 0, player->Failed() ? " err=" : "",
            player->Failed() ? player->Error().c_str() : "");
    }
}

bool BrowserApp::HeadlessShot(const ShotOptions& opt) {
    if (!CreateMainWindow(false, opt.width, opt.height)) {
        std::printf("HeadlessShot: 创建隐藏窗口失败\n");
        return false;
    }
    // 隐藏窗口可能收不到 WM_SIZE（尺寸未变化时系统不会重发），这里显式设定
    // 视口尺寸，保证布局、命中测试和点击模拟都在确定的状态下进行。
    OnSize(opt.width, opt.height);

    HDC mem = CreateCompatibleDC(nullptr);
    if (!mem) {
        std::printf("HeadlessShot: CreateCompatibleDC 失败\n");
        return false;
    }
    GdiCanvas canvas(mem, opt.width, opt.height);

    HDC dibdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = opt.width;
    bi.bmiHeader.biHeight = -opt.height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib =
        CreateDIBSection(dibdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_dib = dib ? SelectObject(dibdc, dib) : nullptr;

    NavigateTo(opt.url, true);
    PumpMessages(opt.wait_ms);

    if (active_ >= 0 && active_ < (int)tabs_.size() && opt.scroll > 0) {
        TabState& tab = tabs_[active_];
        int max_scroll = std::max(0, tab.page.ContentHeight() - page_view_.h);
        tab.scroll = std::max(0, std::min(opt.scroll, max_scroll));
        InvalidateRect(hwnd_, nullptr, FALSE);
        PumpMessages(50);
    }

    // 地址栏输入回归：先（可选）设置内容，再（可选）聚焦，最后逐字符走 OnChar。
    if (!opt.set_address.empty() || opt.focus_address) {
        if (!opt.set_address.empty()) {
            address_text_ = opt.set_address;
            if (address_text_ == "\\empty") address_text_.clear();
        }
        if (opt.focus_address) address_focused_ = true;
        caret_ = (int)address_text_.size();
        std::printf("[address] set='%s' focused=%d caret=%d\n",
                    address_text_.c_str(), address_focused_ ? 1 : 0, caret_);
    }
    if (!opt.type_text.empty()) {
        address_focused_ = true;
        int typed = 0;
        for (size_t i = 0; i < opt.type_text.size();) {
            unsigned char c = (unsigned char)opt.type_text[i];
            size_t len = 1;
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            std::string one = opt.type_text.substr(i, len);
            std::wstring wide = Utf8ToWide(one);
            for (wchar_t wc : wide) OnChar(wc);
            typed++;
            i += len;
        }
        std::string hex;
        for (unsigned char c : address_text_) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02x ", c);
            hex += buf;
        }
        std::printf("[address] typed=%d bytes=%zu caret=%d hex=%s\n", typed,
                    address_text_.size(), caret_, hex.c_str());
        PumpMessages(100);
    }
    if (opt.backspace > 0) {
        address_focused_ = true;
        for (int i = 0; i < opt.backspace; ++i) OnKey(VK_BACK);
        std::printf("[address] backspace=%d bytes=%zu caret=%d\n",
                    opt.backspace, address_text_.size(), caret_);
        PumpMessages(100);
    }

    // 地址栏剪贴板回归：--clipboard 先把文本写进系统剪贴板，之后全部走真实的
    // OnKeyEx 路径（无窗口会话里没有键盘，GetKeyState 恒为 0，所以必须显式传
    // 修饰键，否则测的就不是用户真正按下的那条分支）。
    if (!opt.clipboard.empty() || opt.paste > 0 || opt.select_all || opt.copy ||
        opt.cut) {
        address_focused_ = true;
        if (!opt.clipboard.empty()) {
            bool ok = WriteClipboardText(hwnd_, opt.clipboard);
            // 带上 GetLastError：OpenClipboard 被别的进程占用时会直接失败，
            // 只打 ok=0 没法定位（实测踩到过 ok=0 但不知道为什么）。
            std::printf("[address] clipboard-set bytes=%zu ok=%d err=%lu\n",
                        opt.clipboard.size(), ok ? 1 : 0,
                        ok ? 0UL : (unsigned long)GetLastError());
        }
        if (opt.select_all) {
            OnKeyEx('A', true, false);
            std::printf("[address] select-all sel=[%zu,%zu) len=%zu\n",
                        AddressSelBegin(), AddressSelEnd(),
                        AddressSelectedText().size());
        }
        for (int i = 0; i < opt.paste; ++i) OnKeyEx('V', true, false);
        if (opt.paste > 0) {
            std::printf("[address] paste=%d bytes=%zu caret=%d text='%s'\n",
                        opt.paste, address_text_.size(), caret_,
                        address_text_.c_str());
        }
        if (opt.copy) {
            OnKeyEx('C', true, false);
            std::printf("[address] copy-readback='%s'\n",
                        ReadClipboardText(hwnd_).c_str());
        }
        if (opt.cut) {
            OnKeyEx('X', true, false);
            std::printf("[address] cut bytes=%zu caret=%d readback='%s'\n",
                        address_text_.size(), caret_,
                        ReadClipboardText(hwnd_).c_str());
        }
        PumpMessages(100);
    }

    // 快捷键回归：--hotkey 走与真实按键完全相同的 OnKeyEx 分支。
    for (const std::string& hk : opt.hotkeys) {
        bool ctrl = hk.rfind("ctrl+", 0) == 0;
        std::string k = ctrl ? hk.substr(5) : hk;
        UINT key = 0;
        if (k == "f5") key = VK_F5;
        else if (k == "escape") key = VK_ESCAPE;
        else if (k == "enter") key = VK_RETURN;
        else if (k.size() == 1) {
            char c = k[0];
            key = (UINT)((c >= 'a' && c <= 'z') ? c - 32 : c);
        }
        if (!key) {
            std::printf("[hotkey] %s -> 未知按键名，已跳过\n", hk.c_str());
            continue;
        }
        OnKeyEx(key, ctrl, false);
        const TabState& tab = tabs_[active_];
        std::printf(
            "[hotkey] %s focused=%d bytes=%zu caret=%d sel=[%zu,%zu) "
            "loading=%d pending=%d addr='%s' current=%s pending_url='%s'\n",
            hk.c_str(), address_focused_ ? 1 : 0, address_text_.size(), caret_,
            AddressSelBegin(), AddressSelEnd(), tab.loading ? 1 : 0,
            tab.pending_seq, address_text_.c_str(), CurrentUrl().c_str(),
            tab.pending_url.c_str());
        PumpMessages(50);
    }

    // 先截图，再投递点击，最后 dump 布局树 —— dump 要反映"点击之后"的最终状态，
    // 否则用 --click 验证事件处理器时看到的永远是点击前的内容。
    auto shoot = [&](const std::string& path) -> bool {
        canvas.FillRect(0, 0, opt.width, opt.height, 0xf1f5f9);
        Render(canvas);
        BitBlt(dibdc, 0, 0, opt.width, opt.height, mem, 0, 0, SRCCOPY);
        GdiFlush();
        return SaveBgraBmp(path, (const uint8_t*)bits, opt.width, opt.height);
    };

    bool ok = true;
    if (!opt.out.empty()) {
        std::printf("== 截图 1: %s ==\n", opt.out.c_str());
        LogMediaState("before");
        ok = shoot(opt.out) && ok;
        std::printf("saved %s\n", opt.out.c_str());
    }

    if (!opt.clicks.empty()) {
        for (size_t i = 0; i < opt.clicks.size(); ++i) {
            int cx = opt.clicks[i].first;
            int cy = opt.clicks[i].second;
            std::printf("== 模拟点击 %zu: (%d,%d) ==\n", i + 1, cx, cy);
            OnLButtonDown(cx, cy);
            PumpMessages(opt.after_ms);
            char tag[32];
            std::snprintf(tag, sizeof(tag), "click%zu", i + 1);
            LogMediaState(tag);
        }
        if (!opt.out2.empty()) {
            ok = shoot(opt.out2) && ok;
            std::printf("saved %s\n", opt.out2.c_str());
        }
    }

    if (old_dib) SelectObject(dibdc, old_dib);
    if (opt.dump_boxes && active_ >= 0 && active_ < (int)tabs_.size()) {
        const TabState& tab = tabs_[active_];
        std::printf("== 布局树（点击之后）==\n");
        DumpBoxTree(tab.page.RootBox(), page_view_, tab.scroll, 0);
        if (tab.page.Js()) {
            // 标签用英文：--shot 的诊断输出一律英文键名（与 [address] typed=… 一致），
            // 只有脚本名与异常文本是中文内容。README 里引用的就是这一行。
            std::printf("[js] scripts=%d failed=%d listeners=%zu timers=%d\n",
                        tab.page.JsScriptCount(), tab.page.JsScriptFailures(),
                        tab.page.Js()->ListenerCount(),
                        tab.page.Js()->TimerCount());
            std::string jerr = tab.page.JsLastError();
            if (!jerr.empty()) std::printf("[js] last-error: %s\n", jerr.c_str());
            for (const std::string& line : tab.page.Js()->ScriptLog()) {
                std::printf("%s\n", line.c_str());
            }
        }
    }

    if (dib) DeleteObject(dib);
    DeleteDC(dibdc);
    DeleteDC(mem);
    return ok;
}

int BrowserApp::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK BrowserApp::StaticWndProc(HWND hwnd, UINT msg, WPARAM w,
                                           LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
        BrowserApp* app = reinterpret_cast<BrowserApp*>(cs->lpCreateParams);
        if (!app) return FALSE;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
        return TRUE;
    }
    BrowserApp* app =
        reinterpret_cast<BrowserApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app) return DefWindowProcW(hwnd, msg, w, l);
    if (!app->hwnd_) app->hwnd_ = hwnd;
    return app->WndProc(msg, w, l);
}

LRESULT BrowserApp::WndProc(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_SIZE:
            OnSize(LOWORD(l), HIWORD(l));
            return 0;
        case WM_PAINT:
            OnPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            OnLButtonDown(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONUP:
            OnLButtonUp(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONDBLCLK:
            OnLButtonDblClk(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(l), GET_Y_LPARAM(l),
                        (w & MK_LBUTTON) != 0);
            return 0;
        case WM_RBUTTONUP:
            OnRButtonUp(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_MOUSEWHEEL:
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        // 外部工具（输入法、无障碍程序）通过这三条消息驱动编辑框，
        // 我们不是 EDIT 控件，必须自己接，否则粘贴对它们等于没实现。
        case WM_PASTE:
            if (address_focused_) {
                AddressPasteFromClipboard();
                return 0;
            }
            break;
        case WM_COPY:
            if (address_focused_) {
                AddressCopyToClipboard();
                return 0;
            }
            break;
        case WM_CUT:
            if (address_focused_) {
                AddressCutToClipboard();
                return 0;
            }
            break;
        case WM_KEYDOWN:
            OnKey((UINT)w);
            return 0;
        case WM_CHAR:
            OnChar((wchar_t)w);
            return 0;
        case WM_TIMER:
            OnTimer(w);
            return 0;
        case kMsgNavigationDone:
            OnNavigationDone(l);
            return 0;
        case kMsgAssetsDone:
            OnAssetsDone(l);
            return 0;
        case WM_GETMINMAXINFO: {
            auto mm = reinterpret_cast<MINMAXINFO*>(l);
            mm->ptMinTrackSize = {760, 500};
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd_, caret_timer_);
            KillTimer(hwnd_, video_timer_);
            delete measure_canvas_;
            if (measure_dc_) DeleteDC(measure_dc_);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}

void BrowserApp::OnSize(int w, int h) {
    width_ = std::max(0, w);
    height_ = std::max(0, h);
    page_view_ = {0, 78, width_, std::max(0, height_ - 102)};
    status_rect_ = {0, std::max(0, height_ - 24), width_, 24};
    RelayoutActive();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnPaint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    HDC mem = CreateCompatibleDC(dc);
    {
        GdiCanvas canvas(mem, width_, height_);
        Render(canvas);
        BitBlt(dc, 0, 0, width_, height_, canvas.Dc(), 0, 0, SRCCOPY);
    }
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void BrowserApp::Render(Canvas& canvas) {
    canvas.FillRect(0, 0, width_, height_, 0xf1f5f9);
    RenderTabs(canvas);
    RenderToolbar(canvas);
    RenderPage(canvas);
    RenderStatus(canvas);
}

void BrowserApp::RenderTabs(Canvas& canvas) {
    canvas.FillRect(0, 0, width_, 38, 0x0f172a);
    int n = (int)tabs_.size();
    int tab_w = TabWidth();
    int x = 8;
    for (int i = 0; i < n; ++i) {
        bool active = i == active_;
        Rect r{x, 7, tab_w, 28};
        canvas.FillRoundRect(r.x, r.y, r.w, r.h, 7,
                             active ? 0xffffff : 0x1e293b);
        const TabState& tab = tabs_[i];
        std::string label = tab.title.empty() ? "页面" : tab.title;
        // 标题要裁在关闭按钮左侧，否则长标题会压住 × 甚至溢出标签外。
        const int kTabFont = 13;
        int max_w = std::max(12, r.w - 9 - 26);
        label = EllipsizeText(canvas, label, kTabFont, active, max_w);
        int th = canvas.TextHeight(kTabFont, active);
        canvas.Clip(Rect{r.x + 1, r.y + 1, std::max(1, r.w - 2), r.h - 2});
        canvas.DrawText(label, r.x + 9, r.y + std::max(1, (r.h - th) / 2),
                        kTabFont, active ? 0x0f172a : 0x94a3b8, active, false,
                        false);
        canvas.ResetClip();
        int cx = r.x + r.w - 14;
        int cy = r.y + r.h / 2;
        canvas.StrokeLine(cx - 4, cy - 4, cx + 4, cy + 4, active ? 0x64748b : 0x475569);
        canvas.StrokeLine(cx - 4, cy + 4, cx + 4, cy - 4, active ? 0x64748b : 0x475569);
        x += tab_w + 4;
    }
    int plus_x = x + 4;
    canvas.FillRoundRect(plus_x, 7, 30, 28, 7, 0x1e293b);
    canvas.StrokeLine(plus_x + 15, 13, plus_x + 15, 29, 0x94a3b8);
    canvas.StrokeLine(plus_x + 7, 21, plus_x + 23, 21, 0x94a3b8);
}

void BrowserApp::RenderToolbar(Canvas& canvas) {
    canvas.FillRect(0, 38, width_, 40, 0xf8fafc);
    canvas.StrokeLine(0, 77, width_, 77, 0xe2e8f0);

    // Back.
    Rect back{8, 44, 30, 28};
    canvas.FillRoundRect(back.x, back.y, back.w, back.h, 6, 0xeef2f7);
    canvas.StrokeLine(back.x + 20, back.y + 8, back.x + 11, back.y + 14, 0x334155, 2);
    canvas.StrokeLine(back.x + 11, back.y + 14, back.x + 20, back.y + 20, 0x334155, 2);

    // Forward.
    Rect fwd{40, 44, 30, 28};
    canvas.FillRoundRect(fwd.x, fwd.y, fwd.w, fwd.h, 6, 0xeef2f7);
    canvas.StrokeLine(fwd.x + 10, fwd.y + 8, fwd.x + 19, fwd.y + 14, 0x334155, 2);
    canvas.StrokeLine(fwd.x + 19, fwd.y + 14, fwd.x + 10, fwd.y + 20, 0x334155, 2);

    // Reload：留缺口的圆环 + 箭头。
    // 注意 Rect 是 {x, y, w, h}：之前按 {left,top,right,bottom} 传，
    // w/h 变成 94/67，画出一个横跨到地址栏的巨型椭圆。
    Rect reload{72, 44, 30, 28};
    canvas.FillRoundRect(reload.x, reload.y, reload.w, reload.h, 6, 0xeef2f7);
    const double kPi = 3.14159265358979323846;
    int rcx = reload.x + reload.w / 2;
    int rcy = reload.y + reload.h / 2;
    int rr = 7;
    int arc_start = 60;   // 缺口留在右上角
    int arc_sweep = 270;  // 逆时针扫 270°
    canvas.StrokeArc({rcx - rr, rcy - rr, rr * 2, rr * 2}, arc_start,
                     arc_sweep, 0x334155, 2);
    // 箭头落在弧的起点上，方向取该点的切线，画成一个小 V 形箭头
    double a0 = arc_start * kPi / 180.0;
    int tip_x = rcx + (int)std::lround(rr * std::cos(a0));
    int tip_y = rcy - (int)std::lround(rr * std::sin(a0));
    for (int sign = -1; sign <= 1; sign += 2) {
        double ba = a0 + kPi / 2 + sign * 0.45;  // 切线方向 ±26°
        int bx = (int)std::lround(std::cos(ba) * 5.5);
        int by = -(int)std::lround(std::sin(ba) * 5.5);
        canvas.StrokeLine(tip_x, tip_y, tip_x - bx, tip_y - by, 0x334155, 2);
    }

    // Home.
    Rect home{104, 44, 30, 28};
    canvas.FillRoundRect(home.x, home.y, home.w, home.h, 6, 0xeef2f7);
    canvas.StrokeLine(home.x + 6, home.y + 15, home.x + 15, home.y + 7, 0x334155, 2);
    canvas.StrokeLine(home.x + 15, home.y + 7, home.x + 24, home.y + 15, 0x334155, 2);
    canvas.StrokeLine(home.x + 8, home.y + 13, home.x + 8, home.y + 22, 0x334155, 2);
    canvas.StrokeLine(home.x + 22, home.y + 13, home.x + 22, home.y + 22, 0x334155, 2);
    canvas.StrokeLine(home.x + 8, home.y + 22, home.x + 22, home.y + 22, 0x334155, 2);

    // Address bar.
    // 几何一律取文件开头的 kAddr* 常量：原来这里写死了 140/44/30 并重新定义了
    // kAddrFont/kAddrPadX，改一处忘一处就会出现「看到的位置点不中」。
    Rect addr{kAddrX, kToolbarY, std::max(1, width_ - kAddrX - 8), kAddrH};
    uint32_t border = address_focused_ ? 0x2563eb : 0xcbd5e1;
    canvas.FillRoundRect(addr.x, addr.y, addr.w, addr.h, 7, 0xffffff);
    canvas.StrokeRect(addr.x, addr.y, addr.w, addr.h, border);

    // URL 文字：垂直居中，并裁剪在框内（长 URL 不能溢出到工具栏外）。
    // 之前写成固定 addr.y + 8，字号 15 时文字底部正好压在/穿过下边框。
    int text_h = canvas.TextHeight(kAddrFont);
    int text_y = addr.y + std::max(2, (addr.h - text_h) / 2 + 1);
    std::string shown = address_text_;
    canvas.Clip(Rect{addr.x + 1, addr.y + 1, addr.w - 2, addr.h - 2});
    // 选择区高亮画在文字之下：只测量前缀宽度，与 AddressCaretFromX 同源。
    if (address_focused_ && AddressHasSelection()) {
        size_t b = std::min(AddressSelBegin(), shown.size());
        size_t e = std::min(AddressSelEnd(), shown.size());
        if (e > b) {
            int x0 = addr.x + kAddrPadX +
                     (int)canvas.MeasureText(shown.substr(0, b), kAddrFont, false);
            int x1 = addr.x + kAddrPadX +
                     (int)canvas.MeasureText(shown.substr(0, e), kAddrFont, false);
            canvas.FillRect(x0, text_y - 2, std::max(1, x1 - x0), text_h + 3,
                            0xbfdbfe);
        }
    }
    canvas.DrawText(shown, addr.x + kAddrPadX, text_y, kAddrFont,
                    address_focused_ ? 0x0f172a : 0x334155, false, false, false);
    if (address_focused_ && caret_visible_) {
        size_t prefix_len = shown.substr(0, std::min(caret_, (int)shown.size())).size();
        std::string prefix = shown.substr(0, prefix_len);
        int cx = addr.x + kAddrPadX +
                 (int)canvas.MeasureText(prefix, kAddrFont, false);
        canvas.StrokeLine(cx, text_y, cx, text_y + text_h - 3, 0x2563eb, 2);
    }
    canvas.ResetClip();
}

void BrowserApp::RenderPage(Canvas& canvas) {
    if (page_view_.w <= 0 || page_view_.h <= 0) return;
    canvas.FillRect(page_view_.x, page_view_.y, page_view_.w, page_view_.h,
                    0xffffff);
    if (active_ >= 0 && active_ < (int)tabs_.size()) {
        TabState& tab = tabs_[active_];
        tab.page.Paint(&canvas, page_view_, tab.scroll);
        int content = tab.page.ContentHeight();
        if (content > page_view_.h) {
            int track_w = 10;
            int x = page_view_.x + page_view_.w - track_w - 2;
            canvas.FillRect(x, page_view_.y, track_w, page_view_.h, 0xe2e8f0);
            int thumb_h = std::max(30, page_view_.h * page_view_.h / content);
            int max_y = content - page_view_.h;
            int thumb_y = page_view_.y + (max_y == 0 ? 0
                              : (page_view_.h - thumb_h) * tab.scroll / max_y);
            canvas.FillRoundRect(x, thumb_y, track_w, thumb_h, 4, 0x94a3b8);
        }
    }
}

void BrowserApp::RenderStatus(Canvas& canvas) {
    if (status_rect_.h <= 0) return;
    canvas.FillRect(status_rect_.x, status_rect_.y, status_rect_.w,
                    status_rect_.h, 0x0f172a);
    std::string text;
    if (active_ >= 0 && active_ < (int)tabs_.size() && tabs_[active_].loading) {
        text = "加载中... " + DisplayUrl();
    } else {
        text = DisplayUrl();
    }
    if (text.empty()) text = "Zero Browser";
    canvas.DrawText(text, status_rect_.x + 10, status_rect_.y + 5, 12,
                    0x94a3b8, false, false, false);
}

HitTest BrowserApp::HitTestPoint(int x, int y) const {
    if (y < kToolbarTop) {
        int n = (int)tabs_.size();
        int tab_w = TabWidth();
        int x0 = 8;
        for (int i = 0; i < n; ++i) {
            Rect r{x0, 7, tab_w, 28};
            if (r.contains(x, y)) {
                if (x >= r.x + r.w - 20) return {HitArea::CloseTab, i};
                return {HitArea::Tab, i};
            }
            x0 += tab_w + 4;
        }
        if (Rect{x0 + 4, 7, 30, 28}.contains(x, y)) return {HitArea::AddTab, -1};
        return {};
    }
    if (y < kToolbarTop + kToolbarH) {
        if (Rect{8, kToolbarY, kBtnSize, kBtnHeight}.contains(x, y)) {
            return {HitArea::Back, -1};
        }
        if (Rect{40, kToolbarY, kBtnSize, kBtnHeight}.contains(x, y)) {
            return {HitArea::Forward, -1};
        }
        if (Rect{72, kToolbarY, kBtnSize, kBtnHeight}.contains(x, y)) {
            return {HitArea::Reload, -1};
        }
        if (Rect{104, kToolbarY, kBtnSize, kBtnHeight}.contains(x, y)) {
            return {HitArea::Home, -1};
        }
        if (Rect{kAddrX, kToolbarY, std::max(1, width_ - kAddrX - 8), kAddrH}
                .contains(x, y)) {
            return {HitArea::Address, -1};
        }
        return {};
    }
    if (page_view_.contains(x, y)) return {HitArea::Page, -1};
    return {};
}

void BrowserApp::OnLButtonDown(int x, int y) {
    SetFocus(hwnd_);
    HitTest hit = HitTestPoint(x, y);
    if (hit.area == HitArea::Tab && hit.index >= 0 &&
        hit.index < (int)tabs_.size()) {
        active_ = hit.index;
        address_focused_ = false;
        SyncAddress();
        RelayoutActive();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (hit.area == HitArea::CloseTab && hit.index >= 0) {
        if ((int)tabs_.size() > 1) {
            tabs_.erase(tabs_.begin() + hit.index);
            if (active_ >= (int)tabs_.size()) active_ = (int)tabs_.size() - 1;
            address_focused_ = false;
            SyncAddress();
            RelayoutActive();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (hit.area == HitArea::AddTab) {
        TabState t;
        t.url = "browser://home";
        tabs_.push_back(std::move(t));
        active_ = (int)tabs_.size() - 1;
        NavigateTo("browser://home", true);
        return;
    }
    if (hit.area == HitArea::Back) {
        if (active_ >= 0 && active_ < (int)tabs_.size()) {
            TabState& t = tabs_[active_];
            if (t.history_index > 0) {
                t.history_index--;
                NavigateTo(t.history[t.history_index], false);
            }
        }
        return;
    }
    if (hit.area == HitArea::Forward) {
        if (active_ >= 0 && active_ < (int)tabs_.size()) {
            TabState& t = tabs_[active_];
            if (t.history_index + 1 < (int)t.history.size()) {
                t.history_index++;
                NavigateTo(t.history[t.history_index], false);
            }
        }
        return;
    }
    if (hit.area == HitArea::Reload) {
        NavigateTo(CurrentUrl(), false);
        return;
    }
    if (hit.area == HitArea::Home) {
        NavigateTo("browser://home", true);
        return;
    }
    if (hit.area == HitArea::Address) {
        bool was_focused = address_focused_;
        address_focused_ = true;
        int clicked = (int)AddressCaretFromX(x);
        if (!was_focused) {
            // 点进地址栏＝全选（与系统地址栏一致）。真正的拖动会在
            // OnMouseMove 里用 drag_anchor_ 重新起一段选择。
            AddressSelectAll();
            drag_anchor_ = clicked;
        } else {
            caret_ = clicked;
            AddressClearSelection();
            drag_anchor_ = clicked;
        }
        drag_down_x_ = x;
        mouse_selecting_ = true;
        SetCapture(hwnd_);
        caret_visible_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (hit.area == HitArea::Page) {
        address_focused_ = false;
        // 点页面＝放弃地址栏里没提交的内容，必须同步回真实地址，
        // 否则地址栏会一直显示用户敲了一半却没导航的文本。
        AddressClearSelection();
        SyncAddress();
        // 先把点击交给页面脚本：处理器可能 preventDefault 掉默认动作
        // （比如 <a onclick="return false">），也可能自己发起导航。
        if (active_ >= 0 && active_ < (int)tabs_.size()) {
            TabState& t = tabs_[active_];
            int jx = x - page_view_.x;
            int jy = y - page_view_.y;
            if (t.page.Js()) {
                t.page.DispatchPageClick(jx, jy + t.scroll);
                std::string js_nav = t.page.TakeJsNavigation();
                bool js_reload = t.page.TakeJsReload();
                bool js_dirty = t.page.TakeJsDirty();
                if (js_dirty) RelayoutTab(active_);
                if (!js_nav.empty()) {
                    NavigateTo(js_nav, true);
                    return;
                }
                if (js_reload) {
                    NavigateTo(CurrentUrl(), false);
                    return;
                }
                if (js_dirty) InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        if (active_ >= 0 && active_ < (int)tabs_.size()) {
            TabState& tab = tabs_[active_];
            int px = x - page_view_.x;
            int py = y - page_view_.y + tab.scroll;
            int fpx = x - page_view_.x;
            int fpy = y - page_view_.y;
            if (tab.page.MediaClick(px, py, fpx, fpy)) {
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            std::vector<LinkArea> links;
            tab.page.CollectLinks(links);
            for (const auto& l : links) {
                int lpx = x - page_view_.x;
                int lpy = l.fixed ? y - page_view_.y
                                  : y - page_view_.y + tab.scroll;
                if (l.rect.contains(lpx, lpy)) {
                    std::string target = ResolveUrl(CurrentUrl(), l.href);
                    if (!StartsWith(target, "javascript:") &&
                        !StartsWith(target, "mailto:")) {
                        NavigateTo(target, true);
                    }
                    return;
                }
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void BrowserApp::OnLButtonUp(int x, int y) {
    (void)x;
    (void)y;
    if (mouse_selecting_) {
        mouse_selecting_ = false;
        ReleaseCapture();
    }
}

void BrowserApp::OnLButtonDblClk(int x, int y) {
    if (HitTestPoint(x, y).area != HitArea::Address) return;
    address_focused_ = true;
    AddressSelectAll();
    // 双击之后还常常接着拖：把拖选起点固定到行首。
    drag_anchor_ = 0;
    drag_down_x_ = x;
    mouse_selecting_ = false;
    caret_visible_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnMouseMove(int x, int y, bool dragging) {
    (void)y;
    if (!mouse_selecting_ || !dragging || !address_focused_) return;
    if (drag_anchor_ < 0) return;
    // 单击也会走到这里：只有真的横向移动了才把「点进即全选」换成拖动选择，
    // 否则普通点击会立刻把全选清掉。
    if (x == drag_down_x_) return;
    sel_anchor_ = drag_anchor_;
    caret_ = (int)AddressCaretFromX(x);
    caret_visible_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnRButtonUp(int x, int y) {
    if (HitTestPoint(x, y).area != HitArea::Address) return;
    address_focused_ = true;
    if (caret_ < 0 || (size_t)caret_ > address_text_.size()) {
        caret_ = (int)address_text_.size();
    }
    // 右键落在选择区之外时先清掉选择区，避免「剪切」剪掉看不见的内容。
    if (AddressHasSelection()) {
        size_t at = AddressCaretFromX(x);
        if (at < AddressSelBegin() || at > AddressSelEnd()) {
            AddressClearSelection();
        }
    }
    caret_visible_ = true;
    ShowAddressMenu(x, y);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnMouseWheel(int delta) {
    if (active_ < 0 || active_ >= (int)tabs_.size()) return;
    TabState& tab = tabs_[active_];
    int step = delta / WHEEL_DELTA * 40;
    int max_scroll = std::max(0, tab.page.ContentHeight() - page_view_.h);
    tab.scroll = std::max(0, std::min(tab.scroll - step, max_scroll));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- 地址栏选择区与剪贴板 ------------------------------------------------
// 选择区与插入点都用「字节下标 + 码点边界」表示：sel_anchor_ 是固定端，
// caret_ 是活动端，两者相等即无选择。Shift+方向键、拖动选择、双击全选
// 共用这一套状态，不需要额外的 has_selection 标志去同步。
bool BrowserApp::AddressHasSelection() const {
    if (sel_anchor_ < 0) return false;
    int c = std::max(0, caret_);
    return sel_anchor_ != c;
}

size_t BrowserApp::AddressSelBegin() const {
    if (!AddressHasSelection()) return (size_t)std::max(0, caret_);
    return std::min((size_t)sel_anchor_, (size_t)std::max(0, caret_));
}

size_t BrowserApp::AddressSelEnd() const {
    if (!AddressHasSelection()) return (size_t)std::max(0, caret_);
    return std::max((size_t)sel_anchor_, (size_t)std::max(0, caret_));
}

std::string BrowserApp::AddressSelectedText() const {
    if (!AddressHasSelection()) return "";
    size_t b = AddressSelBegin();
    size_t e = std::min(AddressSelEnd(), address_text_.size());
    if (b >= e) return "";
    return address_text_.substr(b, e - b);
}

void BrowserApp::AddressClearSelection() {
    sel_anchor_ = -1;
}

void BrowserApp::AddressSelectAll() {
    sel_anchor_ = 0;
    caret_ = (int)address_text_.size();
    caret_visible_ = true;
}

void BrowserApp::AddressDeleteSelection() {
    if (!AddressHasSelection()) return;
    size_t b = AddressSelBegin();
    size_t e = std::min(AddressSelEnd(), address_text_.size());
    if (b >= e) {
        AddressClearSelection();
        return;
    }
    address_text_.erase(b, e - b);
    caret_ = (int)b;
    AddressClearSelection();
    caret_visible_ = true;
}

// 在插入点插入文本：先删选择区（输入与粘贴都会替换选中内容），
// 再把插入点吸附到码点边界，最后推进插入点。
void BrowserApp::AddressInsert(const std::string& utf8) {
    if (utf8.empty()) return;
    AddressDeleteSelection();
    if (caret_ < 0) caret_ = 0;
    if (caret_ > (int)address_text_.size()) caret_ = (int)address_text_.size();
    caret_ = (int)Utf8SnapToBoundary(address_text_, (size_t)caret_);
    address_text_.insert((size_t)caret_, utf8);
    caret_ += (int)utf8.size();
    caret_visible_ = true;
}

// 复制：没有选择区时复制整条地址（地址栏最常见的诉求就是「把网址拷走」）。
void BrowserApp::AddressCopyToClipboard() {
    std::string text = AddressHasSelection() ? AddressSelectedText() : address_text_;
    if (text.empty()) return;
    if (!WriteClipboardText(hwnd_, text)) {
        LogStartup("[address] 写剪贴板失败 err=" +
                   std::to_string(GetLastError()));
        return;
    }
    std::printf("[address] copied=%zu bytes\n", text.size());
}

void BrowserApp::AddressCutToClipboard() {
    if (!AddressHasSelection()) {
        AddressSelectAll();
        if (!AddressHasSelection()) return;
    }
    std::string text = AddressSelectedText();
    if (text.empty() || !WriteClipboardText(hwnd_, text)) {
        LogStartup("[address] 剪切写剪贴板失败 err=" +
                   std::to_string(GetLastError()));
        return;
    }
    AddressDeleteSelection();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::AddressPasteFromClipboard() {
    std::string text = ReadClipboardText(hwnd_);
    if (text.empty()) return;
    AddressInsert(text);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// 按 x 像素位置算插入点下标。基准与绘制同源（kAddrX + kAddrPadX），
// 逐码点比较前缀宽度取最近的一个。原来这段逻辑埋在 OnLButtonDown 里，
// 拖动选择要用同一套算法，抽出来共用，避免两处实现漂移。
size_t BrowserApp::AddressCaretFromX(int x) const {
    if (!measure_canvas_) return 0;
    int rel = x - (kAddrX + kAddrPadX);
    if (rel <= 0) return 0;
    size_t best = 0;
    int best_diff = std::abs(rel);
    size_t idx = 0;
    while (idx < address_text_.size()) {
        idx = Utf8NextIndex(address_text_, idx);
        int w = (int)measure_canvas_->MeasureText(address_text_.substr(0, idx),
                                                  kAddrFont, false);
        int diff = std::abs(w - rel);
        if (diff < best_diff) {
            best_diff = diff;
            best = idx;
        }
        if (w > rel) break;
    }
    return best;
}

void BrowserApp::ShowAddressMenu(int x, int y) {
    POINT pt{x, y};
    if (!ClientToScreen(hwnd_, &pt)) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const std::string sel = AddressSelectedText();
    const bool has_text = !sel.empty();
    const bool has_clip = ClipboardHasText();
    AppendMenuW(menu, MF_STRING | (has_text ? 0 : MF_GRAYED), kMenuCut,
                L"剪切(&T)");
    AppendMenuW(menu,
                MF_STRING | ((has_text || !address_text_.empty()) ? 0 : MF_GRAYED),
                kMenuCopy, L"复制(&C)");
    AppendMenuW(menu, MF_STRING | (has_clip ? 0 : MF_GRAYED), kMenuPaste,
                L"粘贴(&P)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (address_text_.empty() ? MF_GRAYED : 0),
                kMenuSelectAll, L"全选(&A)");
    SetMenuDefaultItem(menu, kMenuPaste, FALSE);
    // TPM_RETURNCMD：把命令号当返回值拿，省掉 WM_COMMAND 分发。
    UINT cmd = (UINT)TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0,
        hwnd_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case kMenuCut:
            AddressCutToClipboard();
            break;
        case kMenuCopy:
            AddressCopyToClipboard();
            break;
        case kMenuPaste:
            AddressPasteFromClipboard();
            break;
        case kMenuSelectAll:
            AddressSelectAll();
            break;
        default:
            return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnKey(UINT key) {
    OnKeyEx(key, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
            (GetKeyState(VK_SHIFT) & 0x8000) != 0);
}

void BrowserApp::OnKeyEx(UINT key, bool ctrl, bool shift) {
    // 全局快捷键必须放在最前面：原来地址栏一聚焦，整个函数就走进了
    // 「地址栏分支」并在末尾 return，F5 / Ctrl+R / Ctrl+L 全部失灵。
    if (key == VK_F5 || (ctrl && key == 'R')) {
        // 重载不能让地址栏里正在编辑的内容丢掉：NavigateTo 会主动失焦并把
        // 地址同步回当前 URL，所以这里先存后还原（与系统浏览器一致）。
        bool keep_focus = address_focused_;
        std::string keep_text = address_text_;
        int keep_caret = caret_;
        int keep_anchor = sel_anchor_;
        NavigateTo(CurrentUrl(), false);
        if (keep_focus) {
            address_focused_ = true;
            address_text_ = keep_text;
            caret_ = keep_caret;
            sel_anchor_ = keep_anchor;
            caret_visible_ = true;
        }
        return;
    }
    if ((ctrl && key == 'L') || key == VK_F6) {
        address_focused_ = true;
        AddressSelectAll();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!address_focused_) return;

    size_t caret = (size_t)std::max(0, caret_);
    if (caret > address_text_.size()) caret = address_text_.size();
    caret = Utf8SnapToBoundary(address_text_, caret);

    if (key == VK_RETURN) {
        // 空地址栏按回车：原来会 NavigateTo("")，把空串当地址去解析。
        // 这里按「什么都没输入」处理，只退出编辑状态。
        std::string typed = address_text_;
        if (!typed.empty()) NavigateTo(typed, true);
        address_focused_ = false;
        AddressClearSelection();
        // 这里刻意不调用 SyncAddress：它会用"当前已加载的地址"覆盖地址栏，
        // 而那要等加载完成才更新 —— 用户看到的表现就是"输入后地址栏没变"。
        // NavigateTo 已经把地址栏设成目标地址（pending_url），保持它。
        caret_ = (int)address_text_.size();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_ESCAPE) {
        address_focused_ = false;
        AddressClearSelection();
        SyncAddress();  // 顺带把插入点复位
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // 剪贴板快捷键：除 Ctrl+C/X/V 外，也支持 Windows 传统组合键。
    if ((ctrl && key == 'C') || (ctrl && key == VK_INSERT)) {
        AddressCopyToClipboard();
        return;
    }
    if ((ctrl && key == 'X') || (shift && key == VK_DELETE)) {
        AddressCutToClipboard();
        return;
    }
    if ((ctrl && key == 'V') || (shift && key == VK_INSERT)) {
        AddressPasteFromClipboard();
        return;
    }
    if (ctrl && key == 'A') {
        AddressSelectAll();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_BACK) {
        // 有选择区先删选择区（与所有编辑框一致）；否则按 UTF-8 码点退一格，
        // 只删一个字节会把汉字切成半个（非法 UTF-8）。
        if (AddressHasSelection()) {
            AddressDeleteSelection();
        } else if (caret > 0) {
            size_t prev = Utf8PrevIndex(address_text_, caret);
            address_text_.erase(prev, caret - prev);
            caret_ = (int)prev;
            caret_visible_ = true;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_DELETE) {
        if (AddressHasSelection()) {
            AddressDeleteSelection();
        } else if (caret < address_text_.size()) {
            size_t next = Utf8NextIndex(address_text_, caret);
            address_text_.erase(caret, next - caret);
        }
        // 原来这条分支漏了 caret_visible_：光标会一直停在上次闪烁的隐藏态。
        caret_visible_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
        bool left = (key == VK_LEFT);
        if (!shift && AddressHasSelection()) {
            // 无 Shift 时先塌缩到选择区边缘（与系统编辑框一致）。
            caret_ = (int)(left ? AddressSelBegin() : AddressSelEnd());
            AddressClearSelection();
        } else if (left ? (caret > 0) : (caret < address_text_.size())) {
            if (shift && !AddressHasSelection()) sel_anchor_ = (int)caret;
            caret_ = (int)(left ? Utf8PrevIndex(address_text_, caret)
                                : Utf8NextIndex(address_text_, caret));
        }
        caret_visible_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_HOME || key == VK_END) {
        if (shift && !AddressHasSelection()) sel_anchor_ = (int)caret;
        caret_ = (key == VK_HOME) ? 0 : (int)address_text_.size();
        if (!shift) AddressClearSelection();
        caret_visible_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void BrowserApp::OnChar(wchar_t ch) {
    if (!address_focused_ || ch < 32) return;
    // 走 AddressInsert：有选择区时先替换掉，插入点与边界处理只有一处实现。
    AddressInsert(WideToUtf8(std::wstring(1, ch)));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// JS 定时器心跳：setTimeout / setInterval / requestAnimationFrame 在这里执行。
// 页面没有脚本时只做一次判断就返回，因此 60ms 一跳的开销可以忽略。
void BrowserApp::OnJsTick() {
    if (active_ < 0 || active_ >= (int)tabs_.size()) return;
    TabState& tab = tabs_[active_];
    if (!tab.page.Js()) return;
    bool ran = tab.page.RunJsTimers();
    bool dirty = tab.page.TakeJsDirty();
    if (dirty) RelayoutTab(active_);
    std::string js_nav = tab.page.TakeJsNavigation();
    if (!js_nav.empty()) {
        NavigateTo(js_nav, true);
        return;
    }
    if (tab.page.TakeJsReload()) {
        NavigateTo(CurrentUrl(), false);
        return;
    }
    if (ran || dirty) InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnTimer(UINT_PTR id) {
    if (id == js_timer_) {
        OnJsTick();
        return;
    }
    if (id == video_timer_) {
        if (active_ >= 0 && active_ < (int)tabs_.size()) {
            if (tabs_[active_].page.UpdateMedia()) {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        return;
    }
    caret_visible_ = !caret_visible_;
    if (address_focused_) InvalidateRect(hwnd_, nullptr, FALSE);
}

TabState& BrowserApp::ActiveTab() {
    return tabs_[active_];
}

const TabState& BrowserApp::ActiveTab() const {
    return tabs_[active_];
}

std::string BrowserApp::CurrentUrl() const {
    if (tabs_.empty()) return "";
    return tabs_[active_].url;
}

std::string BrowserApp::FieldText(const Node* n) const {
    if (!n) return "";
    if (n->tag == "textarea") return NodeText(n);
    if (n->tag == "select") {
        for (const auto& c : n->children) {
            if (c->type == NodeType::Element && c->tag == "option") {
                return Trim(NodeText(c.get()));
            }
        }
        return "";
    }
    return n->Attr("value");
}

void BrowserApp::SetFieldText(Node* n, const std::string& v) {
    if (!n) return;
    if (n->tag == "textarea") {
        // textarea 的值是它的文本子节点（布局按文本渲染）
        n->children.clear();
        Node* t = MakeText(v);
        t->parent = n;
        n->children.emplace_back(t);
        return;
    }
    n->attrs["value"] = v;
}

void BrowserApp::FocusField(Node* n, int click_x) {
    TabState& tab = ActiveTab();
    tab.field = n;
    tab.field_focused = (n != nullptr);
    std::string text = FieldText(n);
    // 按点击位置粗定位插入点（精确测量控件内文字要另算，这里按字符比例近似）
    int caret = (int)text.size();
    if (n && click_x >= 0) {
        // 控件左边界用不到像素级信息，这里保守地放到末尾；
        // 需要精细定位时用户可以用 Home/End/左右键。
        caret = (int)text.size();
    }
    tab.field_caret = caret;
    if (n) {
        address_focused_ = false;  // 表单与地址栏互斥
        AddressClearSelection();
    }
}

bool BrowserApp::SubmitFieldForm(Node* field) {
    Node* form = field;
    while (form && !(form->type == NodeType::Element && form->tag == "form")) {
        form = form->parent;
    }
    if (!form) return false;
    std::string method = Lower(Trim(form->Attr("method")));
    if (method.empty()) method = "get";
    std::string action = Trim(form->Attr("action"));
    std::string base = CurrentUrl();
    std::string url = action.empty() ? base : ResolveUrl(base, action);

    std::string body;
    std::vector<const Node*> stack;
    stack.push_back(form);
    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        for (const auto& c : n->children) stack.push_back(c.get());
        if (n->type != NodeType::Element) continue;
        if (n->tag != "input" && n->tag != "textarea" && n->tag != "select") {
            continue;
        }
        std::string name = n->Attr("name");
        if (name.empty()) continue;
        std::string type = Lower(n->Attr("type"));
        // 按钮类控件不参与提交（点击按钮时由按钮值决定，这里从简）
        if (n->tag == "input" &&
            (type == "submit" || type == "button" || type == "image" ||
             type == "reset")) {
            continue;
        }
        if (n->tag == "input" && type == "checkbox" && !n->HasAttr("checked")) {
            continue;
        }
        if (!body.empty()) body += "&";
        body += FormEncode(name) + "=" + FormEncode(FieldText(n));
    }

    LogStartup("[form] submit method=" + method + " fields=" +
               std::to_string(body.empty() ? 0 : 1) + " bytes=" +
               std::to_string(body.size()));
    if (method == "post") {
        post_pending_body_ = body;
        post_pending_type_ = "application/x-www-form-urlencoded";
        NavigateTo(url, true);
    } else {
        if (!body.empty()) {
            url += (url.find('?') == std::string::npos ? "?" : "&") + body;
        }
        NavigateTo(url, true);
    }
    return true;
}

std::string BrowserApp::DisplayUrl() const {
    if (active_ >= 0 && active_ < (int)tabs_.size()) {
        const TabState& tab = tabs_[active_];
        if (!tab.pending_url.empty()) return tab.pending_url;
    }
    return CurrentUrl();
}

void BrowserApp::SyncAddress() {
    if (!address_focused_) {
        address_text_ = DisplayUrl();
        // 失焦后插入点与选择区必须一起复位：否则下次聚焦会带着一个指向
        // 旧文本的选择区（下标可能已经越界），一粘贴就写到错误位置。
        caret_ = (int)address_text_.size();
        AddressClearSelection();
    }
}

void BrowserApp::RelayoutActive() {
    RelayoutTab(active_);
}

void BrowserApp::RelayoutTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    if (page_view_.w <= 0) return;
    TabState& tab = tabs_[index];
    tab.page.Relayout(page_view_.w, page_view_.h, measure_canvas_);
    int max_scroll = std::max(0, tab.page.ContentHeight() - page_view_.h);
    tab.scroll = std::max(0, std::min(tab.scroll, max_scroll));
}

// 第二阶段：图片与外链脚本到手。补画一次并执行脚本。
void BrowserApp::OnAssetsDone(LPARAM l) {
    NavResult* raw = reinterpret_cast<NavResult*>(l);
    if (!raw) return;
    int idx = raw->tab_index;
    if (idx >= 0 && idx < (int)tabs_.size()) {
        TabState& tab = tabs_[idx];
        // 只在这一页仍是当前已加载页面时应用：用户可能已经导航到别处了。
        if (!raw->final_url.empty() && tab.url == raw->final_url) {
            tab.page.SetImages(raw->images);
            tab.page.SetExternalScripts(raw->scripts);
            LogNavTiming(*raw);
            tab.page.RunScripts();
            RelayoutTab(idx);
            tab.title = tab.page.Data().title;
            if (idx == active_) {
                std::wstring title = L"Zero Browser - " + Utf8ToWide(tab.title);
                SetWindowTextW(hwnd_, title.c_str());
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }
    delete raw;
}

void BrowserApp::OnNavigationDone(LPARAM l) {
    NavResult* raw = reinterpret_cast<NavResult*>(l);
    if (!raw) return;
    LogNavTiming(*raw);
    FinishNavigate(raw->tab_index, raw->seq, raw->requested, raw->html,
                   raw->final_url, raw->error, raw->add_history, raw->images,
                   raw->scripts);
    delete raw;
}

void BrowserApp::FinishNavigate(int tab_index, int seq,
                                const std::string& requested,
                                const std::string& html,
                                const std::string& final_url,
                                const std::string& error, bool add_history,
                                const std::map<std::string,
                                               std::shared_ptr<Image>>& images,
                                const std::map<std::string, std::string>& scripts) {
    if (tab_index < 0 || tab_index >= (int)tabs_.size()) return;
    TabState& tab = tabs_[tab_index];
    if (tab.pending_seq != seq) return;
    tab.pending_seq = -1;
    tab.loading = false;
    tab.pending_url.clear();

    std::string resolved = final_url.empty() ? requested : final_url;
    tab.url = resolved;
    tab.page.ParseHtml(html, resolved);
    tab.page.SetImages(images);
    tab.page.SetExternalScripts(scripts);
    RelayoutTab(tab_index);
    // 脚本不在这里执行：外链脚本属于第二阶段资源，等它到手再统一执行
    // （见 OnAssetsDone），否则内联脚本会先跑一遍、外链脚本永远没机会跑。
    tab.scroll = 0;
    tab.title = tab.page.Data().title;
    if (tab.title.empty()) tab.title = resolved;
    if (add_history) {
        if (tab.history_index + 1 < (int)tab.history.size()) {
            tab.history.resize((size_t)tab.history_index + 1);
        }
        if (tab.history.empty() || tab.history.back() != resolved) {
            tab.history.push_back(resolved);
            tab.history_index = (int)tab.history.size() - 1;
        }
    }
    if (tab_index == active_) {
        SyncAddress();
        std::wstring title = L"Zero Browser - " + Utf8ToWide(tab.title);
        SetWindowTextW(hwnd_, title.c_str());
    }
    (void)error;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::StartNavigate(const std::string& url, bool add_history) {
    if (tabs_.empty()) return;
    int tab_index = active_;
    int seq = ++nav_seq_;
    std::string target = NormalizeUrlInput(url);
    TabState& tab = tabs_[tab_index];
    tab.pending_seq = seq;
    tab.loading = true;

    address_focused_ = false;
    address_text_ = target;
    tab.pending_url = target;
    caret_ = (int)address_text_.size();
    InvalidateRect(hwnd_, nullptr, FALSE);

    HWND hwnd = hwnd_;
    std::thread([hwnd, tab_index, seq, target, add_history]() {
        std::string html;
        std::string final_url;
        std::string error;
        ULONGLONG t0 = GetTickCount64();
        LoadUrlSource(target, &html, &final_url, &error);
        ULONGLONG t1 = GetTickCount64();
        std::string res_base = final_url.empty() ? target : final_url;
        double ms_html = (double)(t1 - t0);

        // 阶段一：HTML 到手就交给 UI 解析并绘制。
        // 实测某真实站点首页：HTML 1.4s、图片 6.6s。原来等图片齐了才画，
        // 用户要盯 8 秒白屏；现在页面先出来，图片随后补。
        NavResult* first = new NavResult();
        first->tab_index = tab_index;
        first->seq = seq;
        first->requested = target;
        first->html = html;
        first->final_url = final_url;
        first->error = error;
        first->add_history = add_history;
        first->ms_html = ms_html;
        if (!PostMessage(hwnd, kMsgNavigationDone, 0, (LPARAM)first)) {
            delete first;
        }

        // 阶段二：图片与外链脚本继续在导航线程取，取完补画一次并执行脚本。
        NavResult* second = new NavResult();
        second->assets_phase = true;
        second->tab_index = tab_index;
        second->seq = seq;
        second->requested = target;
        second->final_url = final_url;
        second->html = html;
        second->ms_html = ms_html;
        ULONGLONG t2 = GetTickCount64();
        LoadImagesForHtml(html, res_base, second->images);
        ULONGLONG t3 = GetTickCount64();
        LoadScriptsForHtml(html, res_base, second->scripts);
        ULONGLONG t4 = GetTickCount64();
        second->ms_images = (double)(t3 - t2);
        second->ms_scripts = (double)(t4 - t3);
        second->image_count = (int)second->images.size();
        second->script_count = (int)second->scripts.size();
        if (!PostMessage(hwnd, kMsgAssetsDone, 0, (LPARAM)second)) {
            delete second;
        }
    }).detach();
}

void BrowserApp::NavigateTo(const std::string& url, bool add_history) {
    StartNavigate(url, add_history);
}

}  // namespace zb