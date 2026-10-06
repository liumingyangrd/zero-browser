


#include "js.h"

#include "common.h"
#include "js_internal.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>

namespace zb {

namespace {
using Args = std::vector<JsValue>;

JsValue Arg(const Args& a, size_t i) {
    return i < a.size() ? a[i] : JsValue::Undef();
}


void JsonEscape(const std::string& s, std::string* out) {
    *out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': *out += "\\\""; break;
            case '\\': *out += "\\\\"; break;
            case '\n': *out += "\\n"; break;
            case '\r': *out += "\\r"; break;
            case '\t': *out += "\\t"; break;
            case '\b': *out += "\\b"; break;
            case '\f': *out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    *out += buf;
                } else {
                    out->push_back((char)c);
                }
        }
    }
    *out += '"';
}


bool JsonStringify(Interp& it, const JsValue& v, std::string* out, int depth,
                   std::vector<const JsObject*>* seen) {
    if (depth > 64) return false;
    switch (v.type) {
        case JsType::Undefined:
        case JsType::Function: *out += "null"; return true;
        case JsType::Null: *out += "null"; return true;
        case JsType::Bool: *out += v.b ? "true" : "false"; return true;
        case JsType::Number:
            if (std::isnan(v.num) || std::isinf(v.num)) *out += "null";
            else *out += JsNumberToString(v.num);
            return true;
        case JsType::String: JsonEscape(v.str, out); return true;
        case JsType::Host:
            JsonEscape(it.ToString(v), out);
            return true;
        case JsType::Object: break;
    }
    if (!v.obj) {
        *out += "null";
        return true;
    }
    if (v.obj->is_function) {
        *out += "null";
        return true;
    }
    for (const JsObject* o : *seen) {
        if (o == v.obj.get()) return false;  
    }
    seen->push_back(v.obj.get());
    bool ok = true;
    if (v.obj->is_array) {
        *out += '[';
        double n = it.ToNumber(JsValue::Num(v.obj->length));
        for (double i = 0; i < n && ok; ++i) {
            if (i > 0) *out += ',';
            ok = JsonStringify(it, JsGetIndex(it, v, i), out, depth + 1, seen);
        }
        *out += ']';
    } else {
        *out += '{';
        bool first = true;
        for (const auto& kv : v.obj->props) {
            if (kv.second.type == JsType::Undefined ||
                (kv.second.type == JsType::Function)) {
                continue;
            }
            if (!first) *out += ',';
            first = false;
            JsonEscape(kv.first, out);
            *out += ':';
            if (!JsonStringify(it, kv.second, out, depth + 1, seen)) {
                ok = false;
                break;
            }
        }
        *out += '}';
    }
    seen->pop_back();
    return ok;
}

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    Interp& it;
    bool ok = true;

    JsonParser(const std::string& src, Interp& in) : s(src), it(in) {}

    void Skip() {
        while (i < s.size() && isspace((unsigned char)s[i])) ++i;
    }
    bool Eat(char c) {
        Skip();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        ok = false;
        return false;
    }
    JsValue ParseValue() {
        Skip();
        if (i >= s.size()) {
            ok = false;
            return JsValue::Undef();
        }
        char c = s[i];
        if (c == '{') {
            ++i;
            JsValue o = it.NewObject();
            Skip();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return o;
            }
            for (;;) {
                Skip();
                if (i >= s.size() || s[i] != '"') {
                    ok = false;
                    return o;
                }
                std::string key = ParseString();
                if (!Eat(':')) return o;
                it.SetProp(o, key, ParseValue());
                if (!ok) return o;
                Skip();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                break;
            }
            Eat('}');
            return o;
        }
        if (c == '[') {
            ++i;
            std::vector<JsValue> items;
            Skip();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return it.NewArray(items);
            }
            for (;;) {
                items.push_back(ParseValue());
                if (!ok) return it.NewArray(items);
                Skip();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                break;
            }
            Eat(']');
            return it.NewArray(items);
        }
        if (c == '"') return JsValue::Str(ParseString());
        if (c == 't' && s.compare(i, 4, "true") == 0) {
            i += 4;
            return JsValue::Bool(true);
        }
        if (c == 'f' && s.compare(i, 5, "false") == 0) {
            i += 5;
            return JsValue::Bool(false);
        }
        if (c == 'n' && s.compare(i, 4, "null") == 0) {
            i += 4;
            return JsValue::Null();
        }
        
        size_t start = i;
        if (s[i] == '-' || s[i] == '+') ++i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.' ||
                                s[i] == 'e' || s[i] == 'E' || s[i] == '-' ||
                                s[i] == '+')) {
            ++i;
        }
        if (i == start) {
            ok = false;
            return JsValue::Undef();
        }
        return JsValue::Num(strtod(s.substr(start, i - start).c_str(), nullptr));
    }
    std::string ParseString() {
        std::string out;
        if (i >= s.size() || s[i] != '"') {
            ok = false;
            return out;
        }
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (i + 4 < s.size()) {
                            int cp = (int)strtol(s.substr(i + 1, 4).c_str(),
                                                 nullptr, 16);
                            i += 4;
                            if (cp < 0x80) {
                                out.push_back((char)cp);
                            } else if (cp < 0x800) {
                                out.push_back((char)(0xC0 | (cp >> 6)));
                                out.push_back((char)(0x80 | (cp & 0x3F)));
                            } else {
                                out.push_back((char)(0xE0 | (cp >> 12)));
                                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                                out.push_back((char)(0x80 | (cp & 0x3F)));
                            }
                        }
                        break;
                    }
                    default: out.push_back(e); break;
                }
                ++i;
                continue;
            }
            out.push_back(s[i++]);
        }
        if (i < s.size()) ++i;
        return out;
    }
};


