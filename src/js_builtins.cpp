// 内置方法表：按接收者类型分派（代替原型链）。
//
// 约定：所有方法都以 self（this）为操作对象，因此
//   "abc".toUpperCase()            走 String 表
//   [1,2,3].map(fn)                走 Array 表
//   Array.prototype.slice.call(x)  也能用 —— 因为 slice 读的是 this
// 数组方法同时接受"类数组"（有 length 与数字下标），所以 arguments 这类对象
// 也能直接喂进去。

#include "js.h"

#include "common.h"
#include "js_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zb {

namespace {
using Args = std::vector<JsValue>;

JsValue Arg(const Args& a, size_t i) {
    return i < a.size() ? a[i] : JsValue::Undef();
}

// 字符串下标吸附到码点边界：按字节切片会把汉字切成半个（非法 UTF-8），
// 这类内容一旦写回 DOM 就会在绘制时出乱码。
size_t SnapDown(const std::string& s, long long i) {
    if (i <= 0) return 0;
    if ((size_t)i >= s.size()) return s.size();
    size_t k = (size_t)i;
    while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) --k;
    return k;
}

long long ToInt(Interp& it, const JsValue& v) {
    double d = it.ToNumber(v);
    if (std::isnan(d)) return 0;
    return (long long)d;
}

std::string SelfStr(Interp& it, const JsValue& self) {
    return it.ToString(self);
}

// 数组元素的统一读写：真数组与类数组都走 JsGetIndex/JsSetIndex。
void SetLength(Interp& it, const JsValue& self, double n) {
    if (self.obj && self.obj->is_array) {
        self.obj->length = n;
        return;
    }
    JsSetProp(it, self, "length", JsValue::Num(n));
}

double TypedInt(Interp& it, const JsValue& self) {
    return JsLengthOf(it, self);
}

