#pragma once

#include "common.h"
#include "dom.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace zb {

struct Length {
    bool is_auto = true;
    bool percent = false;
    float value = 0.f;
};

// margin / padding 的默认值必须是 0，而不是 auto。
// Length 默认 is_auto=true 是给 width / height / left / top 这类“可为 auto”的
// 属性用的；如果 margin 也用这个默认值，那么每个块的左右 margin 都会被当成
// auto，于是所有固定宽度的块（video、img 等）都会被错误地水平居中。
inline Length ZeroLength() {
    Length l;
    l.is_auto = false;
    l.percent = false;
    l.value = 0.f;
    return l;
}

struct Style {
    std::string display = "block";
    std::string color = "#111827";
    std::string background = "";
    // background-image 的原始 url(...) 内容；PaintBox 用它查已解码图片。
    std::string background_image;
    std::string background_size = "auto";
    std::string border_color = "#d6dbe4";
    std::string border_style = "none";
    Length width;
    Length height;
    Length max_width;
    Length margin[4];
    Length padding[4];
    Length left;
    Length right;
    Length top;
    Length bottom;
    std::string position = "static";
    bool positioned = false;
    int z_index = 0;
    bool has_z_index = false;
    int border_width = 0;
    int border_radius = 0;
    int font_size = 16;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    // 字体族（逗号分隔的候选列表，绘制时取第一个可用字体）。
    std::string font_family = "Segoe UI";
    // 数值字重：400 normal / 700 bold，Chromium 用 300~900。
    int font_weight = 400;
    std::string text_align = "left";
    // 未显式设置 line-height 时置 0，表示按字体的真实度量取行高（等价 normal）。
    float line_height = 0.f;
    std::string text_transform = "none";
    float letter_spacing = 0.f;
    std::string flex_direction = "row";
    std::string justify_content = "flex-start";
    std::string align_items = "stretch";
    std::string flex_wrap = "nowrap";
    std::string grid_template_columns;  // 原始 grid-template-columns 值
    std::string grid_column;            // 原始 grid-column 值
    std::string overflow = "visible";
    std::string white_space = "normal";
    int gap = 0;
    bool box_border_box = true;
    float opacity = 1.f;
    // box-shadow：首个阴影（Chromium 支持多个，这里取第一个）
    bool has_shadow = false;
    int shadow_x = 0;
    int shadow_y = 0;
    int shadow_blur = 0;
    int shadow_spread = 0;
    std::string shadow_color = "rgba(0,0,0,0.2)";
    // background
    std::string background_repeat = "repeat";
    std::string background_position = "0% 0%";
    Length background_position_x;
    Length background_position_y;
    bool has_background_position = false;
    // ::before / ::after 生成内容（只保留图标/标签最常用的属性，避免递归结构）
    struct Pseudo {
        bool present = false;
        bool has_content = false;
        std::string content;
        std::string color;
        int font_size = 0;    // 0 = 继承
        int font_weight = 0;  // 0 = 继承
        std::string display;
        Length width;
        Length height;
        Length margin[4];
        Length padding[4];
        std::string background;
        int border_radius = 0;
        std::string text_align;
    };
    Pseudo before;
    Pseudo after;

    int PaddingTop() const { return Resolve(padding[0], 0, 0); }
    int PaddingRight() const { return Resolve(padding[1], 0, 0); }
    int PaddingBottom() const { return Resolve(padding[2], 0, 0); }
    int PaddingLeft() const { return Resolve(padding[3], 0, 0); }
    int MarginTop() const { return Resolve(margin[0], 0, 0); }
    int MarginRight() const { return Resolve(margin[1], 0, 0); }
    int MarginBottom() const { return Resolve(margin[2], 0, 0); }
    int MarginLeft() const { return Resolve(margin[3], 0, 0); }

    int Resolve(const Length& l, int parent_w, int fallback) const {
        if (l.is_auto) return fallback;
        if (l.percent) return (int)(l.value * parent_w / 100.f);
        return (int)l.value;
    }
};

struct AttrTest {
    std::string name;
    std::string op;  // ""（仅存在）、"="、"^="、"$="、"*="、"~="、"|="
    std::string value;
};

struct SelectorPart {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    std::vector<AttrTest> attrs;
    std::vector<std::string> pseudos;
};

struct CssRule {
    std::vector<std::vector<SelectorPart>> parts;
    std::vector<std::pair<std::string, std::string>> declarations;
    int index = 0;
};

