// 解释器核心：值转换、属性访问、函数调用、语句/表达式求值。
// 内置对象与内置方法表在 js_builtins.cpp。

#include "js.h"

#include "common.h"
#include "js_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zb {

// ------------------------------------------------------------------ 值构造
JsValue JsValue::Obj(std::shared_ptr<JsObject> o) {
    JsValue v;
    v.type = JsType::Object;
    if (o && o->is_function) v.type = JsType::Function;
    v.obj = std::move(o);
    return v;
}

JsValue JsValue::Host(void* h, const std::string& kind, const std::string& name) {
    auto o = std::make_shared<JsObject>();
    o->is_host = true;
    o->host = h;
    o->host_kind = kind;
    o->name = name;
    JsValue v;
    v.type = JsType::Host;
    v.obj = std::move(o);
    return v;
}

// ------------------------------------------------------------------ 小工具
std::string JsNumberToString(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return "0";
    char buf[64];
    // 整数值直接打成整数，避免出现 "1.000000"
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        snprintf(buf, sizeof(buf), "%.0f", d);
        return buf;
    }
    snprintf(buf, sizeof(buf), "%.14g", d);
    return buf;
}

bool JsIsArrayIndex(const std::string& key, double* out) {
    if (key.empty() || key.size() > 10) return false;
    for (char c : key) {
        if (c < '0' || c > '9') return false;
    }
    if (key.size() > 1 && key[0] == '0') return false;  // "01" 不是下标
    if (out) *out = strtod(key.c_str(), nullptr);
    return true;
}

double JsStringToNumber(const std::string& s) {
    std::string t = Trim(s);
    if (t.empty()) return 0;
    if (t == "Infinity" || t == "+Infinity") return INFINITY;
    if (t == "-Infinity") return -INFINITY;
    // 十六进制
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        return (double)strtoll(t.c_str() + 2, nullptr, 16);
    }
    const char* p = t.c_str();
    char* end = nullptr;
    double d = strtod(p, &end);
    if (end == p) return NAN;
    while (end && *end == ' ') ++end;
    if (end && *end != '\0') return NAN;
    return d;
}

std::string JsTypeOf(const JsValue& v) {
    switch (v.type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "object";
        case JsType::Bool: return "boolean";
        case JsType::Number: return "number";
        case JsType::String: return "string";
        case JsType::Function: return "function";
        case JsType::Host:
            // 宿主对象里的函数（方法工厂）在 call 时才算函数，这里按 object 报。
            return "object";
        case JsType::Object: return "object";
    }
    return "object";
}

bool JsIsCallable(const JsValue& v) {
    return v.type == JsType::Function && v.obj && v.obj->is_function;
}

bool JsStrictEquals(const JsValue& a, const JsValue& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case JsType::Undefined:
        case JsType::Null: return true;
        case JsType::Bool: return a.b == b.b;
        case JsType::Number:
            if (std::isnan(a.num) || std::isnan(b.num)) return false;
            return a.num == b.num;
        case JsType::String: return a.str == b.str;
        default: return a.obj == b.obj;
    }
}

// 数字与非数字的宽松比较：字符串能转成数字就按数字比。
bool JsLooseEquals(Interp& it, const JsValue& a, const JsValue& b) {
    if (a.type == b.type) return JsStrictEquals(a, b);
    if (a.IsNullish() && b.IsNullish()) return true;
    if (a.IsNullish() || b.IsNullish()) return false;
    if (a.type == JsType::Bool) return JsLooseEquals(it, JsValue::Num(a.b ? 1 : 0), b);
    if (b.type == JsType::Bool) return JsLooseEquals(it, a, JsValue::Num(b.b ? 1 : 0));
    if (a.type == JsType::Number && b.type == JsType::String) {
        return a.num == JsStringToNumber(b.str);
    }
    if (a.type == JsType::String && b.type == JsType::Number) {
        return JsStringToNumber(a.str) == b.num;
    }
    if (a.IsObjectLike() && !b.IsObjectLike()) {
        return JsLooseEquals(it, it.ToPrimitive(a), b);
    }
    if (!a.IsObjectLike() && b.IsObjectLike()) {
        return JsLooseEquals(it, a, it.ToPrimitive(b));
    }
    return false;
}

// ------------------------------------------------------------------ 解释器
Interp::Interp(JsBridge* bridge) : bridge_(bridge) {
    envs_.push_back(std::make_shared<Env>());
    global_ = std::make_shared<JsObject>();
    InstallBuiltins();
}

Interp::~Interp() = default;

// 步数预算：每个语句/表达式/调用都记一步，超预算就抛异常终止这段脚本。
// 没有它，页面里一句 while(true){} 就能把浏览器永久卡住。
//
// 注意"抛错本身也要花步数"这一点：错误格式化会走到 CallFunction -> JsCall ->
// BumpSteps，如果超限后还继续抛"带 toString 的错误对象"，就会形成
//   CallFunction -> ToPrimitive -> ToString -> CallFunction
// 的无限递归（实测直接爆栈）。所以超限后只抛一个纯字符串值。
void Interp::BumpSteps(long long n) {
    steps_ += n;
    if (steps_ > step_limit_) {
        if (exhausted_) {
            throw JsThrow{JsValue::Str("脚本执行步数超限（疑似死循环），已中止")};
        }
        exhausted_ = true;
        throw JsThrow{MakeError("脚本执行步数超限（疑似死循环），已中止")};
    }
}

// 安全的错误文本：不调用 JS 的 toString（那会回调解释器，见上），
// 直接从属性里读 name/message。
std::string Interp::ErrorText(const JsValue& v) {
    if (v.type == JsType::Object && v.obj) {
        std::string ns;
        std::string ms;
        auto n = v.obj->props.find("name");
        if (n != v.obj->props.end() && n->second.type == JsType::String) {
            ns = n->second.str;
        }
        auto m = v.obj->props.find("message");
        if (m != v.obj->props.end() && m->second.type == JsType::String) {
            ms = m->second.str;
        }
        if (!ns.empty() || !ms.empty()) {
            if (ns.empty()) return ms;
            if (ms.empty()) return ns;
            return ns + ": " + ms;
        }
        return "[object Object]";
    }
    if (v.type == JsType::Function) return "function";
    return ToString(v);
}