// ------------------------------------------------------------- 字符串方法
const JsMethodDef kStringMethods[] = {
    {"charAt",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long i = ToInt(it, Arg(a, 0));
         if (i < 0 || (size_t)i >= s.size()) {
             *out = JsValue::Str("");
             return true;
         }
         *out = JsValue::Str(s.substr((size_t)i, 1));
         return true;
     }},
    {"charCodeAt",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long i = ToInt(it, Arg(a, 0));
         if (i < 0 || (size_t)i >= s.size()) {
             *out = JsValue::Num(NAN);
             return true;
         }
         *out = JsValue::Num((double)(unsigned char)s[(size_t)i]);
         return true;
     }},
    {"indexOf",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::string sub = it.ToString(Arg(a, 0));
         long long from = a.size() > 1 ? ToInt(it, a[1]) : 0;
         if (from < 0) from = 0;
         size_t p = (size_t)from <= s.size() ? s.find(sub, (size_t)from)
                                             : std::string::npos;
         *out = JsValue::Num(p == std::string::npos ? -1 : (double)p);
         return true;
     }},
    {"lastIndexOf",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::string sub = it.ToString(Arg(a, 0));
         size_t p = s.rfind(sub);
         *out = JsValue::Num(p == std::string::npos ? -1 : (double)p);
         return true;
     }},
    {"includes",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         *out = JsValue::Bool(s.find(it.ToString(Arg(a, 0))) != std::string::npos);
         return true;
     }},
    {"startsWith",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::string sub = it.ToString(Arg(a, 0));
         *out = JsValue::Bool(s.size() >= sub.size() &&
                              s.compare(0, sub.size(), sub) == 0);
         return true;
     }},
    {"endsWith",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::string sub = it.ToString(Arg(a, 0));
         *out = JsValue::Bool(s.size() >= sub.size() &&
                              s.compare(s.size() - sub.size(), sub.size(), sub) == 0);
         return true;
     }},
    {"slice",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long n = (long long)s.size();
         long long b = a.empty() || Arg(a, 0).IsNullish() ? 0 : ToInt(it, a[0]);
         long long e = a.size() < 2 || Arg(a, 1).IsNullish() ? n : ToInt(it, a[1]);
         if (b < 0) b += n;
         if (e < 0) e += n;
         b = std::max(0LL, std::min(b, n));
         e = std::max(0LL, std::min(e, n));
         if (e < b) e = b;
         *out = JsValue::Str(s.substr(SnapDown(s, b), SnapDown(s, e) - SnapDown(s, b)));
         return true;
     }},
    {"substring",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long n = (long long)s.size();
         long long b = a.empty() ? 0 : ToInt(it, a[0]);
         long long e = a.size() < 2 ? n : ToInt(it, a[1]);
         b = std::max(0LL, std::min(b, n));
         e = a.size() < 2 ? n : std::max(0LL, std::min(e, n));
         if (b > e) std::swap(b, e);
         *out = JsValue::Str(s.substr(SnapDown(s, b), SnapDown(s, e) - SnapDown(s, b)));
         return true;
     }},
    {"substr",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long n = (long long)s.size();
         long long b = a.empty() ? 0 : ToInt(it, a[0]);
         if (b < 0) b = std::max(0LL, n + b);
         long long len = a.size() < 2 ? n - b : ToInt(it, a[1]);
         if (len < 0) len = 0;
         b = std::max(0LL, std::min(b, n));
         long long e = std::max(b, std::min(b + len, n));
         *out = JsValue::Str(s.substr(SnapDown(s, b), SnapDown(s, e) - SnapDown(s, b)));
         return true;
     }},
    {"toUpperCase",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         for (char& c : s) {
             if (c >= 'a' && c <= 'z') c = (char)(c - 32);
         }
         *out = JsValue::Str(s);
         return true;
     }},
    {"toLowerCase",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         for (char& c : s) {
             if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
         }
         *out = JsValue::Str(s);
         return true;
     }},
    {"trim",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         *out = JsValue::Str(Trim(SelfStr(it, self)));
         return true;
     }},
    {"trimStart",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         size_t i = 0;
         while (i < s.size() && isspace((unsigned char)s[i])) ++i;
         *out = JsValue::Str(s.substr(i));
         return true;
     }},
    {"trimEnd",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         size_t e = s.size();
         while (e > 0 && isspace((unsigned char)s[e - 1])) --e;
         *out = JsValue::Str(s.substr(0, e));
         return true;
     }},
    {"split",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::vector<JsValue> parts;
         if (a.empty() || Arg(a, 0).type == JsType::Undefined) {
             parts.push_back(JsValue::Str(s));
         } else {
             std::string sep = it.ToString(a[0]);
             if (sep.empty()) {
                 size_t i = 0;
                 while (i < s.size()) {
                     unsigned char c = (unsigned char)s[i];
                     size_t len = 1;
                     if (c >= 0xF0) len = 4;
                     else if (c >= 0xE0) len = 3;
                     else if (c >= 0xC0) len = 2;
                     if (i + len > s.size()) len = 1;
                     parts.push_back(JsValue::Str(s.substr(i, len)));
                     i += len;
                 }
             } else {
                 size_t start = 0;
                 for (;;) {
                     size_t p = s.find(sep, start);
                     if (p == std::string::npos) {
                         parts.push_back(JsValue::Str(s.substr(start)));
                         break;
                     }
                     parts.push_back(JsValue::Str(s.substr(start, p - start)));
                     start = p + sep.size();
                 }
             }
         }
         if (a.size() > 1) {
             long long lim = ToInt(it, a[1]);
             if (lim >= 0 && (size_t)lim < parts.size()) {
                 parts.resize((size_t)lim);
             }
         }
         *out = it.NewArray(parts);
         return true;
     }},
    {"replace",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::string pat = it.ToString(Arg(a, 0));
         size_t p = s.find(pat);
         if (p == std::string::npos) {
             *out = JsValue::Str(s);
             return true;
         }
         std::string repl;
         if (it.IsCallable(Arg(a, 1))) {
             std::vector<JsValue> ca;
             ca.push_back(JsValue::Str(pat));
             ca.push_back(JsValue::Num((double)p));
             ca.push_back(JsValue::Str(s));
             JsValue r;
             if (it.CallFunction(a[1], JsValue::Undef(), ca, &r)) repl = it.ToString(r);
         } else {
             repl = it.ToString(Arg(a, 1));
         }
         *out = JsValue::Str(s.substr(0, p) + repl + s.substr(p + pat.size()));
         return true;
     }},
    {"replaceAll",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         std::string pat = it.ToString(Arg(a, 0));
         std::string repl = it.ToString(Arg(a, 1));
         if (pat.empty()) {
             *out = JsValue::Str(s);
             return true;
         }
         std::string r;
         size_t start = 0;
         for (;;) {
             size_t p = s.find(pat, start);
             if (p == std::string::npos) {
                 r += s.substr(start);
                 break;
             }
             r += s.substr(start, p - start) + repl;
             start = p + pat.size();
         }
         *out = JsValue::Str(r);
         return true;
     }},
    {"concat",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         for (const JsValue& v : a) s += it.ToString(v);
         *out = JsValue::Str(s);
         return true;
     }},
    {"repeat",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long n = ToInt(it, Arg(a, 0));
         if (n < 0) n = 0;
         if (n > 4096) n = 4096;  // 防止 s.repeat(1e9) 直接把内存打爆
         std::string r;
         for (long long i = 0; i < n; ++i) r += s;
         *out = JsValue::Str(r);
         return true;
     }},
    {"padStart",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long want = ToInt(it, Arg(a, 0));
         std::string pad = a.size() > 1 ? it.ToString(a[1]) : " ";
         if (pad.empty()) pad = " ";
         while ((long long)s.size() < want) s = pad.substr(0, 1) + s;
         *out = JsValue::Str(s);
         return true;
     }},
    {"padEnd",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string s = SelfStr(it, self);
         long long want = ToInt(it, Arg(a, 0));
         std::string pad = a.size() > 1 ? it.ToString(a[1]) : " ";
         if (pad.empty()) pad = " ";
         while ((long long)s.size() < want) s += pad.substr(0, 1);
         *out = JsValue::Str(s);
         return true;
     }},
    {"localeCompare",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         int c = SelfStr(it, self).compare(it.ToString(Arg(a, 0)));
         *out = JsValue::Num(c < 0 ? -1 : (c > 0 ? 1 : 0));
         return true;
     }},
    {"toString",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         *out = JsValue::Str(SelfStr(it, self));
         return true;
     }},
    {"valueOf",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         *out = JsValue::Str(SelfStr(it, self));
         return true;
     }},
};

