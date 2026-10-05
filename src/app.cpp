#include "app.h"

#include "network.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <vector>

namespace zb {

namespace {

const wchar_t* kClassW = L"ZeroBrowserSelfBuilt";

const UINT kMsgNavigationDone = WM_APP + 1;

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
};

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

// 在导航线程里扫描 <img>，抓取并解码图片字节。只做传输/解码：
// HTML 结构、布局、绘制仍由浏览器自己负责。
void LoadImagesForHtml(const std::string& html, const std::string& base_url,
                       std::map<std::string, std::shared_ptr<Image>>& out) {
    std::string lower = ToLowerAscii(html);
    size_t pos = 0;
    while (true) {
        size_t img = lower.find("<img", pos);
        if (img == std::string::npos) break;
        size_t end = lower.find('>', img);
        if (end == std::string::npos) break;
        std::string tag = html.substr(img, end - img + 1);
        std::string src = ExtractAttr(tag, "src");
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
        } else if (StartsWith(abs, "http://") || StartsWith(abs, "https://")) {
            FetchResult res;
            FetchBinaryWithCookies(abs, &res, 15000);
            if (!res.html.empty()) image = DecodeImage(res.html);
        }

        if (image) {
            image->source_url = abs;
            out[abs] = std::move(image);
        }
    }

    // CSS 里的 background-image: url(...)（外部样式表已内联进 <style>）。
    // 相对路径以页面地址为基准（外部 CSS 文件本身的相对基准暂不支持）。
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
        if (!StartsWith(abs, "http://") && !StartsWith(abs, "https://") &&
            !StartsWith(abs, "file://")) {
            continue;
        }
        std::shared_ptr<Image> image;
        if (StartsWith(abs, "file://")) {
            std::string path = abs.substr(7);
            if (path.size() > 2 && (path[0] == '/' || path[0] == '\\') &&
                path[2] == ':') {
                path = path.substr(1);
            }
            path = PercentDecode(path);
            std::string bytes = ReadFileBinary(path);
            if (!bytes.empty()) image = DecodeImage(bytes);
        } else {
            FetchResult res;
            FetchBinaryWithCookies(abs, &res, 15000);
            if (!res.html.empty()) image = DecodeImage(res.html);
        }
        if (image) {
            image->source_url = abs;
            out[abs] = std::move(image);
        }
    }
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
        FetchUrlWithCookies(css_url, &css, 10000);
        if (!css.html.empty()) {
            injected += "\n<style>\n";
            injected += css.html;
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
    wc.style = CS_HREDRAW | CS_VREDRAW;
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
    std::printf("%*sbox tag=%s doc=%d,%d,%d,%d screen=%d,%d%s\n", depth * 2, "",
                box->node ? box->node->tag.c_str() : "(anon)", box->rect.x,
                box->rect.y, box->rect.w, box->rect.h, screen_x, screen_y,
                is_fixed ? " fixed" : "");
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

    if (opt.dump_boxes && active_ >= 0 && active_ < (int)tabs_.size()) {
        const TabState& tab = tabs_[active_];
        std::printf("== 布局树 ==\n");
        DumpBoxTree(tab.page.RootBox(), page_view_, tab.scroll, 0);
    }

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
        case WM_MOUSEWHEEL:
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
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
    int tab_w = std::max(70, std::min(190, (width_ - 70) / std::max(1, n)));
    int x = 8;
    for (int i = 0; i < n; ++i) {
        bool active = i == active_;
        Rect r{x, 7, tab_w, 28};
        canvas.FillRoundRect(r.x, r.y, r.w, r.h, 7,
                             active ? 0xffffff : 0x1e293b);
        const TabState& tab = tabs_[i];
        std::string label = tab.title.empty() ? "页面" : tab.title;
        canvas.DrawText(label, r.x + 9, r.y + 7, 13,
                        active ? 0x0f172a : 0x94a3b8, active, false, false);
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

    // Reload.
    Rect reload{72, 44, 30, 28};
    canvas.FillRoundRect(reload.x, reload.y, reload.w, reload.h, 6, 0xeef2f7);
    canvas.StrokeArc({reload.x + 6, reload.y + 7, reload.x + 22, reload.y + 23},
                     60, 270, 0x334155);
    canvas.StrokeLine(reload.x + 17, reload.y + 14, reload.x + 22, reload.y + 10, 0x334155, 2);
    canvas.StrokeLine(reload.x + 22, reload.y + 10, reload.x + 25, reload.y + 12, 0x334155, 2);

    // Home.
    Rect home{104, 44, 30, 28};
    canvas.FillRoundRect(home.x, home.y, home.w, home.h, 6, 0xeef2f7);
    canvas.StrokeLine(home.x + 6, home.y + 15, home.x + 15, home.y + 7, 0x334155, 2);
    canvas.StrokeLine(home.x + 15, home.y + 7, home.x + 24, home.y + 15, 0x334155, 2);
    canvas.StrokeLine(home.x + 8, home.y + 13, home.x + 8, home.y + 22, 0x334155, 2);
    canvas.StrokeLine(home.x + 22, home.y + 13, home.x + 22, home.y + 22, 0x334155, 2);
    canvas.StrokeLine(home.x + 8, home.y + 22, home.x + 22, home.y + 22, 0x334155, 2);

    // Address bar.
    Rect addr{140, 44, std::max(1, width_ - 148), 30};
    uint32_t border = address_focused_ ? 0x2563eb : 0xcbd5e1;
    canvas.FillRoundRect(addr.x, addr.y, addr.w, addr.h, 7, 0xffffff);
    canvas.StrokeRect(addr.x, addr.y, addr.w, addr.h, border);
    std::string shown = address_text_;
    canvas.DrawText(shown, addr.x + 12, addr.y + 8, 15,
                    address_focused_ ? 0x0f172a : 0x334155, false, false, false);
    if (address_focused_ && caret_visible_) {
        size_t prefix_len = shown.substr(0, std::min(caret_, (int)shown.size())).size();
        std::string prefix = shown.substr(0, prefix_len);
        int cx = addr.x + 12 + (int)canvas.MeasureText(prefix, 15, false);
        canvas.StrokeLine(cx, addr.y + 7, cx, addr.y + 23, 0x2563eb, 2);
    }
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
        text = "加载中... " + CurrentUrl();
    } else {
        text = CurrentUrl();
    }
    if (text.empty()) text = "Zero Browser";
    canvas.DrawText(text, status_rect_.x + 10, status_rect_.y + 5, 12,
                    0x94a3b8, false, false, false);
}