JsValue Interp::NewObject() {
    return JsValue::Obj(std::make_shared<JsObject>());
}

JsValue Interp::NewArray() {
    auto o = std::make_shared<JsObject>();
    o->is_array = true;
    return JsValue::Obj(std::move(o));
}

JsValue Interp::NewArray(const std::vector<JsValue>& items) {
    auto o = std::make_shared<JsObject>();
    o->is_array = true;
    o->length = (double)items.size();
    for (size_t i = 0; i < items.size(); ++i) {
        o->props[std::to_string(i)] = items[i];
    }
    return JsValue::Obj(std::move(o));
}

JsValue Interp::MakeNative(const std::string& name, NativeFn fn) {
    auto o = std::make_shared<JsObject>();
    o->is_function = true;
    o->is_native = true;
    o->name = name;
    o->native = std::move(fn);
    return JsValue::Obj(std::move(o));
}

JsValue Interp::MakeHost(void* host, const std::string& kind,
                         const std::string& name) {
    return JsValue::Host(host, kind, name);
}

void Interp::AddGlobal(const std::string& name, const JsValue& v) {
    global_->props[name] = v;
}

bool Interp::GetGlobal(const std::string& name, JsValue* out) const {
    auto it = global_->props.find(name);
    if (it == global_->props.end()) return false;
    if (out) *out = it->second;
    return true;
}

void Interp::NoteScript(bool ok, long long steps, const std::string& err) {
    stats_.scripts += 1;
    if (!ok) {
        stats_.failed += 1;
        stats_.last_error = err;
    }
    stats_.steps += steps;
}

// ------------------------------------------------------------------ 类型转换
JsValue Interp::ToPrimitive(const JsValue& v) {
    if (!v.IsObjectLike()) return v;
    if (v.type == JsType::Host) {
        return JsValue::Str(ToString(v));
    }
    // 对象：有 valueOf/toString 就调用（数组的 toString 会 join）
    if (v.obj) {
        auto it = v.obj->props.find("valueOf");
        if (it == v.obj->props.end()) it = v.obj->props.find("toString");
        if (it != v.obj->props.end() && JsIsCallable(it->second)) {
            JsValue r;
            if (CallFunction(it->second, v, {}, &r) && !r.IsObjectLike()) {
                return r;
            }
        }
        if (v.obj->is_array) {
            // 数组默认 toString = join(",")
            std::string out;
            for (double i = 0; i < v.obj->length; ++i) {
                if (i > 0) out += ",";
                JsValue e = JsGetIndex(*this, v, i);
                if (!e.IsNullish()) out += ToString(e);
            }
            return JsValue::Str(out);
        }
    }
    return JsValue::Str("[object Object]");
}

std::string Interp::ToString(const JsValue& v) {
    switch (v.type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "null";
        case JsType::Bool: return v.b ? "true" : "false";
        case JsType::Number: return JsNumberToString(v.num);
        case JsType::String: return v.str;
        case JsType::Host:
            if (bridge_ && v.obj) {
                return bridge_->HostToString(v.obj->host, v.obj->host_kind);
            }
            return "[object Host]";
        case JsType::Function:
            return "function " + (v.obj ? v.obj->name : std::string()) + "() { }";
        case JsType::Object:
            break;
    }
    if (v.obj && v.obj->is_array) {
        return ToString(ToPrimitive(v));
    }
    // 对象：优先用自身的 toString（Error 就是靠它把 name/message 拼出来的）。
    // ToPrimitive 内部已经按 valueOf -> toString 的顺序尝试过。
    if (v.obj) {
        JsValue prim = ToPrimitive(v);
        if (!prim.IsObjectLike()) return ToString(prim);
    }
    return "[object Object]";
}

double Interp::ToNumber(const JsValue& v) {
    switch (v.type) {
        case JsType::Undefined: return NAN;
        case JsType::Null: return 0;
        case JsType::Bool: return v.b ? 1 : 0;
        case JsType::Number: return v.num;
        case JsType::String: return JsStringToNumber(v.str);
        default: return ToNumber(ToPrimitive(v));
    }
}

bool Interp::ToBool(const JsValue& v) {
    switch (v.type) {
        case JsType::Undefined:
        case JsType::Null: return false;
        case JsType::Bool: return v.b;
        case JsType::Number: return !(v.num == 0 || std::isnan(v.num));
        case JsType::String: return !v.str.empty();
        default: return true;
    }
}

JsValue Interp::MakeError(const std::string& message) {
    JsValue e = NewObject();
    e.obj->props["name"] = JsValue::Str("Error");
    e.obj->props["message"] = JsValue::Str(message);
    // 自带 toString：否则 String(err) 只会得到 "[object Object]"，
    // 诊断信息里就看不到真正的原因。
    e.obj->props["toString"] = MakeNative(
        "toString",
        [](Interp& in, void*, const std::string&, const std::vector<JsValue>&,
           const JsValue& me, JsValue* out) -> bool {
            std::string n = in.ToString(in.GetProp(me, "name"));
            std::string m = in.ToString(in.GetProp(me, "message"));
            *out = JsValue::Str(m.empty() ? n : n + ": " + m);
            return true;
        });
    return e;
}

// 用户函数自动带一个 prototype 对象：`function P(){}; P.prototype.m = ...;`
// 与 `new P()` 之后的 instanceof 都依赖它。缺了它 p.sum() 会报"不是函数"。
void EnsurePrototype(Interp& it, const std::shared_ptr<JsObject>& fn) {
    if (!fn) return;
    if (fn->props.find("prototype") == fn->props.end()) {
        fn->props["prototype"] = it.NewObject();
    }
}

