#pragma once

// 自研 JavaScript 子集解释器。
//
// 设计目标与边界（与项目"渲染内核自研"的定位一致，不引入 V8 / QuickJS / Duktape
// 等第三方引擎）：
//   * 面向真实站点常见脚本：DOM 查询与改写、事件回调、定时器、JSON、字符串/数组处理；
//   * 不追求完整语言规范，实现不到的地方在 README 里明确列为未实现；
//   * 任何脚本错误都必须被隔离在本层：不能让浏览器崩溃，也不能被死循环卡死
//     （有步数预算 + 单脚本异常捕获）。
//
// 已知取舍（README 踩坑条目里也有记录）：
//   * 对象用 shared_ptr 引用计数，环形引用要等整页销毁才回收（不实现 GC）；
//   * 没有原型链，改成"按接收者类型分派方法表"；但 `Array.prototype.slice.call(x)`
//     这类常见写法仍可用，因为内置数组方法一律以 this 为操作对象；
//   * 字符串按 UTF-8 字节存储，length/下标是字节语义（真机是 UTF-16），
//     但所有切片操作都会吸附到码点边界，不会切出非法 UTF-8；
//   * 没有正则表达式、Promise、generator。

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zb {

class Interp;
struct AstNode;
struct Env;
struct JsObject;

// ---------------------------------------------------------------- 宿主桥接
// 解释器不认识 DOM。宿主对象（元素、document、window、style、event…）的属性读写与
// 方法调用全部转给 JsBridge，由 js_dom.cpp 实现。
class JsBridge {
public:
    virtual ~JsBridge() = default;
    // 读属性。不存在返回 false（解释器再去看 own props）。
    virtual bool HostGet(void* host, const std::string& kind,
                         const std::string& key, struct JsValue* out) = 0;
    // 写属性。不支持写返回 false。
    virtual bool HostSet(void* host, const std::string& kind,
                         const std::string& key, const struct JsValue& v) = 0;
    // ToString / 模板拼接用。
    virtual std::string HostToString(void* host, const std::string& kind) = 0;
};

// -------------------------------------------------------------------- 值模型
enum class JsType {
    Undefined,
    Null,
    Bool,
    Number,
    String,
    Object,    // 普通对象与数组
    Function,  // 用户函数 / 内置函数
    Host,      // 宿主对象，属性访问走 JsBridge
};

// 原生（C++）函数签名。Interp& 用于回调解释器做类型转换和调用。
using NativeFn = std::function<bool(Interp&, void*, const std::string&,
                                    const std::vector<struct JsValue>&,
                                    const struct JsValue&,
                                    struct JsValue*)>;

struct JsValue {
    JsType type = JsType::Undefined;
    bool b = false;
    double num = 0;
    std::string str;
    std::shared_ptr<JsObject> obj;

    static JsValue Undef() { return JsValue(); }
    static JsValue Null() {
        JsValue v;
        v.type = JsType::Null;
        return v;
    }
    static JsValue Bool(bool x) {
        JsValue v;
        v.type = JsType::Bool;
        v.b = x;
        return v;
    }
    static JsValue Num(double x) {
        JsValue v;
        v.type = JsType::Number;
        v.num = x;
        return v;
    }
    static JsValue Str(const std::string& x) {
        JsValue v;
        v.type = JsType::String;
        v.str = x;
        return v;
    }
    static JsValue Obj(std::shared_ptr<JsObject> o);
    static JsValue Host(void* h, const std::string& kind, const std::string& name);

    bool IsNullish() const {
        return type == JsType::Undefined || type == JsType::Null;
    }
    bool IsObjectLike() const {
        return type == JsType::Object || type == JsType::Function ||
               type == JsType::Host;
    }
};

struct JsObject {
    std::map<std::string, JsValue> props;
    // 简易原型链：只用于属性回退（方法查找）与 instanceof。
    // 不实现属性描述符、getter/setter、原型上的赋值拦截。
    std::shared_ptr<JsObject> proto;
    // 数组：元素同时存进 props（"0","1",…），length 单独维护。
    bool is_array = false;
    double length = 0;

    // 函数
    bool is_function = false;
    std::string name;
    std::vector<std::string> params;
    // 函数体语句序列。语法树归 Interp 的 arena 所有，解析完成后不再修改，
    // 因此这里存非 const 指针即可（也避免 vector<const T*> 的转换麻烦）。
    std::vector<AstNode*> body;
    std::shared_ptr<Env> closure;
    bool is_arrow = false;

    // 内置函数
    bool is_native = false;
    NativeFn native;

    // 宿主对象
    bool is_host = false;
    void* host = nullptr;
    std::string host_kind;
};

// ---------------------------------------------------------------- 语法树
enum class AstKind {
    // 表达式
    Num, Str, Bool, Null, Undefined, Ident, This,
    Array, Object, Member, Call, New, Unary, Update, Binary, Logical,
    Assign, Conditional, Function, Sequence,
    // 语句
    Var, FunctionDecl, Return, If, For, ForIn, While, DoWhile, Block,
    ExprStmt, Break, Continue, Throw, Try, Switch, Empty, Program,
};

struct AstNode {
    AstKind kind = AstKind::Empty;
    std::string str;   // 标识符名 / 运算符
    double num = 0;    // 数字字面量
    // 子节点含义按 kind 约定，见 js.cpp 里的构造点。
    std::vector<AstNode*> kids;
    // Object 字面量 / 解构等：属性名与计算标志。
    std::vector<std::string> names;
    std::vector<char> flags;
    bool computed = false;
};

// ------------------------------------------------------------------ 解释器
class Interp {
public:
    explicit Interp(JsBridge* bridge = nullptr);
    ~Interp();

    void SetBridge(JsBridge* b) { bridge_ = b; }

    // 执行一段脚本。返回 false 表示抛出了未捕获异常（错误文本见 error()），
    // 宿主必须把这个失败当成"页面里这段脚本没跑成功"，不能中断渲染。
    bool RunScript(const std::string& code, const std::string& name);
    // 求值一个表达式并返回它的值（内联事件属性 onclick="..." 需要）。
    bool EvalExpression(const std::string& src, JsValue* out);
    const std::string& error() const { return error_; }
    void ClearError() { error_.clear(); }

    std::shared_ptr<JsObject> Global() const { return global_; }
    // 半宿主的全局对象（window）由 DOM 层安装。
    void AddGlobal(const std::string& name, const JsValue& v);
    bool GetGlobal(const std::string& name, JsValue* out) const;

    JsValue NewObject();
    JsValue NewArray();
    JsValue NewArray(const std::vector<JsValue>& items);
    JsValue MakeNative(const std::string& name, NativeFn fn);
    JsValue MakeHost(void* host, const std::string& kind,
                     const std::string& name = "");

    // 类型转换（宿主层也要用：取 input.value、拼错误信息等）。
    std::string ToString(const JsValue& v);
    double ToNumber(const JsValue& v);
    bool ToBool(const JsValue& v);
    JsValue ToPrimitive(const JsValue& v);

    // 调用任意可调用值；宿主用它派发事件处理器。
    bool CallFunction(const JsValue& fn, const JsValue& this_val,
                      const std::vector<JsValue>& args, JsValue* out);

    // 属性读写（宿主层用于读写事件对象等的通用路径）。
    JsValue GetProp(const JsValue& base, const std::string& key);
    void SetProp(const JsValue& base, const std::string& key,
                 const JsValue& v);

    // 辅助：造错误值、判断可调用。
    JsValue MakeError(const std::string& message);
    bool IsCallable(const JsValue& v) const;

    // 步数预算：超预算抛"执行时间过长"，防止 while(true) 卡死 UI。
    void SetStepLimit(long long n) { step_limit_ = n; }
    long long Steps() const { return steps_; }
    void ResetSteps() { steps_ = 0; }
    bool LastRunExhausted() const { return exhausted_; }

    // 语法树节点：语法分析器要用，因此公开（仅供 js.cpp 内部使用）。
    AstNode* NewNode(AstKind k);

    // 解释器内部接口：js_eval.cpp / js_builtins.cpp 之间共享，DOM 层不要直接用。
    void BumpSteps(long long n = 1);
    struct JsValue EvalExpr(const AstNode* n, Env& env);
    void EvalStmt(const AstNode* n, Env& env);
    JsBridge* Bridge() const { return bridge_; }
    // 调用深度护栏：JS 递归在实现上就是 C++ 递归，不设上限会被
    // `function f(){ f(); } f();` 一条语句直接爆栈（进程级崩溃，不是脚本错误）。
    // 每层 JS 调用要吃掉若干个 C++ 栈帧，所以 build.bat 里同时把主线程栈加到 8MB
    // （-Wl,--stack,8388608）；这个上限是按那个栈大小留了充足余量定的。
    bool EnterCall() {
        if (call_depth_ >= kMaxCallDepth) return false;
        ++call_depth_;
        return true;
    }
    void LeaveCall() {
        if (call_depth_ > 0) --call_depth_;
    }
    static const int kMaxCallDepth = 200;

    // 错误文本：绝不回调 JS（详见 js_eval.cpp 里 BumpSteps 的注释）。
    std::string ErrorText(const JsValue& v);

    // 诊断：最近一次脚本执行统计。
    struct Stats {
        int scripts = 0;
        int failed = 0;
        long long steps = 0;
        std::string last_error;
    };
    const Stats& stats() const { return stats_; }
    // 统计累加点由 js_eval.cpp 使用。
    void NoteScript(bool ok, long long steps, const std::string& err);

private:
    friend struct Eval;
    JsBridge* bridge_ = nullptr;
    std::shared_ptr<JsObject> global_;
    std::string error_;
    long long steps_ = 0;
    long long step_limit_ = 40000000;
    int call_depth_ = 0;
    bool exhausted_ = false;
    Stats stats_;

    // 语法树与函数体共用同一块 arena，保证 AstNode* 在整个 Interp 生命周期内稳定。
    std::vector<std::unique_ptr<AstNode>> arena_;
    std::vector<std::shared_ptr<Env>> envs_;

    const AstNode* Parse(const std::string& code, std::string* err);
    void InstallBuiltins();
};

}  // namespace zb