HitTest BrowserApp::HitTestPoint(int x, int y) const {
    if (y < 38) {
        int n = (int)tabs_.size();
        int tab_w = std::max(70, std::min(190, (width_ - 70) / std::max(1, n)));
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
    if (y < 78) {
        if (Rect{8, 44, 30, 28}.contains(x, y)) return {HitArea::Back, -1};
        if (Rect{40, 44, 30, 28}.contains(x, y)) return {HitArea::Forward, -1};
        if (Rect{72, 44, 30, 28}.contains(x, y)) return {HitArea::Reload, -1};
        if (Rect{104, 44, 30, 28}.contains(x, y)) return {HitArea::Home, -1};
        if (Rect{140, 44, std::max(1, width_ - 148), 30}.contains(x, y)) {
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
        address_focused_ = true;
        caret_ = (int)address_text_.size();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (hit.area == HitArea::Page) {
        address_focused_ = false;
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

void BrowserApp::OnMouseWheel(int delta) {
    if (active_ < 0 || active_ >= (int)tabs_.size()) return;
    TabState& tab = tabs_[active_];
    int step = delta / WHEEL_DELTA * 40;
    int max_scroll = std::max(0, tab.page.ContentHeight() - page_view_.h);
    tab.scroll = std::max(0, std::min(tab.scroll - step, max_scroll));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnKey(UINT key) {
    if (address_focused_) {
        if (key == VK_RETURN) {
            NavigateTo(address_text_, true);
            address_focused_ = false;
            SyncAddress();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (key == VK_ESCAPE) {
            address_focused_ = false;
            SyncAddress();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (key == VK_BACK) {
            if (caret_ > 0 && caret_ <= (int)address_text_.size()) {
                address_text_.erase(address_text_.begin() + caret_ - 1);
                caret_--;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }
        if (key == VK_DELETE) {
            if (caret_ < (int)address_text_.size()) {
                address_text_.erase(address_text_.begin() + caret_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }
        if (key == VK_LEFT) {
            if (caret_ > 0) {
                caret_--;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }
        if (key == VK_RIGHT) {
            if (caret_ < (int)address_text_.size()) {
                caret_++;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }
        return;
    }
    if (key == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        address_focused_ = true;
        caret_ = (int)address_text_.size();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_F5) {
        NavigateTo(CurrentUrl(), false);
    }
}

void BrowserApp::OnChar(wchar_t ch) {
    if (!address_focused_ || ch < 32) return;
    std::wstring ws(1, ch);
    std::string s = WideToUtf8(ws);
    if (caret_ < 0) caret_ = 0;
    if (caret_ > (int)address_text_.size()) caret_ = (int)address_text_.size();
    address_text_.insert((size_t)caret_, s);
    caret_ += (int)s.size();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserApp::OnTimer(UINT_PTR id) {
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

void BrowserApp::SyncAddress() {
    if (!address_focused_) address_text_ = CurrentUrl();
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

void BrowserApp::OnNavigationDone(LPARAM l) {
    NavResult* raw = reinterpret_cast<NavResult*>(l);
    if (!raw) return;
    FinishNavigate(raw->tab_index, raw->seq, raw->requested, raw->html,
                   raw->final_url, raw->error, raw->add_history, raw->images);
    delete raw;
}

void BrowserApp::FinishNavigate(int tab_index, int seq,
                                const std::string& requested,
                                const std::string& html,
                                const std::string& final_url,
                                const std::string& error, bool add_history,
                                const std::map<std::string,
                                               std::shared_ptr<Image>>& images) {
    if (tab_index < 0 || tab_index >= (int)tabs_.size()) return;
    TabState& tab = tabs_[tab_index];
    if (tab.pending_seq != seq) return;
    tab.pending_seq = -1;
    tab.loading = false;

    std::string resolved = final_url.empty() ? requested : final_url;
    tab.url = resolved;
    tab.page.ParseHtml(html, resolved);
    tab.page.SetImages(images);
    RelayoutTab(tab_index);
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
    caret_ = (int)address_text_.size();
    InvalidateRect(hwnd_, nullptr, FALSE);

    HWND hwnd = hwnd_;
    std::thread([hwnd, tab_index, seq, target, add_history]() {
        std::string html;
        std::string final_url;
        std::string error;
        LoadUrlSource(target, &html, &final_url, &error);
        NavResult* res = new NavResult();
        res->tab_index = tab_index;
        res->seq = seq;
        res->requested = target;
        res->html = html;
        res->final_url = final_url;
        res->error = error;
        res->add_history = add_history;
        LoadImagesForHtml(html, final_url.empty() ? target : final_url,
                          res->images);
        if (!PostMessage(hwnd, kMsgNavigationDone, 0, (LPARAM)res)) {
            delete res;
        }
    }).detach();
}

void BrowserApp::NavigateTo(const std::string& url, bool add_history) {
    StartNavigate(url, add_history);
}

}  // namespace zb