void JsThrowError(Interp& it, const std::string& msg) {
    throw JsThrow{it.MakeError(msg)};
}

// -------------------------------------------------------------- 数组/类数组
double JsLengthOf(Interp& it, const JsValue& v) {
    if (v.IsNullish()) return 0;
    if (v.type == JsType::String) return (double)v.str.size();
    if (v.type == JsType::Object && v.obj) {
        if (v.obj->is_array) return v.obj->length;
        auto it2 = v.obj->props.find("length");
        if (it2 != v.obj->props.end()) {
            double n = it.ToNumber(it2->second);
            return std::isnan(n) || n < 0 ? 0 : std::floor(n);
        }
    }
    return 0;
}

JsValue JsGetIndex(Interp& it, const JsValue& v, double i) {
    if (v.type == JsType::String) {
        size_t idx = (i < 0 || std::isnan(i)) ? v.str.size() : (size_t)i;
        if (idx >= v.str.size()) return JsValue::Undef();
        return JsValue::Str(v.str.substr(idx, 1));
    }
    return JsGetProp(it, v, JsNumberToString(i));
}

void JsSetIndex(Interp& it, const JsValue& v, double i, const JsValue& val) {
    JsSetProp(it, v, JsNumberToString(i), val);
}

// ---------------------------------------------------------------- 属性访问
namespace {
JsValue LookupInTable(const JsMethodDef* defs, size_t n, const std::string& key,
                      const char* owner) {
    for (size_t i = 0; i < n; ++i) {
        if (key == defs[i].name) {
            auto o = std::make_shared<JsObject>();
            o->is_function = true;
            o->is_native = true;
            o->name = key;
            o->native = defs[i].fn;
            (void)owner;
            return JsValue::Obj(std::move(o));
        }
    }
    return JsValue::Undef();
}
}  // namespace

JsValue JsGetProp(Interp& it, const JsValue& base, const std::string& key) {
    if (base.IsNullish()) {
        JsThrowError(it, "无法读取 " + std::string(base.type == JsType::Null
                                                       ? "null"
                                                       : "undefined") +
                             " 的属性 '" + key + "'");
    }
    if (base.type == JsType::Host) {
        JsValue out;
        if (it.Bridge() && base.obj &&
            it.Bridge()->HostGet(base.obj->host, base.obj->host_kind, key, &out)) {
            return out;
        }
        return JsValue::Undef();
    }
    if (base.type == JsType::String) {
        if (key == "length") return JsValue::Num((double)base.str.size());
        size_t n = 0;
        const JsMethodDef* d = JsStringMethods(&n);
        JsValue m = LookupInTable(d, n, key, "String");
        if (m.type == JsType::Function) return m;
        // 下标访问：'abc'[0]
        double idx = 0;
        if (JsIsArrayIndex(key, &idx)) {
            if (idx >= 0 && idx < (double)base.str.size()) {
                return JsValue::Str(base.str.substr((size_t)idx, 1));
            }
        }
        return JsValue::Undef();
    }
    if (base.type == JsType::Number) {
        size_t n = 0;
        const JsMethodDef* d = JsNumberMethods(&n);
        return LookupInTable(d, n, key, "Number");
    }
    if (base.type == JsType::Bool) {
        if (key == "toString") {
            return it.MakeNative("toString",
                                 [](Interp& in, void*, const std::string&,
                                    const std::vector<JsValue>&,
                                    const JsValue& self, JsValue* out) {
                                     *out = JsValue::Str(in.ToBool(self) ? "true"
                                                                        : "false");
                                     return true;
                                 });
        }
        return JsValue::Undef();
    }
    if (base.type == JsType::Function) {
        size_t n = 0;
        const JsMethodDef* d = JsFunctionMethods(&n);
        JsValue m = LookupInTable(d, n, key, "Function");
        if (m.type == JsType::Function) return m;
        if (base.obj) {
            auto own = base.obj->props.find(key);
            if (own != base.obj->props.end()) return own->second;
        }
        if (key == "length") {
            return JsValue::Num(base.obj ? (double)base.obj->params.size() : 0);
        }
        if (key == "name") {
            return JsValue::Str(base.obj ? base.obj->name : std::string());
        }
        return JsValue::Undef();
    }
    // 普通对象 / 数组
    if (base.obj) {
        auto own = base.obj->props.find(key);
        if (own != base.obj->props.end()) return own->second;
        // 原型链回退：new Date().getTime()、自定义原型方法走这条路径。
        for (auto p = base.obj->proto; p; p = p->proto) {
            auto it2 = p->props.find(key);
            if (it2 != p->props.end()) return it2->second;
        }
        size_t n = 0;
        if (base.obj->is_array) {
            if (key == "length") return JsValue::Num(base.obj->length);
            const JsMethodDef* d = JsArrayMethods(&n);
            JsValue m = LookupInTable(d, n, key, "Array");
            if (m.type == JsType::Function) return m;
            // 数组也允许用对象方法（hasOwnProperty 等）
            const JsMethodDef* od = JsObjectMethods(&n);
            return LookupInTable(od, n, key, "Object");
        }
        const JsMethodDef* d = JsObjectMethods(&n);
        return LookupInTable(d, n, key, "Object");
    }
    return JsValue::Undef();
}

void JsSetProp(Interp& it, const JsValue& base, const std::string& key,
               const JsValue& v) {
    if (base.type == JsType::Host) {
        if (it.Bridge() && base.obj &&
            it.Bridge()->HostSet(base.obj->host, base.obj->host_kind, key, v)) {
            return;
        }
        return;
    }
    if (!base.obj) return;  // 基本类型的属性赋值：非严格模式下静默忽略
    if (base.obj->is_array) {
        if (key == "length") {
            double n = it.ToNumber(v);
            if (!std::isnan(n) && n >= 0) base.obj->length = std::floor(n);
            return;
        }
        double idx = 0;
        if (JsIsArrayIndex(key, &idx)) {
            base.obj->props[key] = v;
            if (idx + 1 > base.obj->length) base.obj->length = idx + 1;
            return;
        }
    }
    base.obj->props[key] = v;
}