// 结构伪类求值。状态伪类（:hover/:focus/...）本引擎没有交互状态，
// 一律判为不匹配——否则 `input:focus{border:blue}` 这类规则会永远生效。
inline bool PseudoMatches(const Node* node, const std::string& raw) {
    std::string name = raw;
    std::string arg;
    size_t lp = raw.find('(');
    if (lp != std::string::npos) {
        name = raw.substr(0, lp);
        size_t rp = raw.rfind(')');
        arg = Lower(Trim(raw.substr(
            lp + 1, rp == std::string::npos ? std::string::npos : rp - lp - 1)));
    }
    name = Lower(Trim(name));

    static const char* kState[] = {
        "hover",   "focus",        "active",      "visited",
        "link",    "checked",      "disabled",    "enabled",
        "target",  "focus-within", "focus-visible", "placeholder-shown",
        "before",  "after",        "first-line",  "first-letter",
        "selection", "default",    "valid",       "invalid",
        "required", "optional",    "read-only",   "read-write",
        "indeterminate", "fullscreen", "any-link", "autofill"};
    for (const char* s : kState) {
        if (name == s) return false;
    }
    if (name == "not") return true;  // 无法安全求值：不限制，避免整条规则失效
    if (name == "root") {
        return node->parent == nullptr || node->parent->type == NodeType::Document;
    }
    if (name == "empty") {
        for (const auto& c : node->children) {
            if (c->type == NodeType::Element) return false;
            if (c->type == NodeType::Text && !Trim(c->text).empty()) return false;
        }
        return true;
    }

    bool of_type = name.find("of-type") != std::string::npos;
    bool from_end = name.find("last") != std::string::npos;
    bool nth = name.find("nth") != std::string::npos;
    if (!(name == "first-child" || name == "last-child" ||
          name == "only-child" || name == "first-of-type" ||
          name == "last-of-type" || name == "only-of-type" || nth)) {
        return true;  // 未知伪类不限制
    }

    const Node* parent = node->parent;
    if (!parent) return false;
    std::vector<const Node*> sibs;
    for (const auto& c : parent->children) {
        if (c->type != NodeType::Element) continue;
        if (of_type && c->tag != node->tag) continue;
        sibs.push_back(c.get());
    }
    int n = (int)sibs.size();
    int idx = -1;
    for (int i = 0; i < n; ++i) {
        if (sibs[i] == node) idx = i;
    }
    if (idx < 0) return false;

    if (name == "only-child" || name == "only-of-type") return n == 1;
    if (name == "first-child" || name == "first-of-type") return idx == 0;
    if (name == "last-child" || name == "last-of-type") return idx == n - 1;

    // nth-child / nth-last-child / nth-of-type / nth-last-of-type
    int pos = from_end ? (n - idx) : (idx + 1);
    if (arg.empty()) return false;
    if (arg == "odd") return pos % 2 == 1;
    if (arg == "even") return pos % 2 == 0;
    size_t np = arg.find('n');
    if (np == std::string::npos) return std::atoi(arg.c_str()) == pos;
    int a = 1;
    std::string as = Trim(arg.substr(0, np));
    if (as == "-") a = -1;
    else if (!as.empty()) a = std::atoi(as.c_str());
    int b = 0;
    std::string bs = Trim(arg.substr(np + 1));
    if (!bs.empty()) b = std::atoi(bs.c_str());
    if (a == 0) return pos == b;
    int diff = pos - b;
    return diff % a == 0 && diff / a >= 0;
}