// --------------------------------------------------------------- 数组方法
const JsMethodDef kArrayMethods[] = {
    {"push",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         for (const JsValue& v : a) JsSetIndex(it, self, n++, v);
         SetLength(it, self, n);
         *out = JsValue::Num(n);
         return true;
     }},
    {"pop",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         if (n <= 0) {
             *out = JsValue::Undef();
             return true;
         }
         *out = JsGetIndex(it, self, n - 1);
         JsSetProp(it, self, JsNumberToString(n - 1), JsValue::Undef());
         SetLength(it, self, n - 1);
         return true;
     }},
    {"shift",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         if (n <= 0) {
             *out = JsValue::Undef();
             return true;
         }
         *out = JsGetIndex(it, self, 0);
         for (double i = 1; i < n; ++i) {
             JsSetIndex(it, self, i - 1, JsGetIndex(it, self, i));
         }
         JsSetProp(it, self, JsNumberToString(n - 1), JsValue::Undef());
         SetLength(it, self, n - 1);
         return true;
     }},
    {"unshift",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         double k = (double)a.size();
         for (double i = n; i > 0; --i) {
             JsSetIndex(it, self, i + k - 1, JsGetIndex(it, self, i - 1));
         }
         for (size_t i = 0; i < a.size(); ++i) {
             JsSetIndex(it, self, (double)i, a[i]);
         }
         SetLength(it, self, n + k);
         *out = JsValue::Num(n + k);
         return true;
     }},
    {"slice",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         double b = a.empty() || Arg(a, 0).IsNullish() ? 0 : it.ToNumber(a[0]);
         double e = a.size() < 2 || Arg(a, 1).IsNullish() ? n : it.ToNumber(a[1]);
         if (b < 0) b += n;
         if (e < 0) e += n;
         b = std::max(0.0, std::min(b, n));
         e = std::max(0.0, std::min(e, n));
         std::vector<JsValue> items;
         for (double i = b; i < e; ++i) items.push_back(JsGetIndex(it, self, i));
         *out = it.NewArray(items);
         return true;
     }},
    {"splice",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         double start = a.empty() ? 0 : it.ToNumber(a[0]);
         if (start < 0) start += n;
         start = std::max(0.0, std::min(start, n));
         double del = a.size() < 2 ? n - start : it.ToNumber(a[1]);
         del = std::max(0.0, std::min(del, n - start));
         std::vector<JsValue> removed;
         for (double i = 0; i < del; ++i) {
             removed.push_back(JsGetIndex(it, self, start + i));
         }
         std::vector<JsValue> insert;
         for (size_t i = 2; i < a.size(); ++i) insert.push_back(a[i]);
         std::vector<JsValue> tail;
         for (double i = start + del; i < n; ++i) {
             tail.push_back(JsGetIndex(it, self, i));
         }
         double w = start;
         for (const JsValue& v : insert) JsSetIndex(it, self, w++, v);
         for (const JsValue& v : tail) JsSetIndex(it, self, w++, v);
         SetLength(it, self, w);
         *out = it.NewArray(removed);
         return true;
     }},
    {"concat",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::vector<JsValue> items;
         double n = TypedInt(it, self);
         for (double i = 0; i < n; ++i) items.push_back(JsGetIndex(it, self, i));
         for (const JsValue& v : a) {
             if (v.type == JsType::Object && v.obj && v.obj->is_array) {
                 double m = JsLengthOf(it, v);
                 for (double i = 0; i < m; ++i) items.push_back(JsGetIndex(it, v, i));
             } else {
                 items.push_back(v);
             }
         }
         *out = it.NewArray(items);
         return true;
     }},
    {"join",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string sep = a.empty() || Arg(a, 0).type == JsType::Undefined
                               ? ","
                               : it.ToString(a[0]);
         double n = TypedInt(it, self);
         std::string r;
         for (double i = 0; i < n; ++i) {
             if (i > 0) r += sep;
             JsValue v = JsGetIndex(it, self, i);
             if (!v.IsNullish()) r += it.ToString(v);
         }
         *out = JsValue::Str(r);
         return true;
     }},
    {"indexOf",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         double from = a.size() > 1 ? it.ToNumber(a[1]) : 0;
         if (from < 0) from += n;
         for (double i = std::max(0.0, from); i < n; ++i) {
             if (JsStrictEquals(JsGetIndex(it, self, i), Arg(a, 0))) {
                 *out = JsValue::Num(i);
                 return true;
             }
         }
         *out = JsValue::Num(-1);
         return true;
     }},
    {"lastIndexOf",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         for (double i = n - 1; i >= 0; --i) {
             if (JsStrictEquals(JsGetIndex(it, self, i), Arg(a, 0))) {
                 *out = JsValue::Num(i);
                 return true;
             }
         }
         *out = JsValue::Num(-1);
         return true;
     }},
    {"includes",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         for (double i = 0; i < n; ++i) {
             if (JsStrictEquals(JsGetIndex(it, self, i), Arg(a, 0))) {
                 *out = JsValue::Bool(true);
                 return true;
             }
         }
         *out = JsValue::Bool(false);
         return true;
     }},
    {"forEach",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         JsValue cb = Arg(a, 0);
         JsValue thisArg = a.size() > 1 ? a[1] : JsValue::Undef();
         for (double i = 0; i < n; ++i) {
             std::vector<JsValue> ca{JsGetIndex(it, self, i), JsValue::Num(i),
                                     self};
             it.CallFunction(cb, thisArg, ca, nullptr);
         }
         *out = JsValue::Undef();
         return true;
     }},
    {"map",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         std::vector<JsValue> items;
         JsValue cb = Arg(a, 0);
         JsValue thisArg = a.size() > 1 ? a[1] : JsValue::Undef();
         for (double i = 0; i < n; ++i) {
             std::vector<JsValue> ca{JsGetIndex(it, self, i), JsValue::Num(i),
                                     self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, thisArg, ca, &r);
             items.push_back(r);
         }
         *out = it.NewArray(items);
         return true;
     }},
    {"filter",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         std::vector<JsValue> items;
         JsValue cb = Arg(a, 0);
         for (double i = 0; i < n; ++i) {
             JsValue v = JsGetIndex(it, self, i);
             std::vector<JsValue> ca{v, JsValue::Num(i), self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, JsValue::Undef(), ca, &r);
             if (it.ToBool(r)) items.push_back(v);
         }
         *out = it.NewArray(items);
         return true;
     }},
    {"some",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         JsValue cb = Arg(a, 0);
         for (double i = 0; i < n; ++i) {
             std::vector<JsValue> ca{JsGetIndex(it, self, i), JsValue::Num(i),
                                     self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, JsValue::Undef(), ca, &r);
             if (it.ToBool(r)) {
                 *out = JsValue::Bool(true);
                 return true;
             }
         }
         *out = JsValue::Bool(false);
         return true;
     }},
    {"every",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         JsValue cb = Arg(a, 0);
         for (double i = 0; i < n; ++i) {
             std::vector<JsValue> ca{JsGetIndex(it, self, i), JsValue::Num(i),
                                     self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, JsValue::Undef(), ca, &r);
             if (!it.ToBool(r)) {
                 *out = JsValue::Bool(false);
                 return true;
             }
         }
         *out = JsValue::Bool(true);
         return true;
     }},
    {"find",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         JsValue cb = Arg(a, 0);
         for (double i = 0; i < n; ++i) {
             JsValue v = JsGetIndex(it, self, i);
             std::vector<JsValue> ca{v, JsValue::Num(i), self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, JsValue::Undef(), ca, &r);
             if (it.ToBool(r)) {
                 *out = v;
                 return true;
             }
         }
         *out = JsValue::Undef();
         return true;
     }},
    {"findIndex",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         JsValue cb = Arg(a, 0);
         for (double i = 0; i < n; ++i) {
             std::vector<JsValue> ca{JsGetIndex(it, self, i), JsValue::Num(i),
                                     self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, JsValue::Undef(), ca, &r);
             if (it.ToBool(r)) {
                 *out = JsValue::Num(i);
                 return true;
             }
         }
         *out = JsValue::Num(-1);
         return true;
     }},
    {"reduce",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         JsValue cb = Arg(a, 0);
         double i = 0;
         JsValue acc;
         if (a.size() > 1) {
             acc = a[1];
         } else if (n > 0) {
             acc = JsGetIndex(it, self, 0);
             i = 1;
         }
         for (; i < n; ++i) {
             std::vector<JsValue> ca{acc, JsGetIndex(it, self, i),
                                     JsValue::Num(i), self};
             JsValue r = JsValue::Undef();
             it.CallFunction(cb, JsValue::Undef(), ca, &r);
             acc = r;
         }
         *out = acc;
         return true;
     }},
    {"sort",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         std::vector<JsValue> items;
         for (double i = 0; i < n; ++i) items.push_back(JsGetIndex(it, self, i));
         JsValue cmp = Arg(a, 0);
         if (it.IsCallable(cmp)) {
             std::stable_sort(items.begin(), items.end(),
                              [&](const JsValue& x, const JsValue& y) {
                                  std::vector<JsValue> ca{x, y};
                                  JsValue r = JsValue::Undef();
                                  it.CallFunction(cmp, JsValue::Undef(), ca, &r);
                                  return it.ToNumber(r) < 0;
                              });
         } else {
             std::stable_sort(items.begin(), items.end(),
                              [&](const JsValue& x, const JsValue& y) {
                                  return it.ToString(x) < it.ToString(y);
                              });
         }
         for (size_t i = 0; i < items.size(); ++i) {
             JsSetIndex(it, self, (double)i, items[i]);
         }
         *out = self;
         return true;
     }},
    {"reverse",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         for (double i = 0; i < n / 2; ++i) {
             JsValue x = JsGetIndex(it, self, i);
             JsValue y = JsGetIndex(it, self, n - 1 - i);
             JsSetIndex(it, self, i, y);
             JsSetIndex(it, self, n - 1 - i, x);
         }
         *out = self;
         return true;
     }},
    {"toString",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         double n = TypedInt(it, self);
         std::string r;
         for (double i = 0; i < n; ++i) {
             if (i > 0) r += ",";
             JsValue v = JsGetIndex(it, self, i);
             if (!v.IsNullish()) r += it.ToString(v);
         }
         *out = JsValue::Str(r);
         return true;
     }},
};