// ------------------------------------------------------------------ 函数调用
JsValue JsCall(Interp& it, const JsValue& fn, const JsValue& this_val,
               const std::vector<JsValue>& args) {
    if (!JsIsCallable(fn)) {
        JsThrowError(it, it.ToString(fn) + " 不是函数");
    }
    if (!it.EnterCall()) {
        JsThrowError(it, "调用层级过深（疑似无限递归），已中止");
    }
    // RAII：无论正常返回还是抛异常，都要把调用深度还回去。
    struct DepthGuard {
        Interp& it;
        ~DepthGuard() { it.LeaveCall(); }
    } guard{it};
    it.BumpSteps(1);
    if (fn.obj->is_native) {
        JsValue out = JsValue::Undef();
        if (fn.obj->native) {
            fn.obj->native(it, fn.obj->host, fn.obj->name, args, this_val, &out);
        }
        return out;
    }
    // 用户函数
    auto env = std::make_shared<Env>();
    env->parent = fn.obj->closure;
    env->is_function_scope = true;
    env->this_val = fn.obj->is_arrow && fn.obj->closure
                        ? fn.obj->closure->this_val
                        : this_val;
    env->has_this = true;
    for (size_t i = 0; i < fn.obj->params.size(); ++i) {
        env->vars[fn.obj->params[i]] =
            i < args.size() ? args[i] : JsValue::Undef();
    }
    // arguments：本项目里直接做成真数组（真机是类数组对象），
    // 这样 Array.prototype.slice.call(arguments) 依然可用。
    env->vars["arguments"] = it.NewArray(args);
    // 函数体顶层的函数声明与 var 提升（body 在解析时已展开成语句序列）。
    for (const AstNode* s : fn.obj->body) {
        if (!s) continue;
        if (s->kind == AstKind::FunctionDecl) {
            auto sub = std::make_shared<JsObject>();
            sub->is_function = true;
            sub->name = s->str;
            sub->params = s->names;
            if (!s->kids.empty() && s->kids[0] &&
                s->kids[0]->kind == AstKind::Block) {
                sub->body = s->kids[0]->kids;
            }
            sub->closure = env;
            EnsurePrototype(it, sub);
            env->vars[s->str] = JsValue::Obj(std::move(sub));
        } else if (s->kind == AstKind::Var) {
            for (const std::string& nm : s->names) {
                if (env->vars.find(nm) == env->vars.end()) {
                    env->vars[nm] = JsValue::Undef();
                }
            }
        }
    }
    JsValue result = JsValue::Undef();
    try {
        for (const AstNode* s : fn.obj->body) {
            it.EvalStmt(s, *env);
        }
    } catch (JsReturn& r) {
        result = r.value;
        return result;
    }
    return result;
}

bool Interp::CallFunction(const JsValue& fn, const JsValue& this_val,
                          const std::vector<JsValue>& args, JsValue* out) {
    if (!JsIsCallable(fn)) return false;
    try {
        JsValue r = JsCall(*this, fn, this_val, args);
        if (out) *out = r;
        return true;
    } catch (JsThrow& t) {
        error_ = ErrorText(t.value);
        return false;
    }
}

bool Interp::IsCallable(const JsValue& v) const { return JsIsCallable(v); }

JsValue Interp::GetProp(const JsValue& base, const std::string& key) {
    return JsGetProp(*this, base, key);
}

void Interp::SetProp(const JsValue& base, const std::string& key,
                     const JsValue& v) {
    JsSetProp(*this, base, key, v);
}

// ------------------------------------------------------------------ 求值
namespace {
struct Ref {
    // 赋值目标：变量 或 对象属性
    Env* env = nullptr;
    std::string name;
    JsValue base;
    std::string key;
    bool is_prop = false;
};
}  // namespace