inline bool PartMatches(const Node* node, const SelectorPart& p) {
    if (!node || node->type != NodeType::Element) return false;
    if (!p.tag.empty() && node->tag != p.tag) return false;
    if (!p.id.empty() && node->Id() != p.id) return false;
    if (!p.classes.empty()) {
        auto classes = SplitStr(node->ClassList(), ' ');
        for (const auto& want : p.classes) {
            bool found = false;
            for (const auto& got : classes) {
                if (got == want) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
    }
    for (const auto& a : p.attrs) {
        if (!node->HasAttr(a.name)) return false;
        if (a.op.empty()) continue;
        std::string got = Lower(node->Attr(a.name));
        std::string want = Lower(a.value);
        if (a.op == "=") {
            if (got != want) return false;
        } else if (a.op == "^=") {
            if (!StartsWith(got, want)) return false;
        } else if (a.op == "$=") {
            if (!EndsWith(got, want)) return false;
        } else if (a.op == "*=") {
            if (got.find(want) == std::string::npos) return false;
        } else if (a.op == "~=") {
            bool found = false;
            for (const auto& s : SplitStr(got, ' ')) {
                if (s == want) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        } else if (a.op == "|=") {
            if (got != want && !StartsWith(got, want + "-")) return false;
        }
    }
    for (const auto& ps : p.pseudos) {
        if (!PseudoMatches(node, ps)) return false;
    }
    return true;
}

inline bool MatchesRule(const Node* node, const CssRule& rule) {
    if (!node || node->type != NodeType::Element) return false;
    for (const auto& chain : rule.parts) {
        if (chain.empty()) continue;
        if (!PartMatches(node, chain.back())) continue;
        if (chain.size() == 1) return true;
        const Node* ancestor = node->parent;
        int i = (int)chain.size() - 2;
        while (ancestor && i >= 0) {
            if (PartMatches(ancestor, chain[i])) i--;
            ancestor = ancestor->parent;
        }
        if (i < 0) return true;
    }
    return false;
}

inline Style DefaultStyle() {
    Style s;
    for (int i = 0; i < 4; ++i) {
        s.margin[i] = ZeroLength();
        s.padding[i] = ZeroLength();
    }
    s.width = {};
    s.height = {};
    s.max_width = {};
    s.left = {};
    s.right = {};
    s.top = {};
    s.bottom = {};
    return s;
}

// 视口尺寸，供 vh / vw / calc 折算。由引擎在布局前写入。
inline int& CssViewportWidth() {
    static int w = 1280;
    return w;
}
inline int& CssViewportHeight() {
    static int h = 800;
    return h;
}

// 解析一个长度分量。unit: 0=px 1=% 2=rem 3=em 4=vh 5=vw。
inline bool ParseLengthToken(const std::string& raw, float* out, int* unit) {
    std::string v = Trim(Lower(raw));
    if (v.empty()) return false;
    size_t i = 0;
    while (i < v.size() &&
           (std::isdigit((unsigned char)v[i]) || v[i] == '.' || v[i] == '-' ||
            v[i] == '+' || v[i] == ' ')) {
        i++;
    }
    std::string num = Trim(v.substr(0, i));
    if (num.empty()) return false;
    *out = (float)std::atof(num.c_str());
    std::string u = Trim(v.substr(i));
    if (u.empty() || u == "px") { *unit = 0; return true; }
    if (u == "%") { *unit = 1; return true; }
    if (u == "rem") { *unit = 2; return true; }
    if (u == "em") { *unit = 3; return true; }
    if (u == "vh") { *unit = 4; return true; }
    if (u == "vw") { *unit = 5; return true; }
    if (u == "pt") { *out *= 96.f / 72.f; *unit = 0; return true; }
    if (u == "pc") { *out *= 16.f; *unit = 0; return true; }
    if (u == "in") { *out *= 96.f; *unit = 0; return true; }
    if (u == "cm") { *out *= 37.795f; *unit = 0; return true; }
    if (u == "mm") { *out *= 3.7795f; *unit = 0; return true; }
    // ch / ex 按半个字号近似
    if (u == "ch" || u == "ex") { *out *= 8.f; *unit = 0; return true; }
    return false;
}

// calc() 求值：支持 length 之间的 + -，以及 <数字> * <length>。
// 求不出来返回 false（调用方按 auto 处理，绝不能退化成 0）。
inline bool EvalCalcTerm(const std::string& raw, float* px, float* pct) {
    std::string t = Trim(raw);
    if (t.empty()) return false;
    size_t star = t.find('*');
    if (star != std::string::npos) {
        std::string l = Trim(t.substr(0, star));
        std::string r = Trim(t.substr(star + 1));
        float ln = 0, rn = 0;
        int lu = 0, ru = 0;
        bool lok = ParseLengthToken(l, &ln, &lu);
        bool rok = ParseLengthToken(r, &rn, &ru);
        if (!lok || !rok) return false;
        if (lu != 0 && ru != 0) return false;  // 长度×长度没有意义
        float scale = (lu == 0) ? ln : rn;     // 用无单位的那一侧做倍数
        float val = (lu == 0) ? rn : ln;
        int unit = (lu == 0) ? ru : lu;
        switch (unit) {
            case 1: *pct += scale * val; break;
            case 2:
            case 3: *px += scale * val * 16.f; break;
            case 4: *px += scale * val * CssViewportHeight() / 100.f; break;
            case 5: *px += scale * val * CssViewportWidth() / 100.f; break;
            default: *px += scale * val; break;
        }
        return true;
    }
    float n = 0;
    int unit = 0;
    if (!ParseLengthToken(t, &n, &unit)) return false;
    switch (unit) {
        case 1: *pct += n; break;
        case 2:
        case 3: *px += n * 16.f; break;
        case 4: *px += n * CssViewportHeight() / 100.f; break;
        case 5: *px += n * CssViewportWidth() / 100.f; break;
        default: *px += n; break;
    }
    return true;
}

inline bool EvalCalc(const std::string& raw, float* px_out, float* pct_out) {
    std::string v = Trim(Lower(raw));
    if (!StartsWith(v, "calc(") || v.back() != ')') return false;
    std::string inner = v.substr(5, v.size() - 6);
    float px = 0;
    float pct = 0;
    int sign = 1;
    std::string term;
    for (size_t i = 0; i <= inner.size(); ++i) {
        bool at_end = (i == inner.size());
        char c = at_end ? '+' : inner[i];
        bool is_sign = (!at_end && (c == '+' || c == '-'));
        if (is_sign) {
            // 负号可能是数值的一部分（如 calc(-10px + 50%)）
            size_t j = i;
            bool prev_is_operand = false;
            while (j > 0) {
                char p = inner[j - 1];
                if (p == ' ' || p == '\t') { j--; continue; }
                prev_is_operand = !(p == '+' || p == '-' || p == '*' || p == '/');
                break;
            }
            if (!prev_is_operand) {
                term.push_back(c);
                continue;
            }
        }
        if (is_sign || at_end) {
            std::string t = Trim(term);
            if (t.empty()) {
                if (at_end) break;
                sign = (c == '-') ? -1 : 1;
                continue;
            }
            float tpx = 0;
            float tpct = 0;
            if (!EvalCalcTerm(t, &tpx, &tpct)) return false;
            px += sign * tpx;
            pct += sign * tpct;
            term.clear();
            sign = (c == '-') ? -1 : 1;
            if (at_end) break;
            continue;
        }
        term.push_back(c);
    }
    *px_out = px;
    *pct_out = pct;
    return true;
}

inline void SetLength(Length& out, const std::string& value, bool is_num = false) {
    std::string v = Trim(Lower(value));
    out = Length{};
    // 这些值一律按 auto 处理。绝不能落到 atof() 变成 0：
    // 真实站点大量使用 max-content / fit-content / calc()，一旦被当成 0，
    // 整个容器宽度就是 0，页面会“渲染成空白”。
    if (v.empty() || v == "auto" || v == "none" || v == "max-content" ||
        v == "min-content" || v == "fit-content" || v == "fill-available" ||
        v == "stretch" || v == "inherit" || v == "initial" || v == "unset" ||
        v == "revert" || v == "revert-layer") {
        return;
    }
    if (is_num) {
        out.is_auto = false;
        out.value = (float)std::atof(v.c_str());
        return;
    }
    if (StartsWith(v, "calc(")) {
        float px = 0;
        float pct = 0;
        if (!EvalCalc(v, &px, &pct)) return;
        out.is_auto = false;
        if (pct != 0.f && px == 0.f) {
            out.percent = true;
            out.value = pct;
        } else if (pct == 0.f) {
            out.value = px;
        } else {
            out.value = px + pct * CssViewportWidth() / 100.f;
        }
        return;
    }
    float num = 0;
    int unit = 0;
    if (!ParseLengthToken(v, &num, &unit)) return;  // 未知单位 → auto
    out.is_auto = false;
    switch (unit) {
        case 1: out.percent = true; out.value = num; break;
        case 2:
        case 3: out.value = num * 16.f; break;
        case 4: out.value = num * CssViewportHeight() / 100.f; break;
        case 5: out.value = num * CssViewportWidth() / 100.f; break;
        default: out.value = num; break;
    }
}

inline Color ColorFromCss(const std::string& value) {
    Color c;
    std::string v = Trim(value);
    if (v.empty() || v == "none" || v == "transparent") return c;
    std::string lv = Lower(v);
    if (StartsWith(lv, "rgb") || StartsWith(lv, "hsl")) {
        size_t open = v.find('(');
        size_t close = v.rfind(')');
        if (open == std::string::npos || close == std::string::npos ||
            close <= open) {
            return c;
        }
        std::string args = v.substr(open + 1, close - open - 1);
        // 兼容 rgb(0 0 0 / 50%) 这种空格写法：把空格与斜杠也当分隔符
        std::string norm = args;
        for (char& ch : norm) {
            if (ch == ' ' || ch == '\t' || ch == '/') ch = ',';
        }
        auto nums = SplitStr(norm, ',');
        auto clamp255 = [](int x) { return x < 0 ? 0 : (x > 255 ? 255 : x); };
        auto channel = [](const std::string& s) -> int {
            std::string t = Trim(s);
            if (!t.empty() && t.back() == '%') {
                return (int)(std::atof(t.c_str()) * 255.0 / 100.0);
            }
            return std::atoi(t.c_str());
        };
        auto alpha_of = [](const std::string& s) -> uint8_t {
            std::string t = Trim(s);
            if (t.empty()) return 255;
            float a = 1.f;
            if (t.back() == '%') a = (float)(std::atof(t.c_str()) / 100.0);
            else a = (float)std::atof(t.c_str());
            if (a < 0.f) a = 0.f;
            if (a > 1.f) a = 1.f;
            return (uint8_t)(a * 255.f + 0.5f);
        };
        if (StartsWith(lv, "hsl")) {
            if (nums.size() >= 3) {
                float h = (float)std::atof(Trim(nums[0]).c_str());
                float s = (float)std::atof(Trim(nums[1]).c_str()) / 100.f;
                float l = (float)std::atof(Trim(nums[2]).c_str()) / 100.f;
                if (!Trim(nums[1]).empty() && Trim(nums[1]).back() == '%') {
                    s = (float)std::atof(Trim(nums[1]).c_str()) / 100.f;
                }
                if (!Trim(nums[2]).empty() && Trim(nums[2]).back() == '%') {
                    l = (float)std::atof(Trim(nums[2]).c_str()) / 100.f;
                }
                h = std::fmod(h, 360.f);
                if (h < 0) h += 360.f;
                float cch = (1.f - std::fabs(2.f * l - 1.f)) * s;
                float hp = h / 60.f;
                float x = cch * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
                float rr = 0, gg = 0, bb = 0;
                if (hp < 1) { rr = cch; gg = x; }
                else if (hp < 2) { rr = x; gg = cch; }
                else if (hp < 3) { gg = cch; bb = x; }
                else if (hp < 4) { gg = x; bb = cch; }
                else if (hp < 5) { rr = x; bb = cch; }
                else { rr = cch; bb = x; }
                float m = l - cch / 2.f;
                c.r = (uint8_t)clamp255((int)((rr + m) * 255.f + 0.5f));
                c.g = (uint8_t)clamp255((int)((gg + m) * 255.f + 0.5f));
                c.b = (uint8_t)clamp255((int)((bb + m) * 255.f + 0.5f));
                c.a = nums.size() >= 4 ? alpha_of(nums[3]) : 255;
                c.transparent = false;
                return c;
            }
            return c;
        }
        if (nums.size() >= 3) {
            c.r = (uint8_t)clamp255(channel(nums[0]));
            c.g = (uint8_t)clamp255(channel(nums[1]));
            c.b = (uint8_t)clamp255(channel(nums[2]));
            c.a = nums.size() >= 4 ? alpha_of(nums[3]) : 255;
            c.transparent = false;
            return c;
        }
    }
    // #RRGGBBAA / #RGBA
    if (v[0] == '#' && (v.size() == 9 || v.size() == 5)) {
        auto nib = [&](size_t i) -> int {
            char x = v[i];
            if (x >= '0' && x <= '9') return x - '0';
            if (x >= 'a' && x <= 'f') return x - 'a' + 10;
            if (x >= 'A' && x <= 'F') return x - 'A' + 10;
            return 0;
        };
        if (v.size() == 9) {
            c.r = (uint8_t)(nib(1) * 16 + nib(2));
            c.g = (uint8_t)(nib(3) * 16 + nib(4));
            c.b = (uint8_t)(nib(5) * 16 + nib(6));
            c.a = (uint8_t)(nib(7) * 16 + nib(8));
        } else {
            c.r = (uint8_t)(nib(1) * 17);
            c.g = (uint8_t)(nib(2) * 17);
            c.b = (uint8_t)(nib(3) * 17);
            c.a = (uint8_t)(nib(4) * 17);
        }
        c.transparent = (c.a == 0);
        return c;
    }
    if (v[0] == '#' && v.size() >= 7) {
        auto hex = [&](int a, int b) -> int {
            int out = 0;
            for (int i = a; i < b; i++) {
                char x = v[i];
                out *= 16;
                if (x >= '0' && x <= '9') out += x - '0';
                else if (x >= 'a' && x <= 'f') out += x - 'a' + 10;
                else if (x >= 'A' && x <= 'F') out += x - 'A' + 10;
            }
            return out;
        };
        c.r = (uint8_t)hex(1, 3);
        c.g = (uint8_t)hex(3, 5);
        c.b = (uint8_t)hex(5, 7);
        c.transparent = false;
        return c;
    }
    if (v[0] == '#' && v.size() == 4) {
        int a = std::isdigit((unsigned char)v[1]) ? v[1] - '0' : v[1] - 'a' + 10;
        int b = std::isdigit((unsigned char)v[2]) ? v[2] - '0' : v[2] - 'a' + 10;
        int d = std::isdigit((unsigned char)v[3]) ? v[3] - '0' : v[3] - 'a' + 10;
        c.r = (uint8_t)(a * 17);
        c.g = (uint8_t)(b * 17);
        c.b = (uint8_t)(d * 17);
        c.transparent = false;
        return c;
    }
    struct Named { const char* n; uint8_t r, g, b; };
    static const Named names[] = {
        {"white", 255, 255, 255}, {"black", 0, 0, 0},
        {"red", 255, 0, 0}, {"green", 0, 128, 0}, {"blue", 0, 0, 255},
        {"gray", 128, 128, 128}, {"grey", 128, 128, 128},
        {"lightgray", 211, 211, 211}, {"lightgrey", 211, 211, 211},
        {"darkgray", 64, 64, 64}, {"darkgrey", 64, 64, 64},
        {"orange", 255, 165, 0}, {"purple", 128, 0, 128},
        {"yellow", 255, 255, 0}, {"pink", 255, 192, 203},
        {"silver", 192, 192, 192}, {"navy", 0, 0, 128},
        {"teal", 0, 128, 128}, {"maroon", 128, 0, 0},
        {"olive", 128, 128, 0}, {"lime", 0, 255, 0},
        {"aqua", 0, 255, 255}, {"violet", 238, 130, 238},
        {"coral", 255, 127, 80}, {"tomato", 255, 99, 71},
        {"slategray", 112, 128, 144}, {"slateblue", 106, 90, 205},
        {"mediumseagreen", 60, 179, 113},
        {"whitesmoke", 245, 245, 245}, {"aliceblue", 240, 248, 255},
        {"ghostwhite", 248, 248, 255}, {"dimgray", 105, 105, 105},
        {"royalblue", 65, 105, 225}, {"steelblue", 70, 130, 180},
        {"indigo", 75, 0, 130}, {"crimson", 220, 20, 60},
        {"gold", 255, 215, 0}, {"sandybrown", 244, 164, 96},
        {"seagreen", 46, 139, 87}, {"honeydew", 240, 255, 240},
        {"peachpuff", 255, 218, 185}, {"mintcream", 245, 255, 250},
        {"lavender", 230, 230, 250}, {"thistle", 216, 191, 216},
        {"plum", 221, 160, 221}, {"orchid", 218, 112, 214},
        {"cadetblue", 95, 158, 160}, {"darkblue", 0, 0, 139},
        {"mediumblue", 0, 0, 205}, {"darkorange", 255, 140, 0},
        {"lightgreen", 144, 238, 144}, {"lightblue", 173, 216, 230},
        {"lightcyan", 224, 255, 255}, {"lightyellow", 255, 255, 224},
        {"lightpink", 255, 182, 193}, {"lightsalmon", 255, 160, 122},
        {"darkred", 139, 0, 0}, {"darkgreen", 0, 100, 0},
        {"darkmagenta", 139, 0, 139}, {"darkkhaki", 189, 183, 107},
        {"darkolivegreen", 85, 107, 47}, {"darkseagreen", 143, 188, 143},
        {"darkslateblue", 72, 61, 139}, {"darkslategray", 47, 79, 79},
    };
    v = Lower(v);
    for (const auto& named : names) {
        if (v == named.n) {
            c.r = named.r;
            c.g = named.g;
            c.b = named.b;
            c.transparent = false;
            return c;
        }
    }
    return c;
}

// --- CSS 自定义属性（CSS 变量）---------------------------------------------
// 真实站点（B 站、洛谷、GitHub…）大量用 var(--x) 定义颜色/间距/尺寸，
// 不支持它会导致颜色全部回退、间距丢失，版式与 Chromium 相差很远。
// 做法：解析样式表时把所有 `--name: value` 收进一张表，应用声明时替换 var()。
inline std::map<std::string, std::string>& CssVars() {
    static std::map<std::string, std::string> vars;
    return vars;
}

inline void CssVarsReset() { CssVars().clear(); }

// 替换 value 里的 var(--name[, fallback])，支持嵌套。
inline std::string SubstituteVars(const std::string& value) {
    if (value.find("var(") == std::string::npos) return value;
    std::string out;
    size_t i = 0;
    int guard = 0;
    while (i < value.size() && guard++ < 128) {
        size_t p = value.find("var(", i);
        if (p == std::string::npos) {
            out += value.substr(i);
            break;
        }
        out += value.substr(i, p - i);
        size_t open = p + 3;  // 指向 '('
        int depth = 0;
        size_t j = open;
        bool closed = false;
        for (; j < value.size(); ++j) {
            if (value[j] == '(') depth++;
            else if (value[j] == ')') {
                depth--;
                if (depth == 0) { closed = true; break; }
            }
        }
        if (!closed) break;
        std::string inner = value.substr(open + 1, j - open - 1);
        size_t comma = std::string::npos;
        int d2 = 0;
        for (size_t k = 0; k < inner.size(); ++k) {
            if (inner[k] == '(') d2++;
            else if (inner[k] == ')') d2--;
            else if (inner[k] == ',' && d2 == 0) { comma = k; break; }
        }
        std::string name =
            Trim(comma == std::string::npos ? inner : inner.substr(0, comma));
        std::string fallback =
            comma == std::string::npos ? std::string() : Trim(inner.substr(comma + 1));
        auto it = CssVars().find(name);
        std::string resolved = (it != CssVars().end()) ? it->second : fallback;
        out += SubstituteVars(resolved);
        i = j + 1;
    }
    return out;
}

inline void ApplyDeclaration(Style& s, const std::string& name_raw,
                             const std::string& value_raw) {
    std::string name = Lower(Trim(name_raw));
    std::string value = Trim(value_raw);
    // !important 只做“去标记”处理：本项目按文档顺序应用，不做完整优先级层叠。
    if (EndsWith(Lower(value), "!important")) {
        value = Trim(value.substr(0, value.size() - 10));
    }
    if (name.size() > 2 && name[0] == '-' && name[1] == '-') {
        CssVars()[name] = value;  // 收集 CSS 变量定义
        return;
    }
    value = SubstituteVars(value);
    if (name == "display") s.display = Lower(value);
    else if (name == "position") {
        std::string p = Lower(value);
        if (p == "static" || p == "relative" || p == "absolute" ||
            p == "fixed") {
            s.position = p;
            s.positioned = p != "static";
        }
    }
    else if (name == "z-index") {
        std::string v = Trim(value);
        s.z_index = 0;
        s.has_z_index = !v.empty() && Lower(v) != "auto";
        if (s.has_z_index) s.z_index = std::atoi(v.c_str());
    }
    else if (name == "color") s.color = value;
    else if (name == "background-color" || name == "background") {
        // background 简写里如果带 url(...)，同时记录背景图。
        s.background = value;
        size_t up = value.find("url(");
        if (up != std::string::npos) {
            size_t open = value.find('(', up);
            size_t close = value.find(')', open == std::string::npos ? up
                                                                     : open);
            if (open != std::string::npos && close != std::string::npos) {
                std::string u = value.substr(open + 1, close - open - 1);
                if (u.size() >= 2 &&
                    ((u.front() == '"' && u.back() == '"') ||
                     (u.front() == '\'' && u.back() == '\''))) {
                    u = u.substr(1, u.size() - 2);
                }
                s.background_image = u;
            }
        }
    }
    else if (name == "background-image") {
        s.background_image.clear();
        size_t up = value.find("url(");
        if (up != std::string::npos) {
            size_t open = value.find('(', up);
            size_t close = value.find(')', open == std::string::npos ? up
                                                                     : open);
            if (open != std::string::npos && close != std::string::npos) {
                std::string u = value.substr(open + 1, close - open - 1);
                if (u.size() >= 2 &&
                    ((u.front() == '"' && u.back() == '"') ||
                     (u.front() == '\'' && u.back() == '\''))) {
                    u = u.substr(1, u.size() - 2);
                }
                s.background_image = u;
            }
        }
    }
    else if (name == "background-size") s.background_size = Lower(value);
    else if (name == "border-color") s.border_color = value;
    else if (name == "border-radius") {
        s.border_radius = value.empty() ? 0 : std::max(0, (int)std::atof(value.c_str()));
    }
    else if (name == "border-width") {
        s.border_width = value.empty() ? 0 : std::max(0, (int)std::atof(value.c_str()));
        if (s.border_style == "none") s.border_style = "solid";
    }
    else if (name == "border") {
        auto parts = SplitStr(value, ' ');
        for (const auto& p : parts) {
            if (p == "solid" || p == "dashed" || p == "dotted") s.border_style = p;
            else if (EndsWith(p, "px") && std::isdigit((unsigned char)p[0])) {
                s.border_width = (int)std::atof(p.c_str());
            } else if (ColorFromCss(p).transparent == false || p == "transparent") {
                s.border_color = p;
            }
        }
        if (s.border_style == "none") s.border_style = "solid";
        if (s.border_width == 0) s.border_width = 1;
    }
    else if (name == "font-size") {
        if (EndsWith(value, "px")) s.font_size = (int)std::atof(value.c_str());
        else if (EndsWith(value, "%")) s.font_size = (int)(16 * std::atof(value.c_str()) / 100.0);
        else if (value == "small") s.font_size = 13;
        else if (value == "medium") s.font_size = 16;
        else if (value == "large") s.font_size = 20;
        else if (value == "x-large") s.font_size = 24;
        else if (value == "smaller") s.font_size = std::max(1, s.font_size - 2);
        else if (value == "larger") s.font_size = s.font_size + 2;
        else s.font_size = (int)std::atof(value.c_str()) ?: 16;
    }
    else if (name == "font-weight") {
        s.bold = value == "bold" || value == "bolder" ||
                 (!value.empty() && std::isdigit((unsigned char)value[0]) &&
                  std::atoi(value.c_str()) >= 600);
    }
    else if (name == "font-style") s.italic = value == "italic" || value == "oblique";
    else if (name == "text-decoration" || name == "text-decoration-line") {
        s.underline = value.find("underline") != std::string::npos;
    }
    else if (name == "text-align") s.text_align = Lower(value);
    else if (name == "line-height") {
        if (!value.empty() && std::isdigit((unsigned char)value[0])) {
            if (EndsWith(value, "px")) s.line_height = (float)std::atof(value.c_str()) / s.font_size;
            else s.line_height = (float)std::atof(value.c_str());
        }
    }
    else if (name == "width") SetLength(s.width, value);
    else if (name == "height") SetLength(s.height, value);
    else if (name == "max-width") SetLength(s.max_width, value);
    else if (name == "white-space") s.white_space = Lower(value);
    else if (name == "margin") {
        auto v = SplitStr(value, ' ');
        if (v.size() == 1) {
            SetLength(s.margin[0], v[0]); SetLength(s.margin[1], v[0]);
            SetLength(s.margin[2], v[0]); SetLength(s.margin[3], v[0]);
        } else if (v.size() == 2) {
            SetLength(s.margin[0], v[0]); SetLength(s.margin[2], v[0]);
            SetLength(s.margin[1], v[1]); SetLength(s.margin[3], v[1]);
        } else if (v.size() == 3) {
            SetLength(s.margin[0], v[0]); SetLength(s.margin[1], v[1]);
            SetLength(s.margin[2], v[2]); SetLength(s.margin[3], v[1]);
        } else if (v.size() >= 4) {
            SetLength(s.margin[0], v[0]); SetLength(s.margin[1], v[1]);
            SetLength(s.margin[2], v[2]); SetLength(s.margin[3], v[3]);
        }
    }
    else if (name == "margin-top") SetLength(s.margin[0], value);
    else if (name == "margin-right") SetLength(s.margin[1], value);
    else if (name == "margin-bottom") SetLength(s.margin[2], value);
    else if (name == "margin-left") SetLength(s.margin[3], value);
    else if (name == "padding") {
        auto v = SplitStr(value, ' ');
        if (v.size() == 1) {
            SetLength(s.padding[0], v[0]); SetLength(s.padding[1], v[0]);
            SetLength(s.padding[2], v[0]); SetLength(s.padding[3], v[0]);
        } else if (v.size() == 2) {
            SetLength(s.padding[0], v[0]); SetLength(s.padding[2], v[0]);
            SetLength(s.padding[1], v[1]); SetLength(s.padding[3], v[1]);
        } else if (v.size() == 3) {
            SetLength(s.padding[0], v[0]); SetLength(s.padding[1], v[1]);
            SetLength(s.padding[2], v[2]); SetLength(s.padding[3], v[1]);
        } else if (v.size() >= 4) {
            SetLength(s.padding[0], v[0]); SetLength(s.padding[1], v[1]);
            SetLength(s.padding[2], v[2]); SetLength(s.padding[3], v[3]);
        }
    }
    else if (name == "padding-top") SetLength(s.padding[0], value);
    else if (name == "padding-right") SetLength(s.padding[1], value);
    else if (name == "padding-bottom") SetLength(s.padding[2], value);
    else if (name == "padding-left") SetLength(s.padding[3], value);
    else if (name == "left") SetLength(s.left, value);
    else if (name == "right") SetLength(s.right, value);
    else if (name == "top") SetLength(s.top, value);
    else if (name == "bottom") SetLength(s.bottom, value);
    else if (name == "flex-direction") s.flex_direction = Lower(value);
    else if (name == "justify-content") s.justify_content = Lower(value);
    else if (name == "align-items") s.align_items = Lower(value);
    else if (name == "grid-template-columns") s.grid_template_columns = value;
    else if (name == "grid-template-rows") {
        // 网格行暂时不显式建 track；由子项高度撑起。
    }
    else if (name == "grid-column") s.grid_column = Lower(value);
    else if (name == "overflow") s.overflow = Lower(value);
    else if (name == "gap") {
        if (EndsWith(value, "px")) s.gap = (int)std::atof(value.c_str());
        else s.gap = (int)std::atof(value.c_str());
    }
    else if (name == "box-sizing") s.box_border_box = Lower(value) == "border-box";
    else if (name == "font-family") {
        // 取候选列表里的第一个族名（去引号）；绘制时按顺序尝试。
        std::string v = value;
        std::string first;
        int depth = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            char c = v[i];
            if (c == '(') depth++;
            else if (c == ')') { if (depth > 0) depth--; }
            else if (c == ',' && depth == 0) { first = v.substr(0, i); break; }
        }
        if (first.empty()) first = v;
        first = Trim(first);
        if (first.size() >= 2 && ((first.front() == '"' && first.back() == '"') ||
                                  (first.front() == '\'' && first.back() == '\''))) {
            first = first.substr(1, first.size() - 2);
        }
        if (!first.empty() && Lower(first) != "inherit") s.font_family = first;
    }
    else if (name == "font-weight") {
        std::string v = Lower(Trim(value));
        if (v == "bold" || v == "bolder") { s.font_weight = 700; s.bold = true; }
        else if (v == "normal" || v == "lighter") { s.font_weight = 400; s.bold = false; }
        else {
            int w = std::atoi(v.c_str());
            if (w >= 100 && w <= 1000) {
                s.font_weight = w;
                s.bold = w >= 600;  // 600 以上用粗体近似
            }
        }
    }
    else if (name == "font") {
        // 简写：至少识别其中的 font-size 与 font-weight，族名取最后一段
        auto parts = SplitStr(value, ' ');
        for (const auto& p : parts) {
            std::string t = Lower(Trim(p));
            if (t == "bold" || t == "bolder") { s.font_weight = 700; s.bold = true; }
            else if (EndsWith(t, "px")) {
                s.font_size = (int)std::atof(t.c_str());
            }
        }
        size_t sp = value.rfind(' ');
        if (sp != std::string::npos) {
            std::string fam = Trim(value.substr(sp + 1));
            if (!fam.empty()) ApplyDeclaration(s, "font-family", fam);
        }
    }
    else if (name == "letter-spacing") {
        if (EndsWith(value, "px")) s.letter_spacing = (float)std::atof(value.c_str());
    }
    else if (name == "text-transform") s.text_transform = Lower(value);
    else if (name == "opacity") {
        float o = (float)std::atof(value.c_str());
        s.opacity = o < 0.f ? 0.f : (o > 1.f ? 1.f : o);
    }
    else if (name == "box-shadow") {
        std::string v = Lower(value);
        if (v != "none" && !v.empty()) {
            // 只取第一个阴影：颜色 + 数值（x y blur spread）
            std::string color_part;
            size_t cp = value.find("rgb");
            if (cp == std::string::npos) cp = value.find('#');
            if (cp != std::string::npos) {
                size_t close = value.find(')', cp);
                color_part = (close == std::string::npos)
                                 ? value.substr(cp)
                                 : value.substr(cp, close - cp + 1);
            }
            std::string nums = value;
            if (!color_part.empty()) nums = value.substr(0, cp);
            std::vector<float> vals;
            std::string cur;
            for (size_t i = 0; i <= nums.size(); ++i) {
                char ch = i < nums.size() ? nums[i] : ' ';
                if (std::isdigit((unsigned char)ch) || ch == '-' || ch == '.') {
                    cur.push_back(ch);
                } else if (!cur.empty()) {
                    vals.push_back((float)std::atof(cur.c_str()));
                    cur.clear();
                }
            }
            if (vals.size() >= 2) {
                s.has_shadow = true;
                s.shadow_x = (int)vals[0];
                s.shadow_y = (int)vals[1];
                s.shadow_blur = vals.size() >= 3 ? (int)vals[2] : 0;
                s.shadow_spread = vals.size() >= 4 ? (int)vals[3] : 0;
                if (!color_part.empty()) s.shadow_color = color_part;
                else s.shadow_color = "rgba(0,0,0,0.18)";
            }
        }
    }
    else if (name == "background-repeat") s.background_repeat = Lower(value);
    else if (name == "background-position") {
        s.background_position = Lower(value);
        auto parts = SplitStr(value, ' ');
        auto setpos = [&](const std::string& tok, bool x_axis) {
            std::string t = Lower(Trim(tok));
            Length* dst = x_axis ? &s.background_position_x
                                 : &s.background_position_y;
            if (t == "center") { dst->is_auto = true; return; }
            if (t == "left" || t == "top") { SetLength(*dst, "0"); return; }
            if (t == "right" || t == "bottom") { SetLength(*dst, "100%"); return; }
            SetLength(*dst, t);
        };
        if (parts.size() == 1) { setpos(parts[0], true); setpos(parts[0], false); }
        else if (parts.size() >= 2) { setpos(parts[0], true); setpos(parts[1], false); }
        s.has_background_position = true;
    }
    else if (name == "flex-wrap") s.flex_wrap = Lower(value);
    else if (name == "text-overflow") { /* 由 overflow 近似处理，暂不单独实现 */ }
}

inline void ApplyStyleAttr(const Node* node, Style& style) {
    const std::string attr = node->Attr("style");
    if (attr.empty()) return;
    for (const auto& decl : SplitStr(attr, ';')) {
        auto p = decl.find(':');
        if (p == std::string::npos) continue;
        std::string name = decl.substr(0, p);
        std::string value = decl.substr(p + 1);
        ApplyDeclaration(style, name, value);
    }
}

inline Style ComputeStyleCss(const Node* node,
                             const std::vector<CssRule>& rules,
                             const Style& parent_style) {
    Style s = DefaultStyle();
    if (node && node->type == NodeType::Element) {
        s.color = parent_style.color;
        s.font_size = parent_style.font_size;
        s.bold = parent_style.bold;
        s.italic = parent_style.italic;
        s.underline = parent_style.underline;
        s.text_align = parent_style.text_align;
        s.line_height = parent_style.line_height;

        for (const auto& rule : rules) {
            if (MatchesRule(node, rule)) {
                for (const auto& decl : rule.declarations) {
                    ApplyDeclaration(s, decl.first, decl.second);
                }
            }
        }
        ApplyStyleAttr(node, s);
    }
    return s;
}

}  // namespace zb