std::string PercentEncode(const std::string& s, const char* keep) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || strchr(keep, (char)c)) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string PercentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int h = HexVal(s[i + 1]);
            int l = HexVal(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back((char)((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}



}  


void Interp::InstallBuiltins() {
    
    AddGlobal("NaN", JsValue::Num(NAN));
    AddGlobal("Infinity", JsValue::Num(INFINITY));
    AddGlobal("undefined", JsValue::Undef());

    
    AddGlobal("parseInt", MakeNative(
        "parseInt",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::string s = Trim(it.ToString(Arg(a, 0)));
            int radix = a.size() > 1 ? (int)it.ToNumber(a[1]) : 0;
            if (radix == 0) {
                radix = (s.size() > 1 && s[0] == '0' &&
                         (s[1] == 'x' || s[1] == 'X'))
                            ? 16
                            : 10;
            }
            if (radix == 16 && s.size() > 1 && s[0] == '0' &&
                (s[1] == 'x' || s[1] == 'X')) {
                s = s.substr(2);
            }
            char* end = nullptr;
            long long v = strtoll(s.c_str(), &end, radix);
            if (end == s.c_str()) {
                *out = JsValue::Num(NAN);
            } else {
                *out = JsValue::Num((double)v);
            }
            return true;
        }));
    AddGlobal("parseFloat", MakeNative(
        "parseFloat",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::string s = Trim(it.ToString(Arg(a, 0)));
            char* end = nullptr;
            double v = strtod(s.c_str(), &end);
            if (end == s.c_str()) {
                *out = JsValue::Num(NAN);
            } else {
                *out = JsValue::Num(v);
            }
            return true;
        }));
    AddGlobal("isNaN", MakeNative(
        "isNaN",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Bool(std::isnan(it.ToNumber(Arg(a, 0))));
            return true;
        }));
    AddGlobal("isFinite", MakeNative(
        "isFinite",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Bool(std::isfinite(it.ToNumber(Arg(a, 0))));
            return true;
        }));
    AddGlobal("encodeURIComponent", MakeNative(
        "encodeURIComponent",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Str(PercentEncode(it.ToString(Arg(a, 0)), "-_.!~*'()"));
            return true;
        }));
    AddGlobal("decodeURIComponent", MakeNative(
        "decodeURIComponent",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Str(PercentDecode(it.ToString(Arg(a, 0))));
            return true;
        }));
    AddGlobal("encodeURI", MakeNative(
        "encodeURI",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Str(
                PercentEncode(it.ToString(Arg(a, 0)), "-_.!~*'();/?:@&=+$,#"));
            return true;
        }));
    AddGlobal("decodeURI", MakeNative(
        "decodeURI",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Str(PercentDecode(it.ToString(Arg(a, 0))));
            return true;
        }));

    
    AddGlobal("String", MakeNative(
        "String",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Str(a.empty() ? "" : it.ToString(a[0]));
            return true;
        }));
    AddGlobal("Number", MakeNative(
        "Number",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Num(a.empty() ? 0 : it.ToNumber(a[0]));
            return true;
        }));
    AddGlobal("Boolean", MakeNative(
        "Boolean",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Bool(!a.empty() && it.ToBool(a[0]));
            return true;
        }));

    
    JsValue arrayCtor = MakeNative(
        "Array",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = it.NewArray(a);
            return true;
        });
    size_t na = 0;
    const JsMethodDef* am = JsArrayMethods(&na);
    arrayCtor.obj->props["prototype"] = JsMethodTableObject(am, na);
    arrayCtor.obj->props["isArray"] = MakeNative(
        "isArray",
        [](Interp&, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Bool(!a.empty() && a[0].type == JsType::Object &&
                                 a[0].obj && a[0].obj->is_array);
            return true;
        });
    arrayCtor.obj->props["from"] = MakeNative(
        "from",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::vector<JsValue> items;
            if (!a.empty()) {
                if (a[0].type == JsType::String) {
                    
                    const std::string& s = a[0].str;
                    size_t i = 0;
                    while (i < s.size()) {
                        unsigned char c = (unsigned char)s[i];
                        size_t len = 1;
                        if (c >= 0xF0) len = 4;
                        else if (c >= 0xE0) len = 3;
                        else if (c >= 0xC0) len = 2;
                        if (i + len > s.size()) len = 1;
                        items.push_back(JsValue::Str(s.substr(i, len)));
                        i += len;
                    }
                } else {
                    double n = JsLengthOf(it, a[0]);
                    for (double i = 0; i < n; ++i) {
                        items.push_back(JsGetIndex(it, a[0], i));
                    }
                }
            }
            *out = it.NewArray(items);
            return true;
        });
    arrayCtor.obj->props["of"] = MakeNative(
        "of",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = it.NewArray(a);
            return true;
        });
    AddGlobal("Array", arrayCtor);

    
    JsValue objectCtor = MakeNative(
        "Object",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            if (!a.empty() && a[0].IsObjectLike()) {
                *out = a[0];
            } else {
                *out = it.NewObject();
            }
            return true;
        });
    
    
    
    const JsMethodDef* om = JsObjectMethods(&na);
    objectCtor.obj->props["prototype"] = JsMethodTableObject(om, na);
    objectCtor.obj->props["keys"] = MakeNative(
        "keys",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::vector<JsValue> items;
            if (!a.empty() && a[0].obj) {
                if (a[0].obj->is_array) {
                    double n = JsLengthOf(it, a[0]);
                    for (double i = 0; i < n; ++i) {
                        items.push_back(JsValue::Str(JsNumberToString(i)));
                    }
                } else {
                    for (const auto& kv : a[0].obj->props) {
                        items.push_back(JsValue::Str(kv.first));
                    }
                }
            }
            *out = it.NewArray(items);
            return true;
        });
    objectCtor.obj->props["values"] = MakeNative(
        "values",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::vector<JsValue> items;
            if (!a.empty() && a[0].obj) {
                for (const auto& kv : a[0].obj->props) items.push_back(kv.second);
            }
            *out = it.NewArray(items);
            return true;
        });
    objectCtor.obj->props["entries"] = MakeNative(
        "entries",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::vector<JsValue> items;
            if (!a.empty() && a[0].obj) {
                for (const auto& kv : a[0].obj->props) {
                    std::vector<JsValue> pair{JsValue::Str(kv.first), kv.second};
                    items.push_back(it.NewArray(pair));
                }
            }
            *out = it.NewArray(items);
            return true;
        });
    objectCtor.obj->props["assign"] = MakeNative(
        "assign",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            if (a.empty()) {
                *out = it.NewObject();
                return true;
            }
            JsValue target = a[0].IsObjectLike() ? a[0] : it.NewObject();
            for (size_t i = 1; i < a.size(); ++i) {
                if (!a[i].obj) continue;
                for (const auto& kv : a[i].obj->props) {
                    JsSetProp(it, target, kv.first, kv.second);
                }
            }
            *out = target;
            return true;
        });
    objectCtor.obj->props["create"] = MakeNative(
        "create",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            JsValue o = it.NewObject();
            
            if (!a.empty() && a[0].obj) o.obj->proto = a[0].obj;
            *out = o;
            return true;
        });
    objectCtor.obj->props["defineProperty"] = MakeNative(
        "defineProperty",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            if (a.size() >= 3 && a[0].IsObjectLike()) {
                JsValue desc = a[2];
                JsValue v = it.GetProp(desc, "value");
                JsSetProp(it, a[0], it.ToString(a[1]), v);
            }
            *out = a.empty() ? JsValue::Undef() : a[0];
            return true;
        });
    objectCtor.obj->props["getOwnPropertyNames"] = objectCtor.obj->props["keys"];
    objectCtor.obj->props["freeze"] = MakeNative(
        "freeze",
        [](Interp&, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = a.empty() ? JsValue::Undef() : a[0];
            return true;
        });
    AddGlobal("Object", objectCtor);

    
    const JsMethodDef* fm = JsFunctionMethods(&na);
    JsValue functionProto = JsMethodTableObject(fm, na);
    global_->props["Function"] = JsValue::Obj([&] {
        auto o = std::make_shared<JsObject>();
        o->is_function = true;
        o->is_native = true;
        o->name = "Function";
        o->native = [](Interp& it, void*, const std::string&, const Args&,
                       const JsValue&, JsValue* out) -> bool {
            
            *out = it.MakeNative("anonymous",
                                 [](Interp&, void*, const std::string&,
                                    const Args&, const JsValue&,
                                    JsValue* r) -> bool {
                                     *r = JsValue::Undef();
                                     return true;
                                 });
            return true;
        };
        o->props["prototype"] = functionProto;
        return o;
    }());

    
    JsValue stringCtor = MakeNative(
        "String",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Str(a.empty() ? "" : it.ToString(a[0]));
            return true;
        });
    const JsMethodDef* sm = JsStringMethods(&na);
    stringCtor.obj->props["prototype"] = JsMethodTableObject(sm, na);
    stringCtor.obj->props["fromCharCode"] = MakeNative(
        "fromCharCode",
        [](Interp&, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::string s;
            for (const JsValue& v : a) {
                int c = (int)(v.type == JsType::Number ? v.num : 0);
                if (c < 0x80) {
                    s.push_back((char)c);
                } else if (c < 0x800) {
                    s.push_back((char)(0xC0 | (c >> 6)));
                    s.push_back((char)(0x80 | (c & 0x3F)));
                } else {
                    s.push_back((char)(0xE0 | (c >> 12)));
                    s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                    s.push_back((char)(0x80 | (c & 0x3F)));
                }
            }
            *out = JsValue::Str(s);
            return true;
        });
    global_->props["String"] = stringCtor;

    
    JsValue numberCtor = MakeNative(
        "Number",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Num(a.empty() ? 0 : it.ToNumber(a[0]));
            return true;
        });
    const JsMethodDef* nm = JsNumberMethods(&na);
    numberCtor.obj->props["prototype"] = JsMethodTableObject(nm, na);
    numberCtor.obj->props["isNaN"] = MakeNative(
        "isNaN",
        [](Interp&, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Bool(!a.empty() && a[0].type == JsType::Number &&
                                 std::isnan(a[0].num));
            return true;
        });
    numberCtor.obj->props["isFinite"] = MakeNative(
        "isFinite",
        [](Interp&, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Bool(!a.empty() && a[0].type == JsType::Number &&
                                 std::isfinite(a[0].num));
            return true;
        });
    numberCtor.obj->props["parseInt"] = global_->props["parseInt"];
    numberCtor.obj->props["parseFloat"] = global_->props["parseFloat"];
    numberCtor.obj->props["MAX_SAFE_INTEGER"] = JsValue::Num(9007199254740991.0);
    numberCtor.obj->props["NaN"] = JsValue::Num(NAN);
    global_->props["Number"] = numberCtor;

    
    JsValue math = NewObject();
    math.obj->props["PI"] = JsValue::Num(3.14159265358979323846);
    math.obj->props["E"] = JsValue::Num(2.71828182845904523536);
    math.obj->props["LN2"] = JsValue::Num(0.6931471805599453);
    auto m1 = [&](const char* name, double (*fn)(double)) {
        math.obj->props[name] = MakeNative(
            name, [fn](Interp& it, void*, const std::string&, const Args& a,
                       const JsValue&, JsValue* out) -> bool {
                *out = JsValue::Num(fn(it.ToNumber(Arg(a, 0))));
                return true;
            });
    };
    m1("abs", [](double x) { return std::fabs(x); });
    m1("floor", [](double x) { return std::floor(x); });
    m1("ceil", [](double x) { return std::ceil(x); });
    m1("round", [](double x) { return std::floor(x + 0.5); });
    m1("sqrt", [](double x) { return std::sqrt(x); });
    m1("sin", [](double x) { return std::sin(x); });
    m1("cos", [](double x) { return std::cos(x); });
    m1("tan", [](double x) { return std::tan(x); });
    m1("log", [](double x) { return std::log(x); });
    m1("exp", [](double x) { return std::exp(x); });
    m1("sign", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); });
    m1("trunc", [](double x) { return std::trunc(x); });
    math.obj->props["pow"] = MakeNative(
        "pow",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Num(std::pow(it.ToNumber(Arg(a, 0)),
                                         it.ToNumber(Arg(a, 1))));
            return true;
        });
    math.obj->props["max"] = MakeNative(
        "max",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            double best = -INFINITY;
            for (const JsValue& v : a) {
                double d = it.ToNumber(v);
                if (std::isnan(d)) {
                    *out = JsValue::Num(NAN);
                    return true;
                }
                if (d > best) best = d;
            }
            *out = JsValue::Num(a.empty() ? -INFINITY : best);
            return true;
        });
    math.obj->props["min"] = MakeNative(
        "min",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            double best = INFINITY;
            for (const JsValue& v : a) {
                double d = it.ToNumber(v);
                if (std::isnan(d)) {
                    *out = JsValue::Num(NAN);
                    return true;
                }
                if (d < best) best = d;
            }
            *out = JsValue::Num(a.empty() ? INFINITY : best);
            return true;
        });
    math.obj->props["random"] = MakeNative(
        "random",
        [](Interp&, void*, const std::string&, const Args&,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Num((double)rand() / ((double)RAND_MAX + 1.0));
            return true;
        });
    AddGlobal("Math", math);

    
    JsValue json = NewObject();
    json.obj->props["parse"] = MakeNative(
        "parse",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::string src = it.ToString(Arg(a, 0));
            JsonParser p(src, it);
            JsValue v = p.ParseValue();
            p.Skip();
            if (!p.ok || p.i != src.size()) {
                JsThrowError(it, "JSON.parse 解析失败");
            }
            *out = v;
            return true;
        });
    json.obj->props["stringify"] = MakeNative(
        "stringify",
        [](Interp& it, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            std::string s;
            std::vector<const JsObject*> seen;
            if (!JsonStringify(it, Arg(a, 0), &s, 0, &seen)) {
                JsThrowError(it, "JSON.stringify 遇到环形引用");
            }
            *out = JsValue::Str(s);
            return true;
        });
    AddGlobal("JSON", json);

    
    
    JsValue dateProto = NewObject();
    JsValue dateCtor = MakeNative(
        "Date",
        [dateProto](Interp& it, void*, const std::string&, const Args& a,
                    const JsValue& self, JsValue* out) -> bool {
            double ms = a.empty() ? (double)time(nullptr) * 1000.0
                                  : it.ToNumber(a[0]);
            if (self.IsObjectLike()) {
                self.obj->props["__ts"] = JsValue::Num(ms);
                self.obj->proto = dateProto.obj;
                *out = JsValue::Undef();  
                return true;
            }
            *out = JsValue::Str("Date");
            return true;
        });
    dateCtor.obj->props["now"] = MakeNative(
        "now",
        [](Interp&, void*, const std::string&, const Args&,
           const JsValue&, JsValue* out) -> bool {
            *out = JsValue::Num((double)time(nullptr) * 1000.0);
            return true;
        });
    dateCtor.obj->props["parse"] = MakeNative(
        "parse",
        [](Interp&, void*, const std::string&, const Args& a,
           const JsValue&, JsValue* out) -> bool {
            double ms = 0;
            std::string s = a.empty() ? "" : a[0].str;
            
            int Y = 0, M = 1, D = 1, h = 0, m = 0, sec = 0;
            if (sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m,
                       &sec) >= 3) {
                struct tm tmv {};
                tmv.tm_year = Y - 1900;
                tmv.tm_mon = M - 1;
                tmv.tm_mday = D;
                tmv.tm_hour = h;
                tmv.tm_min = m;
                tmv.tm_sec = sec;
                ms = (double)mktime(&tmv) * 1000.0;
            }
            *out = JsValue::Num(ms);
            return true;
        });
    auto dateGet = [this](const char* name,
                          std::function<double(const struct tm&)> sel) {
        return MakeNative(name, [sel](Interp& it, void*, const std::string&,
                                      const Args&, const JsValue& self,
                                      JsValue* out) -> bool {
            JsValue ts = it.GetProp(self, "__ts");
            time_t t = (time_t)(it.ToNumber(ts) / 1000.0);
            struct tm tmv {};
            struct tm* p = localtime(&t);
            if (p) tmv = *p;
            *out = JsValue::Num(sel(tmv));
            return true;
        });
    };
    dateProto.obj->props["getTime"] = MakeNative(
        "getTime",
        [](Interp& it, void*, const std::string&, const Args&,
           const JsValue& self, JsValue* out) -> bool {
            *out = it.GetProp(self, "__ts");
            return true;
        });
    dateProto.obj->props["getFullYear"] =
        dateGet("getFullYear", [](const struct tm& t) { return t.tm_year + 1900; });
    dateProto.obj->props["getMonth"] =
        dateGet("getMonth", [](const struct tm& t) { return t.tm_mon; });
    dateProto.obj->props["getDate"] =
        dateGet("getDate", [](const struct tm& t) { return t.tm_mday; });
    dateProto.obj->props["getHours"] =
        dateGet("getHours", [](const struct tm& t) { return t.tm_hour; });
    dateProto.obj->props["getMinutes"] =
        dateGet("getMinutes", [](const struct tm& t) { return t.tm_min; });
    dateProto.obj->props["getSeconds"] =
        dateGet("getSeconds", [](const struct tm& t) { return t.tm_sec; });
    dateProto.obj->props["getDay"] =
        dateGet("getDay", [](const struct tm& t) { return t.tm_wday; });
    dateProto.obj->props["toISOString"] = MakeNative(
        "toISOString",
        [](Interp& it, void*, const std::string&, const Args&,
           const JsValue& self, JsValue* out) -> bool {
            time_t t = (time_t)(it.ToNumber(it.GetProp(self, "__ts")) / 1000.0);
            struct tm tmv {};
            struct tm* p = gmtime(&t);
            if (p) tmv = *p;
            char buf[40];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            *out = JsValue::Str(buf);
            return true;
        });
    dateProto.obj->props["toString"] = MakeNative(
        "toString",
        [](Interp& it, void*, const std::string&, const Args&,
           const JsValue& self, JsValue* out) -> bool {
            time_t t = (time_t)(it.ToNumber(it.GetProp(self, "__ts")) / 1000.0);
            struct tm tmv {};
            struct tm* p = localtime(&t);
            if (p) tmv = *p;
            char buf[64];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            *out = JsValue::Str(buf);
            return true;
        });
    dateCtor.obj->props["prototype"] = dateProto;
    AddGlobal("Date", dateCtor);

    
    auto makeErrorCtor = [&](const char* name) {
        std::string nm = name;
        JsValue ctor = MakeNative(
            name, [nm](Interp& it, void*, const std::string&, const Args& a,
                       const JsValue& self, JsValue* out) -> bool {
                std::string msg = it.ToString(Arg(a, 0));
                JsValue target = self.IsObjectLike() ? self : it.NewObject();
                target.obj->props["name"] = JsValue::Str(nm);
                target.obj->props["message"] = JsValue::Str(msg);
                target.obj->props["toString"] = it.MakeNative(
                    "toString",
                    [](Interp& in, void*, const std::string&, const Args&,
                       const JsValue& me, JsValue* o) -> bool {
                        std::string n = in.ToString(in.GetProp(me, "name"));
                        std::string m = in.ToString(in.GetProp(me, "message"));
                        *o = JsValue::Str(m.empty() ? n : n + ": " + m);
                        return true;
                    });
                *out = self.IsObjectLike() ? JsValue::Undef() : target;
                return true;
            });
        ctor.obj->props["prototype"] = NewObject();
        return ctor;
    };
    AddGlobal("Error", makeErrorCtor("Error"));
    AddGlobal("TypeError", makeErrorCtor("TypeError"));
    AddGlobal("RangeError", makeErrorCtor("RangeError"));
    AddGlobal("ReferenceError", makeErrorCtor("ReferenceError"));
    AddGlobal("SyntaxError", makeErrorCtor("SyntaxError"));

    
    JsValue console = NewObject();
    auto logFn = [this](const char* level) {
        return MakeNative(
            level, [level](Interp& it, void*, const std::string&,
                           const Args& a, const JsValue&,
                           JsValue* out) -> bool {
                std::string line;
                for (size_t i = 0; i < a.size(); ++i) {
                    if (i) line += " ";
                    line += it.ToString(a[i]);
                }
                std::printf("[js console.%s] %s\n", level, line.c_str());
                *out = JsValue::Undef();
                return true;
            });
    };
    console.obj->props["log"] = logFn("log");
    console.obj->props["info"] = logFn("info");
    console.obj->props["warn"] = logFn("warn");
    console.obj->props["error"] = logFn("error");
    console.obj->props["debug"] = logFn("debug");
    AddGlobal("console", console);

    
    AddGlobal("globalThis", JsValue::Obj(global_));
    AddGlobal("self", JsValue::Obj(global_));
}

}  