JsValue Interp::EvalExpr(const AstNode* n, Env& env) {
    if (!n) return JsValue::Undef();
    BumpSteps(1);
    switch (n->kind) {
        case AstKind::Num: return JsValue::Num(n->num);
        case AstKind::Str: return JsValue::Str(n->str);
        case AstKind::Bool: return JsValue::Bool(n->num != 0);
        case AstKind::Null: return JsValue::Null();
        case AstKind::Undefined: return JsValue::Undef();
        case AstKind::This: {
            for (Env* e = &env; e; e = e->parent.get()) {
                if (e->has_this) return e->this_val;
            }
            return JsValue::Undef();
        }
        case AstKind::Ident: {
            JsValue* v = env.Find(n->str);
            if (v) return *v;
            auto it = global_->props.find(n->str);
            if (it != global_->props.end()) return it->second;
            // 未声明变量：返回 undefined（非严格模式）
            return JsValue::Undef();
        }
        case AstKind::Array: {
            std::vector<JsValue> items;
            items.reserve(n->kids.size());
            for (const AstNode* k : n->kids) items.push_back(EvalExpr(k, env));
            return NewArray(items);
        }
        case AstKind::Object: {
            JsValue o = NewObject();
            for (size_t i = 0; i < n->kids.size(); ++i) {
                bool computed = i < n->flags.size() && n->flags[i] == 1;
                if (computed) {
                    // 计算属性名：解析器把 [key] 与 value 成对放进 kids。
                    std::string key = ToString(EvalExpr(n->kids[i], env));
                    if (i + 1 < n->kids.size()) {
                        JsSetProp(*this, o, key, EvalExpr(n->kids[i + 1], env));
                        ++i;
                    }
                    continue;
                }
                std::string key = i < n->names.size() ? n->names[i] : "";
                JsSetProp(*this, o, key, EvalExpr(n->kids[i], env));
            }
            return o;
        }
        case AstKind::Member: {
            JsValue base = EvalExpr(n->kids[0], env);
            std::string key = n->computed
                                  ? ToString(EvalExpr(n->kids[1], env))
                                  : n->str;
            return JsGetProp(*this, base, key);
        }
        case AstKind::Call: {
            // 方法调用要把接收者当 this
            if (n->kids[0] && n->kids[0]->kind == AstKind::Member) {
                const AstNode* m = n->kids[0];
                JsValue base = EvalExpr(m->kids[0], env);
                std::string key =
                    m->computed ? ToString(EvalExpr(m->kids[1], env)) : m->str;
                JsValue fn = JsGetProp(*this, base, key);
                std::vector<JsValue> args;
                for (size_t i = 1; i < n->kids.size(); ++i) {
                    args.push_back(EvalExpr(n->kids[i], env));
                }
                return JsCall(*this, fn, base, args);
            }
            JsValue fn = EvalExpr(n->kids[0], env);
            std::vector<JsValue> args;
            for (size_t i = 1; i < n->kids.size(); ++i) {
                args.push_back(EvalExpr(n->kids[i], env));
            }
            return JsCall(*this, fn, JsValue::Undef(), args);
        }
        case AstKind::New: {
            JsValue fn = EvalExpr(n->kids[0], env);
            std::vector<JsValue> args;
            for (size_t i = 1; i < n->kids.size(); ++i) {
                args.push_back(EvalExpr(n->kids[i], env));
            }
            JsValue obj = NewObject();
            if (fn.type == JsType::Function && fn.obj) {
                obj.obj->name = fn.obj->name;
                // new 出来的对象原型指向 F.prototype，自定义构造函数的
                // instanceof 与原型方法查找才能正常工作。
                auto pit = fn.obj->props.find("prototype");
                if (pit != fn.obj->props.end() && pit->second.obj) {
                    obj.obj->proto = pit->second.obj;
                }
            }
            JsValue r = JsCall(*this, fn, obj, args);
            if (r.IsObjectLike()) return r;
            return obj;
        }
        case AstKind::Function: {
            auto fn = std::make_shared<JsObject>();
            fn->is_function = true;
            fn->name = n->str;
            fn->params = n->names;
            fn->is_arrow = !n->flags.empty() && n->flags[0] == 1;
            if (!n->kids.empty() && n->kids[0]) {
                if (n->kids[0]->kind == AstKind::Block) {
                    fn->body = n->kids[0]->kids;
                } else {
                    fn->body.push_back(n->kids[0]);
                }
            }
            fn->closure = std::make_shared<Env>(env);
            EnsurePrototype(*this, fn);
            return JsValue::Obj(std::move(fn));
        }
        case AstKind::Sequence: {
            JsValue last = JsValue::Undef();
            for (const AstNode* k : n->kids) last = EvalExpr(k, env);
            return last;
        }
        case AstKind::Unary: {
            const std::string& op = n->str;
            if (op == "typeof") {
                // typeof 未声明变量不能抛异常
                if (n->kids[0]->kind == AstKind::Ident &&
                    !env.Find(n->kids[0]->str) &&
                    global_->props.find(n->kids[0]->str) == global_->props.end()) {
                    return JsValue::Str("undefined");
                }
                return JsValue::Str(JsTypeOf(EvalExpr(n->kids[0], env)));
            }
            if (op == "delete") {
                if (n->kids[0]->kind == AstKind::Member) {
                    const AstNode* m = n->kids[0];
                    JsValue base = EvalExpr(m->kids[0], env);
                    std::string key =
                        m->computed ? ToString(EvalExpr(m->kids[1], env)) : m->str;
                    if (base.type == JsType::Host) {
                        JsSetProp(*this, base, key, JsValue::Undef());
                        return JsValue::Bool(true);
                    }
                    if (base.obj) base.obj->props.erase(key);
                    return JsValue::Bool(true);
                }
                return JsValue::Bool(true);
            }
            JsValue v = EvalExpr(n->kids[0], env);
            if (op == "!") return JsValue::Bool(!ToBool(v));
            if (op == "-") return JsValue::Num(-ToNumber(v));
            if (op == "+") return JsValue::Num(ToNumber(v));
            if (op == "~") return JsValue::Num((double)~(int)ToNumber(v));
            if (op == "void") return JsValue::Undef();
            return JsValue::Undef();
        }
        case AstKind::Update: {
            // ++/-- 需要写回目标，所以这里分别处理变量与属性
            const AstNode* target = n->kids[0];
            double old = 0;
            if (target->kind == AstKind::Ident) {
                JsValue* v = env.Find(target->str);
                old = v ? ToNumber(*v) : 0;
                double nv = old + (n->str == "++" ? 1 : -1);
                if (v) *v = JsValue::Num(nv);
                else env.Assign(target->str, JsValue::Num(nv));
                return JsValue::Num(n->computed ? nv : old);
            }
            if (target->kind == AstKind::Member) {
                const AstNode* m = target;
                JsValue base = EvalExpr(m->kids[0], env);
                std::string key =
                    m->computed ? ToString(EvalExpr(m->kids[1], env)) : m->str;
                JsValue cur = JsGetProp(*this, base, key);
                old = ToNumber(cur);
                double nv = old + (n->str == "++" ? 1 : -1);
                JsSetProp(*this, base, key, JsValue::Num(nv));
                return JsValue::Num(n->computed ? nv : old);
            }
            return JsValue::Num(0);
        }
        case AstKind::Logical: {
            JsValue l = EvalExpr(n->kids[0], env);
            if (n->str == "&&") return ToBool(l) ? EvalExpr(n->kids[1], env) : l;
            if (n->str == "||") return ToBool(l) ? l : EvalExpr(n->kids[1], env);
            // ??
            if (n->str == "??") return l.IsNullish() ? EvalExpr(n->kids[1], env) : l;
            return l;
        }
        case AstKind::Binary: {
            const std::string& op = n->str;
            JsValue a = EvalExpr(n->kids[0], env);
            JsValue b = EvalExpr(n->kids[1], env);
            if (op == "+") {
                JsValue pa = ToPrimitive(a);
                JsValue pb = ToPrimitive(b);
                if (pa.type == JsType::String || pb.type == JsType::String) {
                    return JsValue::Str(ToString(pa) + ToString(pb));
                }
                return JsValue::Num(ToNumber(pa) + ToNumber(pb));
            }
            if (op == "-") return JsValue::Num(ToNumber(a) - ToNumber(b));
            if (op == "*") return JsValue::Num(ToNumber(a) * ToNumber(b));
            if (op == "/") return JsValue::Num(ToNumber(a) / ToNumber(b));
            if (op == "%") {
                double x = ToNumber(a);
                double y = ToNumber(b);
                return JsValue::Num(std::fmod(x, y));
            }
            if (op == "**") return JsValue::Num(std::pow(ToNumber(a), ToNumber(b)));
            if (op == "==") return JsValue::Bool(JsLooseEquals(*this, a, b));
            if (op == "!=") return JsValue::Bool(!JsLooseEquals(*this, a, b));
            if (op == "===") return JsValue::Bool(JsStrictEquals(a, b));
            if (op == "!==") return JsValue::Bool(!JsStrictEquals(a, b));
            if (op == "<" || op == ">" || op == "<=" || op == ">=") {
                JsValue pa = ToPrimitive(a);
                JsValue pb = ToPrimitive(b);
                int cmp = 0;
                if (pa.type == JsType::String && pb.type == JsType::String) {
                    cmp = pa.str.compare(pb.str);
                } else {
                    double x = ToNumber(pa);
                    double y = ToNumber(pb);
                    if (std::isnan(x) || std::isnan(y)) return JsValue::Bool(false);
                    cmp = x < y ? -1 : (x > y ? 1 : 0);
                }
                if (op == "<") return JsValue::Bool(cmp < 0);
                if (op == ">") return JsValue::Bool(cmp > 0);
                if (op == "<=") return JsValue::Bool(cmp <= 0);
                return JsValue::Bool(cmp >= 0);
            }
            if (op == "&" || op == "|" || op == "^" || op == "<<" ||
                op == ">>" || op == ">>>") {
                int x = (int)ToNumber(a);
                int y = (int)ToNumber(b);
                if (op == "&") return JsValue::Num((double)(x & y));
                if (op == "|") return JsValue::Num((double)(x | y));
                if (op == "^") return JsValue::Num((double)(x ^ y));
                if (op == "<<") return JsValue::Num((double)(x << (y & 31)));
                if (op == ">>") return JsValue::Num((double)(x >> (y & 31)));
                return JsValue::Num((double)((unsigned)x >> (y & 31)));
            }
            if (op == "in") {
                if (!b.obj) return JsValue::Bool(false);
                return JsValue::Bool(b.obj->props.find(ToString(a)) !=
                                     b.obj->props.end());
            }
            if (op == "instanceof") {
                if (!b.obj || !JsIsCallable(b)) return JsValue::Bool(false);
                if (!a.obj) return JsValue::Bool(false);
                // 优先按原型链判断（自定义构造函数都能正确工作）
                JsValue proto = JsGetProp(*this, b, "prototype");
                if (proto.obj) {
                    for (auto p = a.obj->proto; p; p = p->proto) {
                        if (p == proto.obj) return JsValue::Bool(true);
                    }
                }
                if (b.obj->name == "Array") return JsValue::Bool(a.obj->is_array);
                if (b.obj->name == "Object") return JsValue::Bool(true);
                if (b.obj->name == "Function") return JsValue::Bool(a.obj->is_function);
                return JsValue::Bool(!a.obj->name.empty() &&
                                     a.obj->name == b.obj->name);
            }
            return JsValue::Undef();
        }
        case AstKind::Assign: {
            const AstNode* target = n->kids[0];
            JsValue rhs = EvalExpr(n->kids[1], env);
            if (n->str != "=") {
                // 复合赋值：读旧值、按对应运算符算、再写回
                JsValue old;
                if (target->kind == AstKind::Ident) {
                    JsValue* v = env.Find(target->str);
                    old = v ? *v : JsValue::Undef();
                } else if (target->kind == AstKind::Member) {
                    JsValue base = EvalExpr(target->kids[0], env);
                    std::string key = target->computed
                                          ? ToString(EvalExpr(target->kids[1], env))
                                          : target->str;
                    old = JsGetProp(*this, base, key);
                }
                std::string bop = n->str.substr(0, n->str.size() - 1);
                if (bop == "+") {
                    JsValue pa = ToPrimitive(old);
                    JsValue pb = ToPrimitive(rhs);
                    rhs = (pa.type == JsType::String || pb.type == JsType::String)
                              ? JsValue::Str(ToString(pa) + ToString(pb))
                              : JsValue::Num(ToNumber(pa) + ToNumber(pb));
                } else if (bop == "-") rhs = JsValue::Num(ToNumber(old) - ToNumber(rhs));
                else if (bop == "*") rhs = JsValue::Num(ToNumber(old) * ToNumber(rhs));
                else if (bop == "/") rhs = JsValue::Num(ToNumber(old) / ToNumber(rhs));
                else if (bop == "%") rhs = JsValue::Num(std::fmod(ToNumber(old), ToNumber(rhs)));
                else if (bop == "&") rhs = JsValue::Num((double)((int)ToNumber(old) & (int)ToNumber(rhs)));
                else if (bop == "|") rhs = JsValue::Num((double)((int)ToNumber(old) | (int)ToNumber(rhs)));
                else if (bop == "^") rhs = JsValue::Num((double)((int)ToNumber(old) ^ (int)ToNumber(rhs)));
                else if (bop == "<<") rhs = JsValue::Num((double)((int)ToNumber(old) << ((int)ToNumber(rhs) & 31)));
                else if (bop == ">>") rhs = JsValue::Num((double)((int)ToNumber(old) >> ((int)ToNumber(rhs) & 31)));
                else if (bop == ">>>") rhs = JsValue::Num((double)((unsigned)ToNumber(old) >> ((int)ToNumber(rhs) & 31)));
            }
            if (target->kind == AstKind::Ident) {
                env.Assign(target->str, rhs);
                return rhs;
            }
            if (target->kind == AstKind::Member) {
                JsValue base = EvalExpr(target->kids[0], env);
                std::string key = target->computed
                                      ? ToString(EvalExpr(target->kids[1], env))
                                      : target->str;
                JsSetProp(*this, base, key, rhs);
                return rhs;
            }
            return rhs;
        }
        case AstKind::Conditional:
            return ToBool(EvalExpr(n->kids[0], env))
                       ? EvalExpr(n->kids[1], env)
                       : EvalExpr(n->kids[2], env);
        default:
            return JsValue::Undef();
    }
}

