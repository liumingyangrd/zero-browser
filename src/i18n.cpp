#include "i18n.h"

#include "common.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace zb {

namespace {

Settings g_settings;
// 系统 UI 语言是不是中文。Auto 时按它落定。
bool g_system_is_chinese = false;
bool g_system_probed = false;

bool ProbeSystemChinese() {
    LANGID id = GetUserDefaultUILanguage();
    // 主语言 ID（低 10 位）为 0x04 即中文（简繁都算）。
    return (id & 0x3FF) == 0x04;
}

// 界面字符串表。加新字符串时**三列都要写**，漏写的那一列会退回 key 名。
struct Entry {
    const char* key;
    const char* zh;
    const char* en;
};

const Entry kStrings[] = {
    // 外壳
    {"page.untitled", "页面", "Untitled"},
    {"status.loading", "加载中... ", "Loading... "},
    // 地址栏右键菜单（& 后面是快捷键字母）
    {"menu.cut", "剪切(&T)", "Cu&t"},
    {"menu.copy", "复制(&C)", "&Copy"},
    {"menu.paste", "粘贴(&P)", "&Paste"},
    {"menu.selectAll", "全选(&A)", "Select &All"},
    // 错误页
    {"error.heading", "页面加载失败", "Page failed to load"},
    {"error.unknown", "无法加载页面", "Unable to load the page"},
    {"error.addressLabel", "地址: ", "Address: "},
    {"error.fileMissing", "文件不存在或为空: ", "File missing or empty: "},
    {"error.dataNoComma", "data URL 缺少逗号", "data URL is missing a comma"},
    {"error.httpStatus", "服务器返回 HTTP ", "Server returned HTTP "},
    {"error.badScheme",
     "无法解析这个地址，支持 browser://、file://、data:text/html、http://、https://",
     "Cannot parse this address. Supported: browser://, file://, data:text/html, "
     "http://, https://"},
    {"error.backHint", "返回上一页", "Go back"},
    // 设置页
    {"settings.title", "设置", "Settings"},
    {"settings.heading", "设置", "Settings"},
    {"settings.lead", "这些设置保存在本机，重启后依然有效。",
     "These settings are stored on this machine and survive a restart."},
    {"settings.language", "界面语言", "Interface language"},
    {"settings.langAuto", "跟随系统", "Follow system"},
    {"settings.langZh", "中文", "中文"},
    {"settings.langEn", "English", "English"},
    {"settings.current", "当前", "current"},
    {"settings.storageHint", "配置文件：", "Config file: "},
    {"settings.effective", "实际生效：", "In effect: "},
    {"settings.backHome", "返回主页", "Back to home"},
    // ---- 内置页（模板里的 {{key}} 就是这些）----
    {"home.pagetitle", "Zero Browser 主页", "Zero Browser home"},
    {"home.tag", "自建渲染内核 · 演示首页",
     "Self-built rendering engine · demo home"},
    {"home.h1", "一个不用 Chromium 的浏览器",
     "A browser that does not use Chromium"},
    {"home.sub",
     "HTML 解析、CSS 解析、盒模型布局、绘制、JavaScript 解释器全部由本项目自己实现，"
     "不依赖任何现成浏览器内核。系统只负责传输、解码与出像素。这是当前网页引擎的渲染结果。",
     "HTML parsing, CSS parsing, box layout, painting and the JavaScript interpreter are "
     "all implemented by this project — no existing browser engine is involved. The system "
     "only moves bytes, decodes them and puts pixels on screen. What you are reading is "
     "this engine's own output."},
    {"home.card1.title", "解析器", "HTML parser"},
    {"home.card1.body", "自带 HTML 词法/语法解析与实体解码，生成 DOM 树。",
     "Its own HTML tokenizer and tree builder, plus entity decoding, producing the DOM."},
    {"home.card1.link", "查看解析器详情", "Parser details"},
    {"home.card2.title", "CSS 引擎", "CSS engine"},
    {"home.card2.body",
     "选择器（类/ID/后代/属性/结构伪类）、盒模型、flex 与 grid 布局、position 定位与 z-index。",
     "Selectors (class / id / descendant / attribute / structural pseudo-classes), the box "
     "model, flex and grid layout, positioning and z-index."},
    {"home.card2.link", "查看 CSS 能力", "CSS capabilities"},
    {"home.card3.title", "脚本引擎", "Script engine"},
    {"home.card3.body",
     "自研 ES5 子集解释器：DOM 操作、事件冒泡、定时器。死循环与无限递归有护栏，"
     "脚本报错不影响页面渲染。",
     "A self-built ES5 subset interpreter: DOM access, event bubbling, timers. Runaway "
     "loops and recursion are contained, and a script error never breaks the page."},
    {"home.card4.title", "媒体与绘制", "Media and painting"},
    {"home.card4.body",
     "图片/背景图经 WIC 解码、视频帧经 Media Foundation 解码，缩放、裁剪、合成与滚动全部自研。",
     "WIC decodes images and background images, Media Foundation decodes video frames; "
     "scaling, clipping, compositing and scrolling are all ours."},
    {"home.foot",
     "Zero Browser 0.1.5 · 自研渲染内核 · 页面由 zero-browser 渲染",
     "Zero Browser 0.1.5 · self-built rendering engine · rendered by zero-browser"},

    {"parser.pagetitle", "HTML 解析器", "HTML parser"},
    {"parser.barTitle", "<html> 解析器", "<html> parser"},
    {"parser.barSub", "由本项目实现的 HTML 语法分析",
     "HTML parsing implemented by this project"},
    {"parser.h1", "HTML 解析管线", "The HTML parsing pipeline"},
    {"parser.body",
     "源代码被逐字符扫描：标签开始/结束、属性、注释、DOCTYPE、字符实体都会处理，"
     "输出一棵 DOM 树。",
     "The source is scanned character by character: tag open/close, attributes, comments, "
     "DOCTYPE and character entities are all handled, and a DOM tree comes out."},
    {"parser.note", "本页由 Zero Browser 的解析器生成并排版。",
     "This page was produced and laid out by Zero Browser's own parser."},

    {"css.pagetitle", "CSS 引擎", "CSS engine"},
    {"css.barTitle", "CSS 引擎", "CSS engine"},
    {"css.barSub", "由本项目实现的选择器与盒模型",
     "Selectors and the box model, implemented by this project"},
    {"css.h1", "CSS 排版", "CSS layout"},
    {"css.body",
     "样式表被解析为规则，选择器（标签、类、ID、后代）匹配元素，属性应用进盒模型。",
     "Stylesheets are parsed into rules, selectors (tag, class, id, descendant) match "
     "elements, and declarations are applied to the box model."},
    {"css.box1", "盒模型 + flex 布局", "Box model + flex"},
    {"css.box3", "文本排版", "Text layout"},

    {"about.pagetitle", "系统信息", "System information"},
    {"about.barTitle", "about:system", "about:system"},
    {"about.barSub", "自研内核信息", "self-built engine information"},
    {"about.h1", "Zero Browser 系统信息", "Zero Browser system information"},
    {"about.rowHtml", "HTML 解析器", "HTML parser"},
    {"about.valHtml", "内置（tokenizer + tree builder）",
     "built in (tokenizer + tree builder)"},
    {"about.rowCss", "CSS 引擎", "CSS engine"},
    {"about.valCss", "内置（selector + box model）", "built in (selector + box model)"},
    {"about.rowLayout", "布局引擎", "Layout engine"},
    {"about.valLayout", "内置（block / inline / flex / grid / position）",
     "built in (block / inline / flex / grid / position)"},
    {"about.rowTransport", "传输与解码", "Transport and decoding"},
    {"about.valTransport",
     "WinHTTP / Media Foundation / WIC / WASAPI（仅底层管道）",
     "WinHTTP / Media Foundation / WIC / WASAPI (plumbing only)"},
    {"about.rowRender", "渲染", "Painting"},
    {"about.valRender", "GDI 像素输出，无 WebView / Chromium",
     "GDI pixel output, no WebView and no Chromium"},
    {"about.rowVersion", "版本", "Version"},
};

const Entry* FindEntry(const char* key) {
    for (const Entry& e : kStrings) {
        if (std::strcmp(e.key, key) == 0) return &e;
    }
    return nullptr;
}

std::string TrimCopy(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

}  // namespace

Settings& MutableSettings() { return g_settings; }

const Settings& CurrentSettings() { return g_settings; }

bool UiIsEnglish() {
    if (!g_system_probed) {
        g_system_is_chinese = ProbeSystemChinese();
        g_system_probed = true;
    }
    if (g_settings.lang == Lang::Zh) return false;
    if (g_settings.lang == Lang::En) return true;
    return !g_system_is_chinese;
}

const char* UiLangCode() { return UiIsEnglish() ? "en" : "zh"; }

const char* UiLangSetting() { return LangToCode(g_settings.lang); }

const char* T(const char* key) {
    if (!key) return "";
    const Entry* e = FindEntry(key);
    if (!e) return key;  // 见头文件：漏翻时露出键名，不要静默留白
    return UiIsEnglish() ? e->en : e->zh;
}

Lang ParseLang(const std::string& s, Lang fallback) {
    std::string v = Lower(TrimCopy(s));
    if (v == "auto" || v == "system") return Lang::Auto;
    if (v == "zh" || v == "zh-cn" || v == "cn" || v == "chinese") return Lang::Zh;
    if (v == "en" || v == "en-us" || v == "english") return Lang::En;
    return fallback;
}

const char* LangToCode(Lang lang) {
    switch (lang) {
        case Lang::Zh: return "zh";
        case Lang::En: return "en";
        default: return "auto";
    }
}

std::string SettingsFilePath() {
    // 测试可以先设 ZB_SETTINGS 指向临时文件，避免污染真实配置。
    char buf[MAX_PATH * 2]{};
    DWORD n = GetEnvironmentVariableA("ZB_SETTINGS", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return std::string(buf);

    std::string base;
    char appdata[MAX_PATH * 2]{};
    DWORD m = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (m > 0 && m < sizeof(appdata)) {
        base = std::string(appdata);
    } else {
        base = ".";
    }
    std::string dir = base + "\\ZeroBrowser";
    CreateDirectoryA(dir.c_str(), nullptr);  // 已存在时返回失败，无所谓
    return dir + "\\settings.ini";
}

void LoadSettings() {
    std::string path = SettingsFilePath();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;  // 首次运行没有文件，用默认值
    std::string text;
    char chunk[512];
    size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
        text.append(chunk, got);
    }
    std::fclose(f);

    // 极简 ini：只看 `key=value` 形式的行，段名忽略（目前只有 [ui] 一段）。
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = TrimCopy(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Lower(TrimCopy(line.substr(0, eq)));
        std::string val = TrimCopy(line.substr(eq + 1));
        if (key == "lang") {
            g_settings.lang = ParseLang(val, g_settings.lang);
        }
    }
}

bool SaveSettings() {
    std::string path = SettingsFilePath();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# Zero Browser settings. Written by the browser itself.\n");
    std::fprintf(f, "[ui]\n");
    std::fprintf(f, "lang=%s\n", LangToCode(g_settings.lang));
    bool ok = std::fclose(f) == 0;
    return ok;
}

}  // namespace zb