// --------------------------------------------------------------- 数字方法
const JsMethodDef kNumberMethods[] = {
    {"toFixed",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         long long n = a.empty() ? 0 : ToInt(it, a[0]);
         if (n < 0) n = 0;
         if (n > 20) n = 20;
         char buf[64];
         snprintf(buf, sizeof(buf), "%.*f", (int)n, it.ToNumber(self));
         *out = JsValue::Str(buf);
         return true;
     }},
    {"toString",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         double v = it.ToNumber(self);
         long long radix = a.empty() ? 10 : ToInt(it, a[0]);
         if (radix == 10 || radix == 0) {
             *out = JsValue::Str(JsNumberToString(v));
             return true;
         }
         if (radix == 16) {
             char buf[32];
             snprintf(buf, sizeof(buf), "%llx", (unsigned long long)(long long)v);
             *out = JsValue::Str(buf);
             return true;
         }
         if (radix == 2 || radix == 8) {
             std::string s;
             long long x = (long long)v;
             if (x == 0) s = "0";
             while (x > 0) {
                 s = (char)('0' + (x % radix)) + s;
                 x /= radix;
             }
             *out = JsValue::Str(s);
             return true;
         }
         *out = JsValue::Str(JsNumberToString(v));
         return true;
     }},
    {"valueOf",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         *out = JsValue::Num(it.ToNumber(self));
         return true;
     }},
};