void Interp::EvalStmt(const AstNode* n, Env& env) {
    if (!n) return;
    BumpSteps(1);
    switch (n->kind) {
        case AstKind::Empty: return;
        case AstKind::ExprStmt:
            EvalExpr(n->kids[0], env);
            return;
        case AstKind::Block: {
            // 不做块级作用域：整块就在当前环境里执行。
            // 早先的实现给块建了一个局部 Env、并把外层 Env 按值拷贝当父作用域，
            // 于是块内读到的是外层变量的副本：for 的循环变量永远停在初值、
            // while 里的 i++ 改不到真变量（表现为死循环直到步数超限）。
            // 取舍：let/const 因此等价于 var（README 里列为已知偏差）。
            for (const AstNode* s : n->kids) EvalStmt(s, env);
            return;
        }
        case AstKind::Var: {
            for (size_t i = 0; i < n->names.size(); ++i) {
                JsValue v = (i < n->kids.size() && n->kids[i])
                                ? EvalExpr(n->kids[i], env)
                                : JsValue::Undef();
                env.vars[n->names[i]] = v;
            }
            return;
        }
        case AstKind::FunctionDecl: {
            auto fn = std::make_shared<JsObject>();
            fn->is_function = true;
            fn->name = n->str;
            fn->params = n->names;
            if (!n->kids.empty() && n->kids[0] &&
                n->kids[0]->kind == AstKind::Block) {
                fn->body = n->kids[0]->kids;
            }
            fn->closure = std::make_shared<Env>(env);
            EnsurePrototype(*this, fn);
            env.vars[n->str] = JsValue::Obj(std::move(fn));
            return;
        }
        case AstKind::Return: {
            JsValue v = (n->kids.empty() || !n->kids[0])
                            ? JsValue::Undef()
                            : EvalExpr(n->kids[0], env);
            throw JsReturn{v};
        }
        case AstKind::If: {
            if (ToBool(EvalExpr(n->kids[0], env))) {
                EvalStmt(n->kids[1], env);
            } else if (n->kids.size() > 2 && n->kids[2]) {
                EvalStmt(n->kids[2], env);
            }
            return;
        }
        case AstKind::While: {
            while (ToBool(EvalExpr(n->kids[0], env))) {
                BumpSteps(1);
                try {
                    EvalStmt(n->kids[1], env);
                } catch (JsBreak&) {
                    break;
                } catch (JsContinue&) {
                    continue;
                }
            }
            return;
        }
        case AstKind::DoWhile: {
            do {
                BumpSteps(1);
                try {
                    EvalStmt(n->kids[0], env);
                } catch (JsBreak&) {
                    break;
                } catch (JsContinue&) {
                }
            } while (ToBool(EvalExpr(n->kids[1], env)));
            return;
        }
        case AstKind::For: {
            Env loop;
            loop.parent = std::make_shared<Env>(env);
            if (n->kids[0]) EvalStmt(n->kids[0], loop);
            for (;;) {
                BumpSteps(1);
                if (n->kids[1] && !ToBool(EvalExpr(n->kids[1], loop))) break;
                try {
                    EvalStmt(n->kids[3], loop);
                } catch (JsBreak&) {
                    break;
                } catch (JsContinue&) {
                }
                if (n->kids[2]) EvalExpr(n->kids[2], loop);
            }
            return;
        }
        case AstKind::ForIn: {
            JsValue obj = EvalExpr(n->kids[0], env);
            bool is_of = n->str == "of";
            std::vector<JsValue> items;
            if (is_of) {
                if (obj.type == JsType::String) {
                    // 按码点遍历字符串
                    size_t i = 0;
                    while (i < obj.str.size()) {
                        unsigned char c = (unsigned char)obj.str[i];
                        size_t len = 1;
                        if (c >= 0xF0) len = 4;
                        else if (c >= 0xE0) len = 3;
                        else if (c >= 0xC0) len = 2;
                        if (i + len > obj.str.size()) len = 1;
                        items.push_back(JsValue::Str(obj.str.substr(i, len)));
                        i += len;
                    }
                } else {
                    double n = JsLengthOf(*this, obj);
                    for (double i = 0; i < n; ++i) {
                        items.push_back(JsGetIndex(*this, obj, i));
                    }
                }
            } else {
                if (obj.obj) {
                    for (const auto& kv : obj.obj->props) {
                        if (!is_of && kv.first != "length") {
                            items.push_back(JsValue::Str(kv.first));
                        }
                    }
                }
            }
            for (const JsValue& v : items) {
                BumpSteps(1);
                env.vars[n->names[0]] = is_of ? v : v;
                try {
                    EvalStmt(n->kids[1], env);
                } catch (JsBreak&) {
                    break;
                } catch (JsContinue&) {
                    continue;
                }
            }
            return;
        }
        case AstKind::Break: throw JsBreak{};
        case AstKind::Continue: throw JsContinue{};
        case AstKind::Throw: {
            JsValue v = EvalExpr(n->kids[0], env);
            throw JsThrow{v};
        }
        case AstKind::Try: {
            bool rethrow = false;
            JsValue pending;
            try {
                try {
                    EvalStmt(n->kids[0], env);
                } catch (JsThrow& t) {
                    if (n->kids.size() > 1 && n->kids[1]) {
                        // catch 变量临时绑到当前作用域（没有块级作用域），
                        // 执行完恢复原绑定，避免污染同名变量。
                        const std::string& pname =
                            n->names.empty() ? std::string() : n->names[0];
                        bool had = false;
                        JsValue saved;
                        if (!pname.empty()) {
                            auto it2 = env.vars.find(pname);
                            if (it2 != env.vars.end()) {
                                had = true;
                                saved = it2->second;
                            }
                            env.vars[pname] = t.value;
                        }
                        EvalStmt(n->kids[1], env);
                        if (!pname.empty()) {
                            if (had) {
                                env.vars[pname] = saved;
                            } else {
                                env.vars.erase(pname);
                            }
                        }
                    } else {
                        rethrow = true;
                        pending = t.value;
                    }
                }
            } catch (...) {
                if (n->kids.size() > 2 && n->kids[2]) EvalStmt(n->kids[2], env);
                throw;
            }
            if (n->kids.size() > 2 && n->kids[2]) EvalStmt(n->kids[2], env);
            if (rethrow) throw JsThrow{pending};
            return;
        }
        case AstKind::Switch: {
            JsValue disc = EvalExpr(n->kids[0], env);
            int match = -1;
            int def = -1;
            for (size_t i = 1; i < n->kids.size(); ++i) {
                const AstNode* c = n->kids[i];
                if (c->kids.empty() || !c->kids[0]) {
                    def = (int)i;
                    continue;
                }
                if (match < 0 &&
                    JsStrictEquals(disc, EvalExpr(c->kids[0], env))) {
                    match = (int)i;
                }
            }
            if (match < 0) match = def;
            if (match < 0) return;
            try {
                for (size_t i = (size_t)match; i < n->kids.size(); ++i) {
                    if (n->kids[i]->kids.size() > 1 && n->kids[i]->kids[1]) {
                        EvalStmt(n->kids[i]->kids[1], env);
                    }
                }
            } catch (JsBreak&) {
            }
            return;
        }
        case AstKind::Program: {
            for (const AstNode* s : n->kids) EvalStmt(s, env);
            return;
        }
        default:
            EvalExpr(n, env);
            return;
    }
}

