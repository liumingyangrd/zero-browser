#pragma once

#include "common.h"
#include "dom.h"

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
    std::string text_align = "left";
    float line_height = 1.45f;
    std::string flex_direction = "row";
    std::string justify_content = "flex-start";
    std::string align_items = "stretch";
    std::string grid_template_columns;  // 原始 grid-template-columns 值
    std::string grid_column;            // 原始 grid-column 值
    std::string overflow = "visible";
    std::string white_space = "normal";
    int gap = 0;
    bool box_border_box = true;

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
    if (StartsWith(Lower(v), "rgb")) {
        size_t open = v.find('(');
        size_t close = v.rfind(')');
        if (open != std::string::npos && close != std::string::npos &&
            close > open) {
            auto nums = SplitStr(v.substr(open + 1, close - open - 1), ',');
            if (nums.size() >= 3) {
                auto clamp255 = [](int x) { return x < 0 ? 0 : (x > 255 ? 255 : x); };
                c.r = (uint8_t)clamp255(std::atoi(nums[0].c_str()));
                c.g = (uint8_t)clamp255(std::atoi(nums[1].c_str()));
                c.b = (uint8_t)clamp255(std::atoi(nums[2].c_str()));
                c.transparent = false;
                return c;
            }
        }
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

inline void ApplyDeclaration(Style& s, const std::string& name_raw,
                             const std::string& value_raw) {
    std::string name = Lower(Trim(name_raw));
    std::string value = Trim(value_raw);
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