// --------------------------------------------------------------- 对象方法
const JsMethodDef kObjectMethods[] = {
    {"hasOwnProperty",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         std::string k = it.ToString(Arg(a, 0));
         if (!self.obj) {
             *out = JsValue::Bool(false);
             return true;
         }
         if (self.obj->is_array && k == "length") {
             *out = JsValue::Bool(true);
             return true;
         }
         *out = JsValue::Bool(self.obj->props.find(k) != self.obj->props.end());
         return true;
     }},
    {"toString",
     [](Interp& it, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         if (self.obj && self.obj->is_array) {
             *out = JsValue::Str("[object Array]");
             return true;
         }
         *out = JsValue::Str("[object Object]");
         (void)it;
         return true;
     }},
    {"valueOf",
     [](Interp&, void*, const std::string&, const Args&,
        const JsValue& self, JsValue* out) -> bool {
         *out = self;
         return true;
     }},
};

// ------------------------------------------------------------- 函数方法
const JsMethodDef kFunctionMethods[] = {
    {"call",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         JsValue thisArg = a.empty() ? JsValue::Undef() : a[0];
         std::vector<JsValue> rest;
         for (size_t i = 1; i < a.size(); ++i) rest.push_back(a[i]);
         *out = JsCall(it, self, thisArg, rest);
         return true;
     }},
    {"apply",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         JsValue thisArg = a.empty() ? JsValue::Undef() : a[0];
         std::vector<JsValue> rest;
         if (a.size() > 1 && a[1].obj) {
             double n = JsLengthOf(it, a[1]);
             for (double i = 0; i < n; ++i) rest.push_back(JsGetIndex(it, a[1], i));
         }
         *out = JsCall(it, self, thisArg, rest);
         return true;
     }},
    {"bind",
     [](Interp& it, void*, const std::string&, const Args& a,
        const JsValue& self, JsValue* out) -> bool {
         JsValue fn = self;
         JsValue thisArg = a.empty() ? JsValue::Undef() : a[0];
         std::vector<JsValue> bound;
         for (size_t i = 1; i < a.size(); ++i) bound.push_back(a[i]);
         // 捕获 fn/thisArg/bound：NativeFn 是 std::function，可以带状态。
         *out = it.MakeNative(
             "bound",
             [fn, thisArg, bound](Interp& in, void*, const std::string&,
                                  const std::vector<JsValue>& callArgs,
                                  const JsValue&, JsValue* o) -> bool {
                 std::vector<JsValue> all = bound;
                 for (const JsValue& v : callArgs) all.push_back(v);
                 *o = JsCall(in, fn, thisArg, all);
                 return true;
             });
         return true;
     }},
};

}  // namespace