// ------------------------------------------------------------------ 入口
// 求值一个表达式：内联事件属性（onclick="return false"）与需要"取值"的场景用。
bool Interp::EvalExpression(const std::string& src, JsValue* out) {
    error_.clear();
    exhausted_ = false;
    steps_ = 0;
    std::string perr;
    const AstNode* prog = Parse(src, &perr);
    if (!prog || prog->kids.empty()) {
        error_ = "表达式语法错误: " + perr;
        return false;
    }
    Env* env = envs_.front().get();
    try {
        JsValue last = JsValue::Undef();
        for (const AstNode* s : prog->kids) {
            if (s->kind == AstKind::ExprStmt && s->kids.size() == 1) {
                last = EvalExpr(s->kids[0], *env);
            } else {
                EvalStmt(s, *env);
            }
        }
        if (out) *out = last;
        return true;
    } catch (JsThrow& t) {
        error_ = ErrorText(t.value);
        return false;
    } catch (const std::exception& e) {
        error_ = std::string("解释器内部错误: ") + e.what();
        return false;
    }
}

bool Interp::RunScript(const std::string& code, const std::string& name) {
    error_.clear();
    exhausted_ = false;
    steps_ = 0;
    if (code.empty()) return true;
    std::string perr;
    const AstNode* prog = Parse(code, &perr);
    if (!prog) {
        error_ = (name.empty() ? std::string("脚本") : name) + " 语法错误: " + perr;
        NoteScript(false, 0, error_);
        return false;
    }
    Env* env = envs_.front().get();
    // 顶层 var / function 也要提升，否则“先调用后声明”的页面会直接报错
    for (const AstNode* s : prog->kids) {
        if (!s) continue;
        if (s->kind == AstKind::FunctionDecl) {
            auto fn = std::make_shared<JsObject>();
            fn->is_function = true;
            fn->name = s->str;
            fn->params = s->names;
            if (!s->kids.empty() && s->kids[0] &&
                s->kids[0]->kind == AstKind::Block) {
                fn->body = s->kids[0]->kids;
            }
            fn->closure = envs_.front();
            EnsurePrototype(*this, fn);
            env->vars[s->str] = JsValue::Obj(std::move(fn));
        } else if (s->kind == AstKind::Var) {
            for (const std::string& nm : s->names) {
                if (env->vars.find(nm) == env->vars.end()) {
                    env->vars[nm] = JsValue::Undef();
                }
            }
        }
    }
    try {
        for (const AstNode* s : prog->kids) EvalStmt(s, *env);
    } catch (JsThrow& t) {
        error_ = (name.empty() ? std::string("脚本") : name) + " 抛出异常: " +
                 ErrorText(t.value);
        NoteScript(false, steps_, error_);
        return false;
    } catch (JsReturn&) {
        // 顶层 return：当正常结束
    } catch (JsBreak&) {
    } catch (JsContinue&) {
    } catch (const std::exception& e) {
        error_ = std::string("解释器内部错误: ") + e.what();
        NoteScript(false, steps_, error_);
        return false;
    }
    NoteScript(true, steps_, "");
    return true;
}

}  // namespace zb
