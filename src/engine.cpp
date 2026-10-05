#include "engine.h"
#include "html.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sstream>

namespace zb {

namespace {

const char* kHomeHtml = R"HTML(<!DOCTYPE html>
<html><head><style>
  * { box-sizing: border-box; }
  body { margin: 0; background: #0f172a; color: #e2e8f0; font-family: "Segoe UI"; }
  .top { background:#0b1220; border-bottom:1px solid #1e293b; padding: 18px 32px; }
  .brand { font-size: 28px; font-weight:bold; color:#38bdf8; }
  .tag { color:#94a3b8; font-size:14px; margin-top:6px; }
  .hero { padding: 40px 32px 24px 32px; }
  h1 { font-size: 34px; margin: 0 0 10px 0; color:#f8fafc; }
  .sub { font-size:17px; color:#cbd5e1; line-height:1.7; }
  .cards { display:flex; gap:16px; padding: 0 32px 40px 32px; }
  .card { background:#111a2e; border:1px solid #1e293b; border-radius:10px; padding:18px; flex:1; }
  .card h3 { margin:0 0 8px 0; color:#7dd3fc; font-size:18px; }
  .card p { margin:0; color:#94a3b8; font-size:14px; line-height:1.6; }
  .card a { color:#38bdf8; }
  .foot { padding:16px 32px; border-top:1px solid #1e293b; color:#64748b; font-size:12px; }
</style></head>
<body>
  <div class="top"><div class="brand">ZERO Browser</div><div class="tag">自建渲染内核 · 演示首页</div></div>
  <div class="hero">
    <h1>一个不用 Chromium 的浏览器</h1>
    <div class="sub">HTML 解析、CSS 解析、盒模型布局、绘制全部由本项目自己实现，不依赖任何现成浏览器内核。系统只负责传输、解码与出像素。这是当前网页引擎的渲染结果。</div>
  </div>
  <div class="cards">
    <div class="card"><h3>解析器</h3><p>自带 HTML 词法/语法解析与实体解码，生成 DOM 树。</p><p><a href="about:parser">查看解析器详情</a></p></div>
    <div class="card"><h3>CSS 引擎</h3><p>选择器（类/ID/后代/属性/结构伪类）、盒模型、flex 与 grid 布局、position 定位与 z-index。</p><p><a href="about:css">查看 CSS 能力</a></p></div>
    <div class="card"><h3>媒体与绘制</h3><p>图片/背景图经 WIC 解码、视频帧经 Media Foundation 解码，缩放、裁剪、合成与滚动全部自研。</p></div>
  </div>
  <div class="foot">Zero Browser 0.1 · 自研渲染内核 · 页面由 zero-browser 渲染</div>
</body></html>
)HTML";

const char* kParserHtml = R"HTML(<!DOCTYPE html>
<html><head><style>
  * { box-sizing:border-box; }
  body { margin:0; background:#f8fafc; color:#0f172a; }
  .bar { background:#0f172a; padding:14px 24px; color:#e2e8f0; }
  .bar b { color:#38bdf8; font-size:20px; }
  .body { padding:24px; max-width:720px; }
  h1 { font-size:24px; }
  code { background:#e2e8f0; padding:2px 6px; border-radius:4px; font-size:14px; }
  pre { background:#0f172a; color:#7dd3fc; padding:14px; border-radius:8px; font-size:13px; line-height:1.6; overflow:auto; }
  .note { background:#dbeafe; border:1px solid #93c5fd; border-radius:8px; padding:12px 16px; margin-top:20px; }
</style></head>
<body>
  <div class="bar"><b>&lt;html&gt; 解析器</b> &nbsp; 由本项目实现的 HTML 语法分析</div>
  <div class="body">
    <h1>HTML 解析管线</h1>
    <p>源代码被逐字符扫描：标签开始/结束、属性、注释、DOCTYPE、字符实体都会处理，输出一棵 DOM 树。</p>
    <pre>source --[ tokenizer ]--&gt; tokens --[ tree builder ]--&gt; DOM</pre>
    <div class="note">本页由 Zero Browser 的解析器生成并排版。</div>
  </div>
</body></html>
)HTML";

const char* kCssHtml = R"HTML(<!DOCTYPE html>
<html><head><style>
  * { box-sizing:border-box; }
  body { margin:0; background:#f8fafc; color:#0f172a; }
  .bar { background:#0f172a; padding:14px 24px; color:#e2e8f0; }
  .bar b { color:#fbbf24; font-size:20px; }
  .body { padding:24px; max-width:720px; }
  h1 { font-size:24px; }
  .row { display:flex; gap:12px; }
  .box { flex:1; border-radius:8px; padding:12px; color:#fff; font-size:14px; }
  .b1 { background:#2563eb; }
  .b2 { background:#059669; }
  .b3 { background:#d97706; }
  pre { background:#0f172a; color:#fbbf24; padding:14px; border-radius:8px; font-size:13px; }
</style></head>
<body>
  <div class="bar"><b>CSS 引擎</b> &nbsp; 由本项目实现的选择器与盒模型</div>
  <div class="body">
    <h1>CSS 排版</h1>
    <p>样式表被解析为规则，选择器（标签、类、ID、后代）匹配元素，属性应用进盒模型。</p>
    <div class="row">
      <div class="box b1">盒模型 + flex 布局</div>
      <div class="box b2">border-radius</div>
      <div class="box b3">文本排版</div>
    </div>
    <pre>.card { background:#2563eb; border-radius:8px; padding:12px; }</pre>
  </div>
</body></html>
)HTML";

const char* kAboutHtml = R"HTML(<!DOCTYPE html>
<html><head><style>
  * { box-sizing:border-box; }
  body { margin:0; background:#f8fafc; color:#0f172a; font-size:15px; line-height:1.7; }
  .bar { background:#0f172a; padding:14px 24px; color:#e2e8f0; }
  .bar b { color:#a78bfa; font-size:20px; }
  .body { padding:24px; max-width:720px; }
  h1 { font-size:24px; margin-top:0; }
  table { border-collapse:collapse; width:100%; font-size:13px; }
  td { border:1px solid #cbd5e1; padding:8px 12px; }
  td:first-child { width:180px; }
</style></head>
<body>
  <div class="bar"><b>about:system</b> &nbsp; 自研内核信息</div>
  <div class="body">
    <h1>Zero Browser 系统信息</h1>
    <table>
      <tr><td>HTML 解析器</td><td>内置（tokenizer + tree builder）</td></tr>
      <tr><td>CSS 引擎</td><td>内置（selector + box model）</td></tr>
      <tr><td>布局引擎</td><td>内置（block / inline / flex / grid / position）</td></tr>
      <tr><td>传输与解码</td><td>WinHTTP / Media Foundation / WIC / WASAPI（仅底层管道）</td></tr>
      <tr><td>渲染</td><td>GDI 像素输出，无 WebView / Chromium</td></tr>
      <tr><td>版本</td><td>0.1</td></tr>
    </table>
  </div>
</body></html>
)HTML";

const char* kDefaultCss = R"CSS(
  * { box-sizing:border-box; }
  body { margin:0; background:#ffffff; color:#111827; font-size:16px; line-height:1.5; }
  p { margin:16px 0; }
  h1 { font-size:32px; font-weight:bold; margin:20px 0 12px 0; }
  h2 { font-size:24px; font-weight:bold; margin:18px 0 10px 0; }
  h3 { font-size:19px; font-weight:bold; margin:16px 0 8px 0; }
  h4,h5,h6 { font-weight:bold; margin:14px 0 8px 0; }
  a { color:#2563eb; text-decoration:underline; }
  span,a,b,strong,i,em,small,code,label,abbr,cite,q,sub,sup,time,mark,font,u,s,strike,big,tt { display:inline; }
  br { display:inline; }
  ul,ol { margin:16px 0; padding-left:28px; }
  li { display:block; margin:4px 0; }
  dl { margin:16px 0; }
  dt { font-weight:bold; }
  dd { margin:6px 0 6px 28px; }
  table { display:block; width:100%; border-collapse:collapse; margin:12px 0; }
  tr { display:flex; }
  td,th { flex:1; padding:6px 10px; border:1px solid #cbd5e1; }
  th { font-weight:bold; background:#f1f5f9; }
  pre,code { font-family:monospace; font-size:13px; }
  pre { display:block; white-space:pre; background:#0f172a; color:#94a3b8; padding:12px; border-radius:8px; margin:12px 0; overflow:auto; }
  code { background:#f1f5f9; padding:1px 4px; border-radius:3px; }
  blockquote { margin:16px 24px; padding:8px 14px; border:1px solid #e2e8f0; color:#475569; }
  hr { display:block; height:1px; background:#e2e8f0; border:0; margin:18px 0; }
  center { display:block; text-align:center; }
  img { display:inline-block; }
  video { display:block; background:#000000; }
  input,button,select,textarea { display:inline-block; color:#0f172a; border:1px solid #94a3b8; background:#ffffff; }
  input[type="submit"],input[type="button"],input[type="reset"],button { background:#eef2ff; color:#1e3a8a; cursor:pointer; }
  style,script,head,title,meta,link,base,iframe,object,embed,canvas,svg,path,g,defs,symbol,use,circle,rect,line,polygon,polyline { display:none; }
)CSS";

std::string RemoveComments(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

SelectorPart ParseSelectorPart(std::string raw) {
    SelectorPart p;
    raw = Trim(raw);
    size_t i = 0;
    std::string tag;
    while (i < raw.size() && raw[i] != '.' && raw[i] != '#' &&
           raw[i] != ':' && raw[i] != '[') {
        tag.push_back(raw[i++]);
    }
    if (tag != "*") p.tag = Lower(tag);
    while (i < raw.size()) {
        if (raw[i] == '.') {
            size_t end = i + 1;
            while (end < raw.size() && raw[end] != '.' && raw[end] != '#' &&
                   raw[end] != ':' && raw[end] != '[') end++;
            p.classes.push_back(raw.substr(i + 1, end - i - 1));
            i = end;
        } else if (raw[i] == '#') {
            size_t end = i + 1;
            while (end < raw.size() && raw[end] != '.' && raw[end] != '#' &&
                   raw[end] != ':' && raw[end] != '[') end++;
            p.id = raw.substr(i + 1, end - i - 1);
            i = end;
        } else if (raw[i] == '[') {
            size_t end = raw.find(']', i);
            size_t stop = end == std::string::npos ? raw.size() : end;
            std::string inner = raw.substr(i + 1, stop - i - 1);
            i = end == std::string::npos ? raw.size() : end + 1;
            AttrTest t;
            size_t eq = inner.find_first_of("=~^$*|");
            if (eq == std::string::npos) {
                t.name = Lower(Trim(inner));
            } else {
                t.name = Lower(Trim(inner.substr(0, eq)));
                size_t v = eq;
                while (v < inner.size() &&
                       (inner[v] == '=' || inner[v] == '~' || inner[v] == '^' ||
                        inner[v] == '$' || inner[v] == '*' || inner[v] == '|')) {
                    v++;
                }
                t.op = inner.substr(eq, v - eq);
                std::string val = Trim(inner.substr(v));
                if (val.size() >= 2 &&
                    ((val.front() == '"' && val.back() == '"') ||
                     (val.front() == '\'' && val.back() == '\''))) {
                    val = val.substr(1, val.size() - 2);
                }
                t.value = val;
            }
            if (!t.name.empty()) p.attrs.push_back(t);
        } else if (raw[i] == ':') {
            size_t end = i + 1;
            while (end < raw.size()) {
                char c = raw[end];
                if (c == '(') {
                    size_t rp = raw.find(')', end);
                    end = rp == std::string::npos ? raw.size() : rp + 1;
                    continue;
                }
                if (c == '.' || c == '#' || c == ':' || c == '[') break;
                end++;
            }
            p.pseudos.push_back(raw.substr(i + 1, end - i - 1));
            i = end;
        } else {
            i++;
        }
    }
    return p;
}

std::vector<SelectorPart> ParseSelectorChain(const std::string& raw) {
    std::vector<SelectorPart> chain;
    std::string cur;
    for (size_t i = 0; i <= raw.size(); ++i) {
        char c = i < raw.size() ? raw[i] : ' ';
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>') {
            if (!cur.empty()) {
                chain.push_back(ParseSelectorPart(cur));
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (chain.empty()) chain.push_back(SelectorPart{});
    return chain;
}

// 找到与 open 处的 '{' 匹配的 '}'，跳过字符串与注释里的花括号。
size_t FindMatchingBrace(const std::string& css, size_t open) {
    int depth = 0;
    char quote = 0;
    for (size_t i = open; i < css.size(); ++i) {
        char c = css[i];
        if (quote) {
            if (c == '\\') { i++; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

// 按分隔符拆分，但跳过 () [] 内部以及引号内部的分隔符。
// 必须这样做：`background: url(data:image/png;base64,AAA)` 里的分号、
// `content: ";"` 里的分号都不是声明分隔符，简单 SplitStr 会把声明切坏。
std::vector<std::string> SplitTopLevel(const std::string& s, char sep) {
    std::vector<std::string> out;
    int depth = 0;
    char quote = 0;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (quote) {
            cur.push_back(c);
            if (c == '\\' && i + 1 < s.size()) {
                cur.push_back(s[++i]);
                continue;
            }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; cur.push_back(c); continue; }
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') { if (depth > 0) depth--; }
        if (c == sep && depth == 0) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

// 媒体查询是否适用于当前桌面视口。@media 块不再破坏整个样式表：
// 没有宽度条件的（如 print、screen）一律采用；带宽度条件时按桌面宽度判断。
bool MediaQueryMatchesDesktop(const std::string& query) {
    const int kDesktopWidth = 1280;
    std::string q = Lower(query);
    size_t pos = 0;
    while (true) {
        size_t p = q.find("width", pos);
        if (p == std::string::npos) break;
        size_t colon = q.find(':', p);
        if (colon == std::string::npos) break;
        std::string num;
        for (size_t i = colon + 1; i < q.size(); ++i) {
            if (std::isdigit((unsigned char)q[i])) num.push_back(q[i]);
            else if (!num.empty()) break;
        }
        int value = std::atoi(num.c_str());
        bool is_max = p >= 5 && q.compare(p - 5, 5, "max-w") == 0;
        bool is_min = p >= 5 && q.compare(p - 5, 5, "min-w") == 0;
        if (is_max && value > 0 && value < kDesktopWidth) return false;
        if (is_min && value > kDesktopWidth) return false;
        pos = colon + 1;
    }
    return true;
}

std::vector<CssRule> ParseCssText(const std::string& css_raw, int* index) {
    std::vector<CssRule> rules;
    std::string css = RemoveComments(css_raw);
    size_t pos = 0;
    // 按逗号拆分选择器组，但跳过 :not(...) / [attr="a,b"] 内部的逗号。
    auto split_selectors = [](const std::string& s) {
        std::vector<std::string> out;
        for (const auto& piece : SplitTopLevel(s, ',')) {
            if (!Trim(piece).empty()) out.push_back(Trim(piece));
        }
        return out;
    };
    while (pos < css.size()) {
        size_t open = css.find('{', pos);
        if (open == std::string::npos) break;
        std::string selector_text = Trim(css.substr(pos, open - pos));
        size_t close = FindMatchingBrace(css, open);
        if (close == std::string::npos) break;
        std::string body = css.substr(open + 1, close - open - 1);

        if (!selector_text.empty() && selector_text[0] == '@') {
            // @media / @supports 里是嵌套规则，递归解析；其余 at 规则跳过整个块。
            std::string at = Lower(selector_text);
            bool nested = StartsWith(at, "@media") || StartsWith(at, "@supports") ||
                          StartsWith(at, "@layer");
            if (nested && StartsWith(at, "@media")) {
                size_t brace = selector_text.find('{');
                std::string query = brace == std::string::npos
                                        ? selector_text.substr(6)
                                        : selector_text.substr(6, brace - 6);
                if (MediaQueryMatchesDesktop(query)) {
                    auto inner = ParseCssText(body, index);
                    for (auto& r : inner) rules.push_back(std::move(r));
                }
            } else if (nested) {
                auto inner = ParseCssText(body, index);
                for (auto& r : inner) rules.push_back(std::move(r));
            }
            pos = close + 1;
            continue;
        }
        pos = close + 1;

        std::vector<std::pair<std::string, std::string>> decls;
        for (const auto& decl : SplitTopLevel(body, ';')) {
            std::string d = Trim(decl);
            if (d.empty()) continue;
            // 跳过自定义属性（var() 解析另行处理）以外的普通声明；
            // 冒号要取第一个“不在括号内”的。
            int depth = 0;
            char quote = 0;
            size_t colon = std::string::npos;
            for (size_t i = 0; i < d.size(); ++i) {
                char c = d[i];
                if (quote) {
                    if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') { quote = c; continue; }
                if (c == '(') depth++;
                else if (c == ')') { if (depth > 0) depth--; }
                else if (c == ':' && depth == 0) { colon = i; break; }
            }
            if (colon == std::string::npos) continue;
            decls.emplace_back(Trim(d.substr(0, colon)),
                               Trim(d.substr(colon + 1)));
        }

        for (const auto& sel : split_selectors(selector_text)) {
            CssRule rule;
            rule.index = (*index)++;
            rule.parts.push_back(ParseSelectorChain(sel));
            rule.declarations = decls;
            rules.push_back(rule);
        }
    }
    return rules;
}

std::vector<CssRule> DefaultRules() {
    int idx = 0;
    return ParseCssText(kDefaultCss, &idx);
}

std::string TextOf(const Node* n);

void CollectStyleRules(const Node* node, std::vector<CssRule>& rules,
                       int* index) {
    if (!node) return;
    if (node->type == NodeType::Element && node->tag == "style") {
        std::string css = TextOf(node);
        auto parsed = ParseCssText(css, index);
        for (auto& r : parsed) rules.push_back(std::move(r));
    }
    for (const auto& c : node->children) CollectStyleRules(c.get(), rules, index);
}

std::string TextOf(const Node* n) {
    if (n->type == NodeType::Text) return n->text;
    std::string out;
    for (const auto& c : n->children) out += TextOf(c.get());
    return out;
}

Style ComputeStyle(const Node* node, const std::vector<CssRule>& rules,
                   const Style& parent_style) {
    Style s = parent_style;
    if (node->type != NodeType::Element) return s;

    // Non-inherited defaults.
    s.display = "block";
    s.background = "";
    s.background_image.clear();
    s.background_size = "auto";
    s.border_color = "#d1d5db";
    s.border_style = "none";
    s.border_width = 0;
    s.border_radius = 0;
    s.box_border_box = true;
    s.margin[0] = ZeroLength(); s.margin[1] = ZeroLength();
    s.margin[2] = ZeroLength(); s.margin[3] = ZeroLength();
    s.padding[0] = ZeroLength(); s.padding[1] = ZeroLength();
    s.padding[2] = ZeroLength(); s.padding[3] = ZeroLength();
    s.width = {};
    s.height = {};
    s.max_width = {};
    s.white_space = parent_style.white_space;
    s.positioned = false;
    s.left = {}; s.right = {}; s.top = {}; s.bottom = {};

    for (const auto& rule : rules) {
        if (MatchesRule(node, rule)) {
            for (const auto& d : rule.declarations) {
                ApplyDeclaration(s, d.first, d.second);
            }
        }
    }
    ApplyStyleAttr(node, s);

    if (node->tag == "video") {
        if (s.width.is_auto) {
            std::string w = node->Attr("width");
            if (!w.empty()) {
                SetLength(s.width, w);
            } else {
                s.width.is_auto = false;
                s.width.value = 640.f;
            }
        }
        if (s.height.is_auto) {
            std::string h = node->Attr("height");
            if (!h.empty()) {
                SetLength(s.height, h);
            } else {
                s.height.is_auto = false;
                s.height.value = 360.f;
            }
        }
    }

    if (node->tag == "style" || node->tag == "script" || node->tag == "head" ||
        node->tag == "title" || node->tag == "meta" || node->tag == "link" ||
        node->tag == "base" || node->tag == "iframe" || node->tag == "object" ||
        node->tag == "embed" || node->tag == "canvas" ||
        node->tag == "svg" || node->tag == "path" ||
        node->tag == "g" || node->tag == "defs" || node->tag == "symbol" ||
        node->tag == "use" || node->tag == "circle" || node->tag == "rect" ||
        node->tag == "line" || node->tag == "polygon" || node->tag == "polyline") {
        s.display = "none";
    }
    return s;
}

bool IsVisible(const Style& s) {
    return s.display != "none";
}

bool IsBlock(const Style& s) {
    return IsVisible(s) && s.display != "inline" && s.display != "inline-block";
}

int BorderSize(const Style& s) {
    return s.border_width;
}

int ResolveLength(const Length& l, int parent_w, int fallback = 0) {
    if (l.is_auto) return fallback;
    if (l.percent) return (int)(l.value * parent_w / 100.f);
    return (int)l.value;
}

int ClampMaxWidth(const Style& s, int width, int parent_w) {
    if (s.max_width.is_auto) return width;
    int max_w = ResolveLength(s.max_width, parent_w, width);
    return std::max(0, std::min(width, max_w));
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
    std::string result;
    for (const auto& p : out) {
        result += "/";
        result += p;
    }
    return result.empty() ? "/" : result;
}

std::string ResolveUrl(const std::string& base, const std::string& href_raw) {
    std::string href = Trim(href_raw);
    if (href.empty()) return StripFragment(base);
    size_t frag = href.find('#');
    if (frag != std::string::npos) href = href.substr(0, frag);
    if (href.empty()) return StripFragment(base);
    if (href.find("://") != std::string::npos || StartsWith(href, "data:") ||
        StartsWith(href, "about:") || StartsWith(href, "browser:") ||
        StartsWith(href, "file:")) {
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
    std::string base_path =
        slash == std::string::npos ? "/" : base_clean.substr(slash);
    size_t bq = base_path.find('?');
    if (bq != std::string::npos) base_path = base_path.substr(0, bq);
    size_t last = base_path.find_last_of('/');
    std::string dir = last == std::string::npos
                          ? "/"
                          : base_path.substr(0, last + 1);
    return origin + NormalizePath(dir + path_part) + query;
}

void CollectVideoNodes(Node* node, std::vector<Node*>& out) {
    if (!node) return;
    if (node->type == NodeType::Element && node->tag == "video") {
        out.push_back(node);
    }
    for (auto& c : node->children) CollectVideoNodes(c.get(), out);
}

const Box* FindVideoBox(const Box* box, int x, int y, bool want_fixed,
                        bool in_fixed) {
    if (!box || box->hidden) return nullptr;
    bool cur_fixed = in_fixed || box->fixed;
    // 普通搜索与 fixed 搜索分开：遇到另一类子树直接剪枝，避免坐标混用。
    if (cur_fixed != want_fixed) return nullptr;
    if (box->node && box->node->tag == "video" && box->rect.contains(x, y)) {
        return box;
    }
    for (const auto& c : box->children) {
        if (const Box* found = FindVideoBox(c.get(), x, y, want_fixed, cur_fixed))
            return found;
    }
    return nullptr;
}

// --------------------------------------------------------------------------
// Inline text layout
// --------------------------------------------------------------------------

struct InlinePiece {
    std::string text;
    Style style;
    bool link = false;
    std::string href;
    bool hard_break = false;
    // 图片替换元素（<img>）。非空时按 image_w/image_h 参与行内布局。
    std::shared_ptr<Image> image;
    int image_w = 0;
    int image_h = 0;
    std::string alt_text;
    // 表单控件（input/button/select/textarea）。非空时按 widget_w/widget_h 排布。
    std::string widget;
    int widget_w = 0;
    int widget_h = 0;
    std::string widget_value;
};

// 构造行内片段。字段较多，用具名构造避免聚合初始化漏字段告警。
InlinePiece MakePiece(std::string text, const Style& style, bool link,
                      const std::string& href, bool hard_break = false) {
    InlinePiece p;
    p.text = std::move(text);
    p.style = style;
    p.link = link;
    p.href = href;
    p.hard_break = hard_break;
    return p;
}

std::string Utf8CharAt(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    size_t len = 1;
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    return s.substr(i, len);
}

void TokenizeText(const std::string& text, const Style& style, bool link,
                  const std::string& href, std::vector<InlinePiece>& out,
                  bool preserve_space = false) {
    if (preserve_space) {
        size_t start = 0;
        while (start <= text.size()) {
            size_t nl = text.find('\n', start);
            std::string line = nl == std::string::npos
                                   ? text.substr(start)
                                   : text.substr(start, nl - start);
            if (!line.empty()) out.push_back(MakePiece(line, style, link, href));
            if (nl == std::string::npos) break;
            out.push_back(MakePiece("", style, link, href, true));
            start = nl + 1;
        }
        return;
    }
    std::string word;
    size_t i = 0;
    bool pending_space = false;
    auto flush_word = [&] {
        if (!word.empty()) {
            if (pending_space && !out.empty()) out.push_back(MakePiece(" ", style, link, href));
            pending_space = false;
            out.push_back(MakePiece(word, style, link, href));
            word.clear();
        }
    };
    while (i < text.size()) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            bool has_word = !word.empty();
            flush_word();
            pending_space = false;
            // 纯空白文本节点不应产生空行：HTML 中块级元素之间的换行/缩进
            // 在 CSS 空白折叠后不占任何高度。只有实际内容后的换行才断行。
            if (has_word) out.push_back(MakePiece("", style, link, href, true));
            i++;
            continue;
        }
        if (c == ' ' || c == '\t') {
            flush_word();
            pending_space = true;
            i++;
            continue;
        }
        unsigned char u = (unsigned char)c;
        if (u >= 0x80) {
            flush_word();
            if (pending_space && !out.empty() && out.back().text != " ") {
                out.push_back(MakePiece(" ", style, link, href));
            }
            pending_space = false;
            std::string ch = Utf8CharAt(text, i);
            i += ch.size();
            out.push_back(MakePiece(ch, style, link, href));
            continue;
        }
        if (std::isspace(c)) {
            flush_word();
            i++;
            continue;
        }
        word.push_back(c);
        i++;
    }
    flush_word();
    if (pending_space && !out.empty() && out.back().text != " ") {
        out.push_back(MakePiece(" ", style, link, href));
    }
}

int LineHeightOf(const Style& s);

void CollectInline(const Node* node, Style parent,
                   const std::vector<CssRule>& rules, int container_w,
                   bool link, const std::string& parent_href,
                   std::vector<InlinePiece>& out) {
    if (node->type == NodeType::Text) {
        TokenizeText(node->text, parent, link, parent_href, out,
                     parent.white_space == "pre" || parent.white_space == "pre-wrap");
        return;
    }
    if (node->type != NodeType::Element) return;
    Style s = ComputeStyle(node, rules, parent);
    if (!IsVisible(s)) return;
    if (node->tag == "img") {
        int iw = 0, ih = 0;
        if (node->image) {
            iw = node->image->width;
            ih = node->image->height;
        }
        int w = 0, h = 0;
        if (!s.width.is_auto) {
            w = ResolveLength(s.width, container_w);
        } else if (!node->Attr("width").empty()) {
            w = (int)std::atof(node->Attr("width").c_str());
        }
        if (!s.height.is_auto) {
            h = ResolveLength(s.height, container_w);
        } else if (!node->Attr("height").empty()) {
            h = (int)std::atof(node->Attr("height").c_str());
        }
        // 只指定一边时按图片内在长宽比补另一边；都没有时用内在尺寸。
        if (iw > 0 && ih > 0) {
            if (w > 0 && h <= 0) h = (int)(w * (double)ih / iw);
            else if (h > 0 && w <= 0) w = (int)(h * (double)iw / ih);
            else if (w <= 0 && h <= 0) { w = iw; h = ih; }
        }
        if (w <= 0) w = iw > 0 ? iw : 160;
        if (h <= 0) h = ih > 0 ? ih : (iw > 0 ? 90 : 90);
        InlinePiece p;
        p.text = "";
        p.style = s;
        p.link = link;
        p.href = parent_href;
        p.image = node->image;
        p.image_w = w;
        p.image_h = h;
        p.alt_text = node->Attr("alt");
        out.push_back(std::move(p));
        return;
    }
    if (node->tag == "input" || node->tag == "button" ||
        node->tag == "select" || node->tag == "textarea") {
        InlinePiece p;
        p.text = "";
        p.style = s;
        p.link = link;
        p.href = parent_href;
        p.widget = node->tag;
        int fh = LineHeightOf(s);
        if (node->tag == "textarea") {
            p.widget_w = !s.width.is_auto
                             ? ResolveLength(s.width, container_w)
                             : 240;
            p.widget_h = !s.height.is_auto
                             ? ResolveLength(s.height, container_w)
                             : (int)(fh * 4);
        } else {
            std::string v = node->tag == "button"
                                ? Trim(TextOf(node))
                                : node->Attr("value");
            if (node->tag == "input") {
                std::string type = Lower(node->Attr("type"));
                if (type == "submit" || type == "button" || type == "reset") {
                    p.widget = "button";
                    v = node->Attr("value");
                    if (v.empty()) v = type == "submit" ? "提交" : type;
                } else {
                    v = node->Attr("value");
                    if (v.empty()) v = node->Attr("placeholder");
                }
            }
            if (node->tag == "select") {
                for (const auto& c : node->children) {
                    if (c->type == NodeType::Element && c->tag == "option") {
                        v = Trim(TextOf(c.get()));
                        break;
                    }
                }
            }
            p.widget_value = v;
            p.widget_w = !s.width.is_auto
                             ? ResolveLength(s.width, container_w)
                             : (int)std::max(60.0, p.widget_value.size() * 7.0 + 32);
            p.widget_h = fh + 10;
        }
        out.push_back(std::move(p));
        return;
    }
    std::string href = parent_href;
    if (s.display != "inline" && s.display != "inline-block") {
        // A block nested in an inline context is flattened as text.
        for (const auto& c : node->children) {
            CollectInline(c.get(), s, rules, container_w, link, href, out);
        }
        return;
    }
    if (node->tag == "a") {
        link = true;
        href = node->Attr("href");
        s.underline = true;
        s.color = "#2563eb";
    }
    if (node->tag == "b" || node->tag == "strong") s.bold = true;
    if (node->tag == "i" || node->tag == "em") s.italic = true;
    if (node->tag == "br") {
        out.push_back(MakePiece("", s, link, href, true));
        return;
    }
    for (const auto& c : node->children) {
        CollectInline(c.get(), s, rules, container_w, link, href, out);
    }
}

struct PlacedRun {
    TextRun run;
    int rel_x = 0;
    int width = 0;
};

int LineHeightOf(const Style& s) {
    return std::max(2, (int)std::round(s.font_size * s.line_height));
}

// 把行内 pieces 排成若干行并追加到 box.runs。
// start_y >= 0 时从该 y 开始（块流交错布局用匿名块盒），否则从 box.content.y 开始。
// 返回本次行内内容占用的高度。
int LayoutInlineInto(Box& box, const std::vector<InlinePiece>& pieces,
                     Canvas* canvas, int start_y = -1) {
    if (pieces.empty()) return 0;
    int base_y = start_y >= 0 ? start_y : box.content.y;
    int line_height = LineHeightOf(box.style);
    int x = box.content.x;
    int y = base_y;
    int right = box.content.x + box.content.w;
    std::vector<PlacedRun> line;
    std::vector<TextRun> final_runs;
    int line_width = 0;

    auto flush_line = [&] {
        if (line.empty()) return;
        int shift = 0;
        if (box.style.text_align == "center") shift = std::max(0, box.content.w - line_width) / 2;
        else if (box.style.text_align == "right") shift = std::max(0, box.content.w - line_width);
        int descent = std::max(1, line_height / 5);
        int baseline = y + line_height - descent;
        for (auto& p : line) {
            p.run.rect.x = box.content.x + shift + p.rel_x;
            if (p.run.image || p.run.image_missing ||
                !p.run.widget.empty()) {
                // 替换元素底边对齐文本基线（浏览器默认 vertical-align:baseline）。
                p.run.rect.y = baseline - p.run.rect.h;
            } else {
                p.run.rect.y = y;
                p.run.rect.h = line_height;
            }
            p.run.rect.w = p.width;
            final_runs.push_back(p.run);
        }
        line.clear();
        line_width = 0;
    };

    for (const auto& piece : pieces) {
        if (piece.hard_break) {
            flush_line();
            y += line_height;
            x = box.content.x;
            continue;
        }
        if (piece.image || (piece.image_w > 0 && piece.image_h > 0)) {
            int iw = piece.image_w;
            int ih = piece.image_h;
            if (iw <= 0) iw = 160;
            if (ih <= 0) ih = 90;
            if (x + iw > right && !line.empty()) {
                flush_line();
                y += line_height;
                x = box.content.x;
            }
            if (ih > line_height) line_height = ih;
            TextRun tr;
            tr.image = piece.image;
            tr.image_missing = !piece.image;
            tr.alt_text = piece.alt_text;
            tr.link = piece.link;
            tr.href = piece.href;
            tr.rect.h = ih;
            line.push_back({tr, x - box.content.x, iw});
            line_width += iw;
            x += iw;
            continue;
        }
        if (!piece.widget.empty()) {
            int iw = std::max(1, piece.widget_w);
            int ih = std::max(1, piece.widget_h);
            if (x + iw > right && !line.empty()) {
                flush_line();
                y += line_height;
                x = box.content.x;
            }
            if (ih > line_height) line_height = ih;
            TextRun tr;
            tr.widget = piece.widget;
            tr.widget_value = piece.widget_value;
            tr.link = piece.link;
            tr.href = piece.href;
            tr.rect.h = ih;
            line.push_back({tr, x - box.content.x, iw});
            line_width += iw;
            x += iw;
            continue;
        }
        if (piece.text.empty()) continue;
        bool all_space = piece.text.find_first_not_of(" \t\r\n") ==
                         std::string::npos;
        // 行首的纯空白不占位，避免图片/文本行尾的换行缩进产生幽灵空行。
        if (all_space && line.empty()) continue;
        int w = (int)canvas->MeasureText(piece.text, piece.style.font_size,
                                         piece.style.bold);
        if (w <= 0) continue;
        // 行尾放不下的空格直接丢弃（浏览器折叠行尾空白），
        // 否则它会另起一行并继承上一个替换元素的行高，凭空撑高容器。
        if (all_space && x + w > right) continue;
        if (x + w > right && !line.empty()) {
            flush_line();
            y += line_height;
            x = box.content.x;
        }
        if (w > box.content.w) {
            // Break a too-long single word.
            std::string rest = piece.text;
            while (!rest.empty()) {
                int take = 1;
                while (take < (int)rest.size()) {
                    int w2 = (int)canvas->MeasureText(
                        rest.substr(0, take + 1), piece.style.font_size,
                        piece.style.bold);
                    if (x + w2 > right && !line.empty()) break;
                    if (w2 > box.content.w) break;
                    take++;
                }
                if (x + take > right && !line.empty()) {  // placeholder guard
                }
                if (x + take > right && !line.empty()) {
                }
                std::string chunk = rest.substr(0, take);
                int cw = (int)canvas->MeasureText(chunk, piece.style.font_size,
                                                  piece.style.bold);
                if (x + cw > right && !line.empty()) {
                    flush_line();
                    y += line_height;
                    x = box.content.x;
                }
                TextRun tr;
                tr.text = chunk;
                tr.color = piece.style.color;
                tr.font_size = piece.style.font_size;
                tr.italic = piece.style.italic;
                tr.bold = piece.style.bold;
                tr.underline = piece.style.underline;
                tr.link = piece.link;
                tr.href = piece.href;
                line.push_back({tr, x - box.content.x, cw});
                line_width += cw;
                x += cw;
                rest = rest.substr(take);
            }
            continue;
        }
        TextRun tr;
        tr.text = piece.text;
        tr.color = piece.style.color;
        tr.font_size = piece.style.font_size;
        tr.italic = piece.style.italic;
        tr.bold = piece.style.bold;
        tr.underline = piece.style.underline;
        tr.link = piece.link;
        tr.href = piece.href;
        line.push_back({tr, x - box.content.x, w});
        line_width += w;
        x += w;
    }
    flush_line();
    int used = final_runs.empty()
                   ? 0
                   : (int)final_runs.back().rect.y + line_height - base_y;
    if (start_y < 0 && used > box.content.h) {
        box.content.h = used;
    }
    box.runs.insert(box.runs.end(), final_runs.begin(), final_runs.end());
    return used;
}

void PopulateBoxes(Box& parent, const std::vector<CssRule>& rules) {
    if (!parent.node) return;
    for (const auto& c : parent.node->children) {
        if (c->type != NodeType::Element) continue;
        Style s = ComputeStyle(c.get(), rules, parent.style);
        if (!IsVisible(s) || !IsBlock(s)) continue;
        auto child = std::make_unique<Box>();
        child->node = c.get();
        child->style = s;
        child->rules = &rules;
        PopulateBoxes(*child, rules);
        parent.children.push_back(std::move(child));
    }
}

void LayoutBox(Box& box, Canvas* canvas);

std::string ListMarkerFor(const Node* li);
bool IsWhitespaceOnly(const std::string& s);

// 定位单个块级子盒：宽度、margin、auto 居中、递归布局，并推进流位置 y。
// absolute/fixed 不占流：仍布局内部内容，但不推进 y、不影响父容器高度。
void PlaceBlockChild(Box& box, Box& child, int& y, int& bottom, Canvas* canvas) {
    bool out_of_flow = child.style.position == "absolute" ||
                       child.style.position == "fixed";
    if (out_of_flow) {
        int w = child.style.width.is_auto
                    ? box.content.w
                    : ResolveLength(child.style.width, box.content.w);
        w = ClampMaxWidth(child.style, w, box.content.w);
        child.rect.x = box.content.x;
        child.rect.y = box.content.y;
        child.rect.w = w;
        int border = BorderSize(child.style);
        child.content.x = child.rect.x + border + child.style.PaddingLeft();
        child.content.y = child.rect.y + border + child.style.PaddingTop();
        child.content.w =
            std::max(0, w - 2 * border - child.style.PaddingLeft() -
                            child.style.PaddingRight());
        child.content.h = 0;
        LayoutBox(child, canvas);
        if (child.style.height.is_auto) {
            child.rect.h = child.content.h + 2 * border +
                           child.style.PaddingTop() + child.style.PaddingBottom();
        }
        return;
    }
    int ml = child.style.MarginLeft();
    int mr = child.style.MarginRight();
    int mt = child.style.MarginTop();
    int mb = child.style.MarginBottom();
    int avail_w = box.content.w;
    int w = child.style.width.is_auto
                ? std::max(0, avail_w - ml - mr)
                : ResolveLength(child.style.width, avail_w);
    w = std::min(w, std::max(0, avail_w - ml - mr));
    w = ClampMaxWidth(child.style, w, avail_w);
    int x = box.content.x + ml;
    int leftover = avail_w - ml - mr - w;
    if (child.style.margin[3].is_auto && child.style.margin[1].is_auto) {
        x = box.content.x + leftover / 2;
    } else if (child.style.margin[3].is_auto) {
        x = box.content.x + leftover;
    }
    child.rect.x = x;
    child.rect.y = y + mt;
    child.rect.w = w;
    int border = BorderSize(child.style);
    child.content.x = child.rect.x + border + child.style.PaddingLeft();
    child.content.y = child.rect.y + border + child.style.PaddingTop();
    child.content.w = std::max(0, w - 2 * border - child.style.PaddingLeft() -
                                      child.style.PaddingRight());
    child.content.h =
        child.style.height.is_auto
            ? 0
            : std::max(0, ResolveLength(child.style.height, box.content.h) -
                              2 * border - child.style.PaddingTop() -
                              child.style.PaddingBottom());
    LayoutBox(child, canvas);
    if (child.style.height.is_auto) {
        child.rect.h = child.content.h + 2 * border +
                       child.style.PaddingTop() + child.style.PaddingBottom();
    }
    y += mt + child.rect.h + mb;
    bottom = std::max(bottom, y - box.content.y);
}

// 块容器的主布局：严格按文档顺序交错排布行内内容与块级子盒。
// 连续的行内内容构成一个“匿名块盒”（浏览器行为），否则 label/input 这种
// 交替结构会被拆成“所有行内内容 + 所有块”两段，表单控件全跑到容器顶部。
void LayoutBlockFlow(Box& box, Canvas* canvas) {
    std::map<Node*, Box*> by_node;
    for (auto& c : box.children) {
        if (c->node) by_node[c->node] = c.get();
    }

    const std::vector<CssRule>& rules =
        box.rules ? *box.rules : std::vector<CssRule>{};
    std::vector<InlinePiece> buf;
    int y = box.content.y;
    int bottom = 0;
    bool marker_done = false;

    auto is_inline_display = [](const std::string& d) {
        return d == "inline" || d == "inline-block";
    };

    auto flush = [&]() {
        if (buf.empty()) return;
        if (!marker_done && box.node && box.node->tag == "li") {
            Style ms = box.style;
            ms.bold = false;
            ms.underline = false;
            buf.insert(buf.begin(),
                       MakePiece(ListMarkerFor(box.node), ms, false, ""));
            marker_done = true;
        }
        int h = LayoutInlineInto(box, buf, canvas, y);
        y += h;
        bottom = std::max(bottom, y - box.content.y);
        buf.clear();
    };

    if (box.node) {
        const auto& kids = box.node->children;
        for (size_t i = 0; i < kids.size(); ++i) {
            const Node* c = kids[i].get();
            if (c->type == NodeType::Text) {
                if (IsWhitespaceOnly(c->text)) {
                    // 块级边界处的换行/缩进要折叠掉；但行内元素之间的空白
                    // 必须保留成一个空格（<a>登录</a> <a>注册</a>）。
                    // 末尾空白一律丢弃——否则宽图片后面的缩进会变成“第二行”，
                    // 并继承图片行高，把容器撑成两倍高。
                    bool has_next = false;
                    bool next_inline = false;
                    for (size_t j = i + 1; j < kids.size(); ++j) {
                        if (kids[j]->type != NodeType::Element) continue;
                        has_next = true;
                        Style ns = ComputeStyle(kids[j].get(), rules, box.style);
                        next_inline =
                            IsVisible(ns) && is_inline_display(ns.display);
                        break;
                    }
                    if (!buf.empty() && has_next && next_inline) {
                        buf.push_back(MakePiece(" ", box.style, false, ""));
                    }
                    continue;
                }
                TokenizeText(c->text, box.style, false, "", buf,
                             box.style.white_space == "pre" ||
                                 box.style.white_space == "pre-wrap");
                continue;
            }
            if (c->type != NodeType::Element) continue;
            Style cs = ComputeStyle(c, rules, box.style);
            if (!IsVisible(cs)) continue;
            if (!is_inline_display(cs.display)) {
                auto it = by_node.find(const_cast<Node*>(c));
                if (it != by_node.end()) {
                    // 绝对/固定定位的盒子不打断行内流（它脱离文档流）。
                    if (cs.position != "absolute" && cs.position != "fixed") {
                        flush();
                    }
                    PlaceBlockChild(box, *it->second, y, bottom, canvas);
                    continue;
                }
                // 没有对应盒子：退化为行内内容，避免整块丢失。
            }
            CollectInline(c, box.style, rules, box.content.w, false, "", buf);
        }
    }
    flush();
    box.content.h = std::max(box.content.h, y - box.content.y);
    box.scroll_height = box.content.h;
}

void LayoutColumn(Box& box, Canvas* canvas) {
    int y = box.content.y;
    int used = 0;
    int gap = box.style.gap;
    int n = (int)box.children.size();
    for (int i = 0; i < n; ++i) {
        Box* child = box.children[i].get();
        int w = child->style.width.is_auto
                    ? box.content.w
                    : std::min(ResolveLength(child->style.width, box.content.w),
                               box.content.w);
        w = ClampMaxWidth(child->style, w, box.content.w);
        int border = BorderSize(child->style);
        child->rect.x = box.content.x;
        child->rect.y = y;
        child->rect.w = w;
        child->content.x = child->rect.x + border + child->style.PaddingLeft();
        child->content.y = child->rect.y + border + child->style.PaddingTop();
        child->content.w =
            std::max(0, w - 2 * border - child->style.PaddingLeft() -
                            child->style.PaddingRight());
        child->content.h =
            child->style.height.is_auto
                ? 0
                : std::max(0, ResolveLength(child->style.height, box.content.h) -
                                  2 * border - child->style.PaddingTop() -
                                  child->style.PaddingBottom());
        LayoutBox(*child, canvas);
        child->rect.h = child->content.h + 2 * border +
                        child->style.PaddingTop() +
                        child->style.PaddingBottom();
        y += child->rect.h;
        used += child->rect.h;
        if (i + 1 < n) {
            y += gap;
            used += gap;
        }
    }
    box.content.h = std::max(box.content.h, used);
    box.scroll_height = box.content.h;
}

void LayoutFlexRow(Box& box, Canvas* canvas) {
    int n = (int)box.children.size();
    if (n == 0) return;
    int gap = box.style.gap;
    int total_gap = gap * (n - 1);
    int avail = std::max(0, box.content.w - total_gap);
    std::vector<int> widths(n, 0);
    int fixed = 0;
    int auto_count = 0;
    for (int i = 0; i < n; ++i) {
        if (!box.children[i]->style.width.is_auto) {
            widths[i] = ResolveLength(box.children[i]->style.width, box.content.w);
            fixed += widths[i];
        } else {
            auto_count++;
        }
    }
    int auto_w = auto_count ? std::max(0, (avail - fixed) / auto_count) : 0;
    for (int i = 0; i < n; ++i) if (widths[i] == 0) widths[i] = auto_w;
    for (int i = 0; i < n; ++i) {
        widths[i] = ClampMaxWidth(box.children[i]->style, widths[i],
                                  box.content.w);
    }

    int total = fixed + auto_w * auto_count + total_gap;
    int x = box.content.x;
    if (box.style.justify_content == "center") x += std::max(0, (box.content.w - total) / 2);
    else if (box.style.justify_content == "flex-end") x += std::max(0, box.content.w - total);
    else if (box.style.justify_content == "space-between" && n > 1) {
        int extra = std::max(0, box.content.w - (fixed + auto_w * auto_count));
        total_gap = extra;  // distribute in loop below
    }

    std::vector<int> heights(n, 0);
    std::vector<int> xs(n, 0);
    int max_h = 0;
    for (int i = 0; i < n; ++i) {
        Box* child = box.children[i].get();
        int border = BorderSize(child->style);
        child->rect.x = x;
        child->rect.y = box.content.y;
        child->rect.w = widths[i];
        child->content.x = child->rect.x + border + child->style.PaddingLeft();
        child->content.y = child->rect.y + border + child->style.PaddingTop();
        child->content.w =
            std::max(0, widths[i] - 2 * border - child->style.PaddingLeft() -
                            child->style.PaddingRight());
        child->content.h =
            child->style.height.is_auto
                ? 0
                : std::max(0, ResolveLength(child->style.height, box.content.h) -
                                  2 * border - child->style.PaddingTop() -
                                  child->style.PaddingBottom());
        LayoutBox(*child, canvas);
        child->rect.h = child->content.h + 2 * border +
                        child->style.PaddingTop() + child->style.PaddingBottom();
        heights[i] = child->rect.h;
        max_h = std::max(max_h, child->rect.h);
        xs[i] = child->rect.x;
        if (box.style.justify_content == "space-between" && n > 1) {
            x += widths[i] + (box.content.w - total) / (n - 1);
        } else {
            x += widths[i];
            if (i + 1 < n) x += gap;
        }
    }
    box.content.h = std::max(box.content.h, max_h);
    box.scroll_height = box.content.h;
    if (box.style.align_items == "center" || box.style.align_items == "flex-end") {
        for (int i = 0; i < n; ++i) {
            int dy = box.style.align_items == "center"
                         ? (max_h - heights[i]) / 2
                         : max_h - heights[i];
            Box* child = box.children[i].get();
            child->rect.y = box.content.y + dy;
            child->content.y = child->rect.y + BorderSize(child->style) +
                               child->style.PaddingTop();
        }
    }
}

// 把 "120px 1fr repeat(2, 1fr)" 拆成若干 track 字符串。
// 空格和顶层逗号都是分隔符；括号内的逗号要保留。
std::vector<std::string> SplitGridTracks(const std::string& value) {
    std::vector<std::string> out;
    if (value.empty()) return out;
    size_t i = 0;
    int depth = 0;
    std::string cur;
    while (i < value.size()) {
        char c = value[i];
        if (c == '(') {
            depth++;
            cur.push_back(c);
            i++;
        } else if (c == ')') {
            if (depth > 0) depth--;
            cur.push_back(c);
            i++;
        } else if (depth == 0 && (c == ' ' || c == '\t' || c == '\n' ||
                                   c == ',')) {
            if (!Trim(cur).empty()) out.push_back(Trim(cur));
            cur.clear();
            i++;
        } else {
            cur.push_back(c);
            i++;
        }
    }
    if (!Trim(cur).empty()) out.push_back(Trim(cur));
    return out;
}

struct GridTrack {
    bool is_fr = false;
    bool is_auto = true;
    float value = 0.f;
    bool is_percent = false;
};

bool ParseGridTrack(const std::string& raw, GridTrack& t) {
    std::string v = Lower(Trim(raw));
    if (v.empty() || v == "auto" || v == "min-content" ||
        v == "max-content") {
        t.is_auto = true;
        return true;
    }
    if (v == "0") {
        t.is_fr = false;
        t.is_auto = false;
        t.value = 0.f;
        return true;
    }
    if (EndsWith(v, "fr")) {
        t.is_fr = true;
        t.is_auto = false;
        t.value = (float)std::atof(v.substr(0, v.size() - 2).c_str());
        return true;
    }
    if (EndsWith(v, "%")) {
        t.is_percent = true;
        t.is_auto = false;
        t.value = (float)std::atof(v.substr(0, v.size() - 1).c_str());
        return true;
    }
    if (EndsWith(v, "px")) v = v.substr(0, v.size() - 2);
    t.is_fr = false;
    t.is_auto = false;
    t.value = (float)std::atof(v.c_str());
    return true;
}

// minmax(A, B)：取 A；repeat(N, X)：复制 N 份。够覆盖常见 grid。
std::vector<std::string> ExpandGridTracks(const std::string& value) {
    std::vector<std::string> raw = SplitGridTracks(value);
    std::vector<std::string> out;
    for (std::string t : raw) {
        if (StartsWith(t, "repeat(")) {
            size_t p = t.find(',');
            size_t pe = t.find_last_of(')');
            if (p != std::string::npos && pe != std::string::npos) {
                int n = std::atoi(t.substr(7, p - 7).c_str());
                std::string inner = t.substr(p + 1, pe - p - 1);
                n = std::max(1, n);
                for (int i = 0; i < n; ++i) out.push_back(Trim(inner));
            }
        } else if (StartsWith(t, "minmax(")) {
            size_t p = t.find(',');
            size_t pe = t.find_last_of(')');
            if (p != std::string::npos && pe != std::string::npos) {
                out.push_back(Trim(t.substr(p + 1, pe - p - 1)));
            } else {
                out.push_back(t);
            }
        } else {
            out.push_back(t);
        }
    }
    if (out.empty()) out.push_back("auto");
    return out;
}

int ParseGridSpan(const Style& s, int fallback) {
    // 支持 grid-column: span N / 1 / 1 / 3 / 1 / span 2。
    const std::string& v = s.grid_column;
    if (v.empty()) return fallback;
    if (StartsWith(v, "span ")) {
        return std::max(1, std::atoi(v.c_str() + 5));
    }
    // "1 / 3" 形式：只认跨度。
    if (v.find('/') != std::string::npos) {
        auto parts = SplitStr(v, '/');
        if (parts.size() == 2) {
            int end = std::atoi(Trim(parts[1]).c_str());
            int start = std::atoi(Trim(parts[0]).c_str());
            if (end > start && start >= 1) return end - start;
        }
    }
    return fallback;
}

void LayoutGrid(Box& box, Canvas* canvas) {
    int n = (int)box.children.size();
    if (n == 0) return;
    std::vector<std::string> tracks = ExpandGridTracks(box.style.grid_template_columns);
    int cols = (int)tracks.size();
    if (cols <= 0) cols = 1;

    // 先解析每列的基准宽度，再按 fr/剩余空间分摊。
    int gap = box.style.gap;
    int avail = std::max(0, box.content.w - gap * (cols - 1));
    std::vector<GridTrack> gt(cols);
    for (int i = 0; i < cols; ++i) ParseGridTrack(tracks[i], gt[i]);

    std::vector<int> widths(cols, 0);
    int fixed_total = 0;
    int fr_total = 0;
    for (int i = 0; i < cols; ++i) {
        if (gt[i].is_fr) {
            fr_total += (int)gt[i].value;
        } else if (!gt[i].is_auto) {
            int w = gt[i].is_percent
                        ? (int)(gt[i].value * box.content.w / 100.f)
                        : (int)gt[i].value;
            widths[i] = std::max(0, w);
            fixed_total += widths[i];
        }
    }
    int remain = std::max(0, avail - fixed_total);
    if (fr_total > 0) {
        for (int i = 0; i < cols; ++i) {
            if (gt[i].is_fr) {
                widths[i] = (int)(remain * gt[i].value / fr_total);
            }
        }
    }
    for (int i = 0; i < cols; ++i) {
        if (gt[i].is_auto) widths[i] = remain / std::max(1, cols);
    }

    // auto-flow row：逐行放置，支持跨列。
    std::vector<int> row_height(cols, 0);
    int col = 0;
    int y = box.content.y;
    int grid_bottom = box.content.y;
    for (int i = 0; i < n; ++i) {
        Box* child = box.children[i].get();
        if (child->hidden) continue;
        int span = ParseGridSpan(child->style, 1);
        span = std::max(1, std::min(span, cols));
        if (col + span > cols) {
            // 放不下则换行；把上一行最高高度累加进 y。
            int row_h = 0;
            for (int j = 0; j < cols; ++j) row_h = std::max(row_h, row_height[j]);
            y += row_h + gap;
            col = 0;
            std::fill(row_height.begin(), row_height.end(), 0);
        }
        int track_w = 0;
        for (int k = 0; k < span; ++k) track_w += widths[col + k];
        track_w += gap * (span - 1);
        int w = child->style.width.is_auto
                    ? track_w
                    : std::min(ResolveLength(child->style.width, box.content.w),
                               track_w);
        w = ClampMaxWidth(child->style, w, box.content.w);
        int border = BorderSize(child->style);
        // 实际 x = 前面所有列宽 + gap
        int x = box.content.x;
        for (int k = 0; k < col; ++k) x += widths[k] + gap;
        child->rect.x = x;
        child->rect.y = y;
        child->rect.w = w;
        child->content.x = child->rect.x + border + child->style.PaddingLeft();
        child->content.y = child->rect.y + border + child->style.PaddingTop();
        child->content.w = std::max(
            0, w - 2 * border - child->style.PaddingLeft() -
                   child->style.PaddingRight());
        child->content.h = child->style.height.is_auto
                               ? 0
                               : std::max(0, ResolveLength(child->style.height,
                                                           box.content.h) -
                                                 2 * border -
                                                 child->style.PaddingTop() -
                                                 child->style.PaddingBottom());
        LayoutBox(*child, canvas);
        child->rect.h = child->content.h + 2 * border +
                        child->style.PaddingTop() + child->style.PaddingBottom();
        for (int k = 0; k < span; ++k) {
            row_height[col + k] = std::max(row_height[col + k], child->rect.h);
        }
        grid_bottom = std::max(grid_bottom, y + child->rect.h);
        col += span;
        if (col >= cols) {
            // 本行放满：结算行高，下一行从新行开始。
            int row_h = 0;
            for (int j = 0; j < cols; ++j) row_h = std::max(row_h, row_height[j]);
            y += row_h + gap;
            col = 0;
            std::fill(row_height.begin(), row_height.end(), 0);
        }
    }
    for (int j = 0; j < cols; ++j) y = std::max(y, row_height[j]);
    box.content.h = std::max(box.content.h, grid_bottom - box.content.y);
    box.scroll_height = box.content.h;
}

std::string ListMarkerFor(const Node* li) {
    if (!li || !li->parent) return "• ";
    int index = 1;
    for (const auto& c : li->parent->children) {
        if (c.get() == li) break;
        if (c->type == NodeType::Element) index++;
    }
    if (li->parent->tag == "ol") return std::to_string(index) + ". ";
    return "• ";
}

bool IsWhitespaceOnly(const std::string& s) {
    for (char c : s) {
        if (!std::isspace((unsigned char)c)) return false;
    }
    return true;
}

// 平移一个盒子的全部后代（子盒 + 行内 runs）。
// 定位阶段改变盒子的 content 原点后，盒内已经排好的文字/图片必须一起搬家，
// 否则 fixed/absolute 盒的文字会留在原来的流位置（悬浮块看起来是空的）。
void ShiftBoxSubtree(Box& box, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    for (auto& r : box.runs) {
        r.rect.x += dx;
        r.rect.y += dy;
    }
    for (auto& c : box.children) {
        c->rect.x += dx;
        c->rect.y += dy;
        c->content.x += dx;
        c->content.y += dy;
        ShiftBoxSubtree(*c, dx, dy);
    }
}

// 把定位偏移应用到盒子树。relative 在流内位置基础上偏移；
// absolute 以父盒 content box 为基准，fixed 以视口为基准。
void ApplyPositioning(Box& box, const Rect& viewport) {
    // 先处理子节点，父节点偏移会统一作用于已经定位好的子节点坐标。
    for (auto& c : box.children) {
        if (c->hidden) continue;
        const Style& s = c->style;
        if (s.position == "relative") {
            int ox = 0, oy = 0;
            if (!s.left.is_auto) ox = box.style.Resolve(s.left, box.content.w, 0);
            else if (!s.right.is_auto) ox = -box.style.Resolve(s.right, box.content.w, 0);
            if (!s.top.is_auto) oy = box.style.Resolve(s.top, box.content.h, 0);
            else if (!s.bottom.is_auto) oy = -box.style.Resolve(s.bottom, box.content.h, 0);
            c->rect.x += ox;
            c->rect.y += oy;
            c->content.x += ox;
            c->content.y += oy;
            ShiftBoxSubtree(*c, ox, oy);
        } else if (s.position == "absolute") {
            // 找最近的 positioned 祖先：由调用链保证传入的是该祖先的 content box。
            // 这里直接基于父盒的 content box 处理最简单且覆盖绝大多数场景。
            Rect cb = {box.content.x, box.content.y, box.content.w,
                       box.content.h};
            int w = c->rect.w;
            int h = c->rect.h;
            if (!s.width.is_auto) w = box.style.Resolve(s.width, box.content.w, w);
            if (!s.height.is_auto) h = box.style.Resolve(s.height, box.content.h, h);
            int x = c->rect.x, y = c->rect.y;
            if (!s.left.is_auto) x = cb.x + box.style.Resolve(s.left, box.content.w, 0);
            else if (!s.right.is_auto) x = cb.x + cb.w - w - box.style.Resolve(s.right, box.content.w, 0);
            if (!s.top.is_auto) y = cb.y + box.style.Resolve(s.top, box.content.h, 0);
            else if (!s.bottom.is_auto) y = cb.y + cb.h - h - box.style.Resolve(s.bottom, box.content.h, 0);
            c->rect.x = x;
            c->rect.y = y;
            c->rect.w = w;
            c->rect.h = h;
            int old_cx = c->content.x;
            int old_cy = c->content.y;
            int border = BorderSize(s);
            c->content.x = x + border + s.PaddingLeft();
            c->content.y = y + border + s.PaddingTop();
            c->content.w = std::max(0, w - 2 * border - s.PaddingLeft() -
                                           s.PaddingRight());
            c->content.h =
                s.height.is_auto ? c->content.h
                                 : std::max(0, h - 2 * border - s.PaddingTop() -
                                                   s.PaddingBottom());
            ShiftBoxSubtree(*c, c->content.x - old_cx, c->content.y - old_cy);
        } else if (s.position == "fixed") {
            c->fixed = true;
            int w = c->rect.w;
            int h = c->rect.h;
            if (!s.width.is_auto) w = box.style.Resolve(s.width, viewport.w, w);
            if (!s.height.is_auto) h = box.style.Resolve(s.height, viewport.h, h);
            int x = c->rect.x, y = c->rect.y;
            // fixed 以视口为基准；rect 保持“视口顶部为 0”的局部坐标，
            // Paint/命中时不再减 scroll。
            if (!s.left.is_auto) x = viewport.x + box.style.Resolve(s.left, viewport.w, 0);
            else if (!s.right.is_auto) x = viewport.x + viewport.w - w - box.style.Resolve(s.right, viewport.w, 0);
            if (!s.top.is_auto) y = viewport.y + box.style.Resolve(s.top, viewport.h, 0);
            else if (!s.bottom.is_auto) y = viewport.y + viewport.h - h - box.style.Resolve(s.bottom, viewport.h, 0);
            c->rect.x = x;
            c->rect.y = y;
            c->rect.w = w;
            c->rect.h = h;
            int old_cx = c->content.x;
            int old_cy = c->content.y;
            int border = BorderSize(s);
            c->content.x = x + border + s.PaddingLeft();
            c->content.y = y + border + s.PaddingTop();
            c->content.w = std::max(0, w - 2 * border - s.PaddingLeft() -
                                           s.PaddingRight());
            c->content.h =
                s.height.is_auto ? c->content.h
                                 : std::max(0, h - 2 * border - s.PaddingTop() -
                                                   s.PaddingBottom());
            ShiftBoxSubtree(*c, c->content.x - old_cx, c->content.y - old_cy);
        }
        // 递归时给 absolute 子节点传它的父 content box，给 fixed 传无效基准。
        ApplyPositioning(*c, viewport);
    }
}

void LayoutBox(Box& box, Canvas* canvas) {
    if (!IsVisible(box.style)) {
        box.hidden = true;
        box.scroll_height = 0;
        return;
    }
    if (box.style.display == "flex") {
        // flex 容器里的文字/行内图片也是内容：先按行内布局排一列，
        // 再排真正的块级 flex 子项。否则“图标+文字”行会直接空白。
        std::vector<InlinePiece> pieces;
        if (box.node) {
            for (const auto& c : box.node->children) {
                if (c->type == NodeType::Text) {
                    if (IsWhitespaceOnly(c->text)) continue;
                    TokenizeText(c->text, box.style, false, "", pieces,
                                 box.style.white_space == "pre" ||
                                     box.style.white_space == "pre-wrap");
                } else {
                    Style cs = ComputeStyle(
                        c.get(), box.rules ? *box.rules
                                           : std::vector<CssRule>{},
                        box.style);
                    if (IsVisible(cs) && (cs.display == "inline" ||
                                          cs.display == "inline-block")) {
                        CollectInline(c.get(), box.style,
                                      box.rules ? *box.rules
                                                : std::vector<CssRule>{},
                                      box.content.w, false, "", pieces);
                    }
                }
            }
        }
        LayoutInlineInto(box, pieces, canvas);
        if (box.style.flex_direction == "column") {
            LayoutColumn(box, canvas);
        } else {
            LayoutFlexRow(box, canvas);
        }
        if (!box.style.height.is_auto) {
            box.rect.h = ResolveLength(box.style.height, box.content.h);
            box.content.h = std::max(
                0, box.rect.h - 2 * BorderSize(box.style) -
                       box.style.PaddingTop() - box.style.PaddingBottom());
        } else {
            box.rect.h = box.content.h + 2 * BorderSize(box.style) +
                         box.style.PaddingTop() + box.style.PaddingBottom();
        }
        return;
    }

    if (box.style.display == "grid") {
        // grid 容器同样可能混有行内文字/图片；先按行内布局排一列，
        // 再按 grid-template-columns 排真正的子项。
        std::vector<InlinePiece> pieces;
        if (box.node) {
            for (const auto& c : box.node->children) {
                if (c->type == NodeType::Text) {
                    if (IsWhitespaceOnly(c->text)) continue;
                    TokenizeText(c->text, box.style, false, "", pieces,
                                 box.style.white_space == "pre" ||
                                     box.style.white_space == "pre-wrap");
                } else {
                    Style cs = ComputeStyle(
                        c.get(), box.rules ? *box.rules
                                           : std::vector<CssRule>{},
                        box.style);
                    if (IsVisible(cs) && (cs.display == "inline" ||
                                          cs.display == "inline-block")) {
                        CollectInline(c.get(), box.style,
                                      box.rules ? *box.rules
                                                : std::vector<CssRule>{},
                                      box.content.w, false, "", pieces);
                    }
                }
            }
        }
        LayoutInlineInto(box, pieces, canvas);
        LayoutGrid(box, canvas);
        if (!box.style.height.is_auto) {
            box.rect.h = ResolveLength(box.style.height, box.content.h);
            box.content.h = std::max(
                0, box.rect.h - 2 * BorderSize(box.style) -
                       box.style.PaddingTop() - box.style.PaddingBottom());
        } else {
            box.rect.h = box.content.h + 2 * BorderSize(box.style) +
                         box.style.PaddingTop() + box.style.PaddingBottom();
        }
        return;
    }

    // 块容器：行内内容与块级子盒按文档顺序交错排布（见 LayoutBlockFlow）。
    LayoutBlockFlow(box, canvas);
    if (!box.style.height.is_auto) {
        box.rect.h = ResolveLength(box.style.height, box.content.h);
        box.content.h = std::max(
            0, box.rect.h - 2 * BorderSize(box.style) - box.style.PaddingTop() -
                   box.style.PaddingBottom());
    } else {
        box.rect.h = box.content.h + 2 * BorderSize(box.style) +
                     box.style.PaddingTop() + box.style.PaddingBottom();
    }
}

std::string FormatClock(double seconds) {
    if (seconds < 0 || seconds != seconds) seconds = 0;
    int total = (int)seconds;
    int m = total / 60;
    int s = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

void PaintVideoControls(Canvas* canvas, const Box& box, MediaPlayer* player,
                        int dx, int dy) {
    int bar_h = 34;
    int by = dy + box.rect.h - bar_h;
    canvas->FillRect(dx, by, box.rect.w, bar_h, 0x0f172a);

    int cx = dx + 18;
    int cy = by + bar_h / 2;
    if (player->IsPlaying()) {
        canvas->FillRect(cx - 6, cy - 7, 5, 14, 0xe2e8f0);
        canvas->FillRect(cx + 1, cy - 7, 5, 14, 0xe2e8f0);
    } else {
        for (int yy = cy - 8; yy <= cy + 8; ++yy) {
            int len = (int)((8 - std::abs(yy - cy)) * 12.0 / 8.0);
            canvas->StrokeLine(cx - 5, yy, cx - 5 + len, yy, 0xe2e8f0, 1);
        }
    }

    int tx = dx + 44;
    int tw = box.rect.w - 88;
    if (tw < 8) tw = 8;
    double dur = player->Duration();
    double pos = player->Position();
    canvas->FillRect(tx, cy - 3, tw, 6, 0x334155);
    int prog = dur > 0 ? (int)(tw * (pos / dur)) : 0;
    if (prog < 0) prog = 0;
    if (prog > tw) prog = tw;
    canvas->FillRect(tx, cy - 3, prog, 6, 0x38bdf8);

    std::string label = FormatClock(pos) + " / " + FormatClock(dur);
    int label_w = (int)canvas->MeasureText(label, 12, false);
    canvas->DrawText(label, dx + box.rect.w - label_w - 34, by + 10, 12,
                     0xcbd5e1, false, false, false);

    int mx = dx + box.rect.w - 20;
    uint32_t speaker = player->Muted() ? 0x64748b : 0xe2e8f0;
    canvas->FillRect(mx - 6, cy - 4, 6, 8, speaker);
    canvas->FillRect(mx, cy - 7, 3, 14, speaker);
}

// 暂停时在画面中央画一个播放标记，让“已暂停”一眼可辨。
// 纯自研绘制：圆角方块 + 逐行描出的三角形，不依赖任何外部资源。
void PaintPausedBadge(Canvas* canvas, const Box& box, int dx, int dy) {
    int size = 64;
    int bx = dx + (box.rect.w - size) / 2;
    int by = dy + (box.rect.h - size) / 2;
    canvas->FillRoundRect(bx, by, size, size, 12, 0x0f172a);
    canvas->StrokeRect(bx, by, size, size, 0x38bdf8);

    int cx = bx + size / 2;
    int cy = by + size / 2;
    for (int yy = cy - 14; yy <= cy + 14; ++yy) {
        int len = (int)((14 - std::abs(yy - cy)) * 18.0 / 14.0);
        canvas->StrokeLine(cx - 8, yy, cx - 8 + len, yy, 0x38bdf8, 1);
    }
}

void DrawWidget(Canvas* canvas, const TextRun& run, int dx, int dy) {
    const int w = run.rect.w;
    const int h = run.rect.h;
    bool button_like = run.widget == "button";
    if (button_like) {
        canvas->FillRect(dx, dy, w, h, run.widget_focused ? 0xddd6fe : 0xeef2ff);
        canvas->StrokeRect(dx, dy, w, h, 0x818cf8);
        int fs = std::max(11, std::min((int)(h * 0.5f), 15));
        int tw = (int)canvas->MeasureText(run.widget_value, fs, false);
        canvas->DrawText(run.widget_value,
                         dx + std::max(2, (w - tw) / 2),
                         dy + std::max(1, (h - fs) / 2), fs, 0x1e3a8a,
                         false, false, false);
        return;
    }
    int bg = run.widget_focused ? 0xfffbeb : 0xffffff;
    canvas->FillRect(dx, dy, w, h, bg);
    canvas->StrokeRect(dx, dy, w, h, run.widget_focused ? 0x6366f1
                                                         : 0x94a3b8);
    if (run.widget == "textarea") {
        int x = dx + 5;
        int y = dy + 4;
        int cw = std::max(7, w - 10);
        std::string line;
        int fs = std::max(12, std::min(14, h / 3));
        for (char ch : run.widget_value) {
            if (ch == '\n') {
                canvas->DrawText(line, x, y, fs, 0x0f172a, false, false, false);
                y += fs + 3;
                line.clear();
                continue;
            }
            line.push_back(ch);
            int tw2 = (int)canvas->MeasureText(line, fs, false);
            if (tw2 > cw && !line.empty()) {
                line.pop_back();
                canvas->DrawText(line, x, y, fs, 0x0f172a, false, false, false);
                y += fs + 3;
                line.clear();
                line.push_back(ch);
            }
        }
        if (!line.empty())
            canvas->DrawText(line, x, y, fs, 0x0f172a, false, false, false);
        return;
    }
    if (run.widget == "select") {
        canvas->DrawText(run.widget_value, dx + 6, dy + 3, 14, 0x0f172a,
                         false, false, false);
        int cx = dx + w - 16;
        int cy = dy + h / 2;
        canvas->DrawText("▶", cx, cy - 8, 10, 0x475569, false, false, false);
        return;
    }
    // input
    canvas->DrawText(run.widget_value, dx + 5, dy + 3, 14, 0x0f172a,
                     false, false, false);
    int fs = 14;
    if (run.widget_focused && run.widget_value.empty()) {
        canvas->DrawText("|", dx + 5, dy + 3, fs, 0x94a3b8, false, false,
                         false);
    }
}

void PaintBox(const Box& box, Canvas* canvas, const Rect& viewport,
              int scroll_y,
              const std::map<Node*, std::unique_ptr<MediaPlayer>>& media,
              const std::map<std::string, std::shared_ptr<Image>>& images,
              const std::string& base_url, bool fixed_ctx = false) {
    if (box.hidden || box.rect.w <= 0 || box.rect.h <= 0) return;

    // 坐标系约定（渲染与命中测试必须严格互逆）：
    //   屏幕坐标 = 视口原点 + 文档坐标 - 滚动量
    //   文档坐标 = 屏幕坐标 - 视口原点 + 滚动量   （见 BrowserApp::OnLButtonDown）
    // 之前这里写成 doc - viewport + scroll，既没有加视口原点、滚动方向也反了，
    // 结果是页面顶部被裁掉、内容整体上移，且与命中测试相差 2*viewport.y，
    // 导致视频控件条、进度条、链接都点不中。
    bool is_fixed = fixed_ctx || box.fixed;
    // fixed 子树使用视口局部坐标，不参与文档滚动裁剪。
    int view_top = is_fixed ? -2000000 : scroll_y;
    int view_bottom = is_fixed ? 2000000 : scroll_y + viewport.h;
    if (box.rect.y + box.rect.h < view_top || box.rect.y > view_bottom ||
        box.rect.x + box.rect.w < viewport.x ||
        box.rect.x > viewport.x + viewport.w) {
        return;
    }
    int dx = viewport.x + box.rect.x;
    int dy = is_fixed ? viewport.y + box.rect.y
                      : viewport.y + box.rect.y - scroll_y;
    Color bg = ColorFromCss(box.style.background);
    if (!bg.transparent) {
        if (box.style.border_radius > 0) {
            canvas->FillRoundRect(dx, dy, box.rect.w, box.rect.h,
                                  box.style.border_radius, bg.rgb());
        } else {
            canvas->FillRect(dx, dy, box.rect.w, box.rect.h, bg.rgb());
        }
    }
    // background-image：默认 cover（拉伸且保持比例、居中）。真实站点里
    // “整块背景图”远多于平铺，cover 比默认 auto 平铺更有用。
    if (!box.style.background_image.empty()) {
        std::string abs = ResolveUrl(base_url, box.style.background_image);
        auto img_it = images.find(abs);
        if (img_it != images.end() && img_it->second &&
            !img_it->second->bgra.empty()) {
            const Image* img = img_it->second.get();
            int iw = img->width;
            int ih = img->height;
            int dw = box.rect.w;
            int dh = box.rect.h;
            if (iw > 0 && ih > 0) {
                if (box.style.background_size == "contain") {
                    float s = std::min((float)dw / iw, (float)dh / ih);
                    dw = (int)(iw * s);
                    dh = (int)(ih * s);
                } else if (box.style.background_size != "stretch") {
                    // cover / auto
                    float s = std::max((float)dw / iw, (float)dh / ih);
                    dw = (int)(iw * s);
                    dh = (int)(ih * s);
                }
                int ox = dx + (box.rect.w - dw) / 2;
                int oy = dy + (box.rect.h - dh) / 2;
                // cover/contain 的图可能比盒子大；必须裁剪在 padding box 内，
                // 否则大背景图会溢出到相邻内容上。
                canvas->Clip(Rect{dx, dy, box.rect.w, box.rect.h});
                canvas->DrawImage(img->bgra.data(), iw, ih, ox, oy, dw, dh);
                canvas->ResetClip();
            }
        }
    }
    if (box.style.border_width > 0) {
        Color bc = ColorFromCss(box.style.border_color);
        if (bc.transparent) bc = {0x94, 0xa3, 0xb8};
        canvas->StrokeRect(dx, dy, box.rect.w, box.rect.h, bc.rgb());
    }

    if (box.node && box.node->tag == "video") {
        auto it = media.find(box.node);
        if (it != media.end() && it->second) {
            MediaPlayer* player = it->second.get();
            std::vector<uint8_t> frame;
            int fw = 0;
            int fh = 0;
            if (player->CopyFrame(&frame, &fw, &fh)) {
                float sx = (float)box.rect.w / (float)fw;
                float sy = (float)box.rect.h / (float)fh;
                float scale = sx < sy ? sx : sy;
                int dw = (int)(fw * scale);
                int dh = (int)(fh * scale);
                int ox = dx + (box.rect.w - dw) / 2;
                int oy = dy + (box.rect.h - dh) / 2;
                canvas->DrawImage(frame.data(), fw, fh, ox, oy, dw, dh);
            } else {
                canvas->FillRect(dx, dy, box.rect.w, box.rect.h, 0x000000);
                std::string message = player->Failed()
                                          ? "视频无法播放: " + player->Error()
                                          : "视频加载中...";
                canvas->DrawText(message, dx + 8, dy + 8, 13,
                                 player->Failed() ? 0xf87171 : 0x94a3b8, false,
                                 false, false);
            }
            if (box.rect.h >= 60) PaintVideoControls(canvas, box, player, dx, dy);
            if (!player->IsPlaying() && box.rect.w >= 120 && box.rect.h >= 120) {
                PaintPausedBadge(canvas, box, dx, dy);
            }
        } else {
            canvas->FillRect(dx, dy, box.rect.w, box.rect.h, 0x000000);
            canvas->DrawText("无视频源", dx + 8, dy + 8, 13, 0x94a3b8, false,
                             false, false);
        }
    }

    for (const auto& run : box.runs) {
        if (run.rect.y + run.rect.h < view_top || run.rect.y > view_bottom) {
            continue;
        }
        int dx = viewport.x + run.rect.x;
        int dy =
            viewport.y + run.rect.y - (is_fixed ? 0 : scroll_y);
        if (!run.widget.empty()) {
            DrawWidget(canvas, run, dx, dy);
            continue;
        }
        if (run.image) {
            if (!run.image->bgra.empty() && run.rect.w > 0 && run.rect.h > 0) {
                canvas->DrawImage(run.image->bgra.data(), run.image->width,
                                  run.image->height, dx, dy, run.rect.w,
                                  run.rect.h);
            }
            continue;
        }
        if (run.image_missing) {
            canvas->FillRect(dx, dy, run.rect.w, run.rect.h, 0xe2e8f0);
            canvas->StrokeRect(dx, dy, run.rect.w, run.rect.h, 0x94a3b8);
            std::string alt = run.alt_text.empty() ? "图片加载失败" : run.alt_text;
            int fs = std::max(10, std::min(13, run.rect.h / 4));
            int max_chars = std::max(1, run.rect.w / std::max(6, fs));
            if (alt.size() > (size_t)max_chars) {
                alt = alt.substr(0, (size_t)max_chars) + "...";
            }
            canvas->DrawText(alt, dx + 4, dy + 4, fs, 0x64748b, false, false,
                             false);
            continue;
        }
        Color c = ColorFromCss(run.color);
        if (c.transparent) c = {0, 0, 0};
        canvas->DrawText(run.text, dx, dy, run.font_size,
                         c.rgb(), run.bold, run.italic, run.underline);
    }
        // 先画普通子节点，再画定位子节点：定位元素浮在普通内容之上。
    for (const auto& c : box.children) {
        if (c->style.positioned || c->fixed) continue;
        PaintBox(*c, canvas, viewport, scroll_y, media, images, base_url,
                 is_fixed);
    }
    // positioned/fixed 子节点按 z-index 稳定排序（同值保持 DOM 顺序）。
    std::vector<const Box*> pos_children;
    for (const auto& c : box.children) {
        if (c->style.positioned || c->fixed) pos_children.push_back(c.get());
    }
    std::stable_sort(pos_children.begin(), pos_children.end(),
                     [](const Box* a, const Box* b) {
                         return a->style.has_z_index && b->style.has_z_index
                                    ? a->style.z_index < b->style.z_index
                                    : false;
                     });
    for (const Box* c : pos_children) {
        PaintBox(*c, canvas, viewport, scroll_y, media, images, base_url,
                 is_fixed);
    }
}

void CollectLinks(const Box& box, std::vector<LinkArea>& out,
                  bool fixed_ctx = false) {
    bool is_fixed = fixed_ctx || box.fixed;
    for (const auto& r : box.runs) {
        if (r.link && !r.href.empty()) {
            out.push_back({r.rect, r.href, is_fixed});
        }
    }
    for (const auto& c : box.children) CollectLinks(*c, out, is_fixed);
}

}  // namespace

std::unique_ptr<Box> Layout::Build(Canvas* measurer) {
    // vh / vw / calc() 折算需要知道视口尺寸，compute style 之前先写入。
    CssViewportWidth() = viewport_w_;
    CssViewportHeight() = viewport_h_ > 0 ? viewport_h_ : 800;
    auto root = std::make_unique<Box>();
    root->node = const_cast<Node*>(root_);
    root->rules = &rules_;
    root->style = ComputeStyle(root_, rules_, Style{});
    root->style.display = "block";
    root->style.background = "#ffffff";
    root->rect = {0, 0, viewport_w_, 0};
    root->content = {0, 0, viewport_w_, 0};
    PopulateBoxes(*root, rules_);
    LayoutBox(*root, measurer);
    root->scroll_height = root->content.h;
    Rect viewport{0, 0, viewport_w_, viewport_h_};
    ApplyPositioning(*root, viewport);
    return root;
}

void Page::ParseHtml(const std::string& html, const std::string& url) {
    data_.html = html;
    data_.url = url;
    data_.final_url = url;
    data_.error.clear();
    data_.ready = true;
    root_ = ::zb::ParseHtml(html);
    rules_ = DefaultRules();
    int index = (int)rules_.size();
    CollectStyleRules(root_.get(), rules_, &index);
    data_.title = "";
    const Node* title = FindFirstElement(root_.get(), "title");
    if (title) data_.title = Trim(TextOf(title));
    if (data_.title.empty()) data_.title = url.empty() ? "页面" : url;
    BuildMediaPlayers();
}

void Page::Relayout(int viewport_w, int viewport_h, Canvas* measurer) {
    if (!root_) return;
    viewport_w_ = viewport_w;
    viewport_h_ = viewport_h;
    Layout layout(root_.get(), rules_, viewport_w_, viewport_h_);
    root_box_ = layout.Build(measurer);
    if (scroll_y_ > ContentHeight() - 200) scroll_y_ = std::max(0, ContentHeight() - 200);
    if (scroll_y_ < 0) scroll_y_ = 0;
}

void Page::Paint(Canvas* canvas, const Rect& viewport, int scroll_y) const {
    if (!root_box_) {
        canvas->FillRect(viewport.x, viewport.y, viewport.w, viewport.h, 0xffffff);
        return;
    }
    canvas->Clip(viewport);
    canvas->FillRect(viewport.x, viewport.y, viewport.w, viewport.h, 0xf8fafc);
    std::string base = data_.final_url.empty() ? data_.url : data_.final_url;
    PaintBox(*root_box_, canvas, viewport, scroll_y, media_, images_, base);
    canvas->ResetClip();
}

void Page::CollectLinks(std::vector<LinkArea>& out) const {
    out.clear();
    if (root_box_) ::zb::CollectLinks(*root_box_, out);
}

void Page::SetImages(
    const std::map<std::string, std::shared_ptr<Image>>& images) {
    images_ = images;
    AttachImages(images_);
}

void Page::AttachImages(
    const std::map<std::string, std::shared_ptr<Image>>& images) {
    if (!root_) return;
    std::string base = data_.final_url.empty() ? data_.url : data_.final_url;
    std::vector<Node*> stack;
    stack.push_back(root_.get());
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (n->type == NodeType::Element && n->tag == "img") {
            std::string src = n->Attr("src");
            if (!src.empty()) {
                std::string absolute = ResolveUrl(base, src);
                auto it = images.find(absolute);
                if (it != images.end()) n->image = it->second;
            }
        }
        for (const auto& c : n->children) stack.push_back(c.get());
    }
}

void Page::BuildMediaPlayers() {
    media_.clear();
    if (!root_) return;
    std::vector<Node*> videos;
    CollectVideoNodes(root_.get(), videos);
    std::string base = data_.final_url.empty() ? data_.url : data_.final_url;
    for (Node* video : videos) {
        std::string src = video->Attr("src");
        if (src.empty()) {
            for (const auto& c : video->children) {
                if (c->type == NodeType::Element && c->tag == "source") {
                    src = c->Attr("src");
                    if (!src.empty()) break;
                }
            }
        }
        if (src.empty()) continue;
        std::string absolute = ResolveUrl(base, src);
        auto player = std::make_unique<MediaPlayer>();
        player->Open(absolute, video->HasAttr("autoplay"), video->HasAttr("loop"),
                     video->HasAttr("muted"));
        media_[video] = std::move(player);
    }
}

bool Page::UpdateMedia() {
    bool any = false;
    for (auto& kv : media_) {
        if (kv.second && kv.second->Update()) any = true;
    }
    return any;
}

bool Page::MediaClick(int x, int y, int fixed_x, int fixed_y) {
    // 先找普通（滚动文档坐标）视频，再找 fixed（视口局部坐标）视频。
    const Box* box = FindVideoBox(root_box_.get(), x, y, false, false);
    int use_x = x, use_y = y;
    if (!box && fixed_x > -2147483647) {
        box = FindVideoBox(root_box_.get(), fixed_x, fixed_y, true, false);
        use_x = fixed_x;
        use_y = fixed_y;
    }
    if (!box || !box->node) return false;
    auto it = media_.find(box->node);
    if (it == media_.end() || !it->second) return false;
    MediaPlayer* player = it->second.get();

    const int bar_h = 34;
    bool on_controls = use_y >= box->rect.y + box->rect.h - bar_h;
    if (on_controls) {
        int left = box->rect.x + 44;
        int right = box->rect.x + box->rect.w - 44;
        if (use_x < box->rect.x + 44) {
            player->TogglePlay();
        } else if (use_x < right && right > left) {
            double ratio = (double)(use_x - left) / (double)(right - left);
            player->Seek(ratio * player->Duration());
        } else {
            player->SetMuted(!player->Muted());
        }
    } else {
        player->TogglePlay();
    }
    return true;
}

void Page::SetScroll(int y) {
    int max_y = std::max(0, ContentHeight() - 0);
    (void)max_y;
    scroll_y_ = std::max(0, y);
}

std::string BuiltinHtml(const std::string& key) {
    if (key == "home" || key.empty() || key == "browser://home") {
        return kHomeHtml;
    }
    if (key == "parser" || key == "browser://parser") return kParserHtml;
    if (key == "css" || key == "browser://css") return kCssHtml;
    if (key == "about" || key == "browser://about" ||
        key == "about:system") {
        return kAboutHtml;
    }
    return kHomeHtml;
}

}  // namespace zb