const JsMethodDef* JsStringMethods(size_t* count) {
    *count = sizeof(kStringMethods) / sizeof(kStringMethods[0]);
    return kStringMethods;
}
const JsMethodDef* JsArrayMethods(size_t* count) {
    *count = sizeof(kArrayMethods) / sizeof(kArrayMethods[0]);
    return kArrayMethods;
}
const JsMethodDef* JsNumberMethods(size_t* count) {
    *count = sizeof(kNumberMethods) / sizeof(kNumberMethods[0]);
    return kNumberMethods;
}
const JsMethodDef* JsObjectMethods(size_t* count) {
    *count = sizeof(kObjectMethods) / sizeof(kObjectMethods[0]);
    return kObjectMethods;
}
const JsMethodDef* JsFunctionMethods(size_t* count) {
    *count = sizeof(kFunctionMethods) / sizeof(kFunctionMethods[0]);
    return kFunctionMethods;
}

JsValue JsMethodTableObject(const JsMethodDef* defs, size_t n) {
    auto o = std::make_shared<JsObject>();
    for (size_t i = 0; i < n; ++i) {
        auto f = std::make_shared<JsObject>();
        f->is_function = true;
        f->is_native = true;
        f->name = defs[i].name;
        f->native = defs[i].fn;
        o->props[defs[i].name] = JsValue::Obj(std::move(f));
    }
    return JsValue::Obj(std::move(o));
}

}  // namespace zb
