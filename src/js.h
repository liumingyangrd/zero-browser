#pragma once


















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




class JsBridge {
public:
    virtual ~JsBridge() = default;
    
    virtual bool HostGet(void* host, const std::string& kind,
                         const std::string& key, struct JsValue* out) = 0;
    
    virtual bool HostSet(void* host, const std::string& kind,
                         const std::string& key, const struct JsValue& v) = 0;
    
    virtual std::string HostToString(void* host, const std::string& kind) = 0;
};


enum class JsType {
    Undefined,
    Null,
    Bool,
    Number,
    String,
    Object,    
    Function,  
    Host,      
};


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
    
    
    std::shared_ptr<JsObject> proto;
    
    bool is_array = false;
    double length = 0;

    
    bool is_function = false;
    std::string name;
    std::vector<std::string> params;
    
    
    std::vector<AstNode*> body;
    std::shared_ptr<Env> closure;
    bool is_arrow = false;

    
    bool is_native = false;
    NativeFn native;

    
    bool is_host = false;
    void* host = nullptr;
    std::string host_kind;
};


enum class AstKind {
    
    Num, Str, Bool, Null, Undefined, Ident, This,
    Array, Object, Member, Call, New, Unary, Update, Binary, Logical,
    Assign, Conditional, Function, Sequence,
    
    Var, FunctionDecl, Return, If, For, ForIn, While, DoWhile, Block,
    ExprStmt, Break, Continue, Throw, Try, Switch, Empty, Program,
};

struct AstNode {
    AstKind kind = AstKind::Empty;
    std::string str;   
    double num = 0;    
    
    std::vector<AstNode*> kids;
    
    std::vector<std::string> names;
    std::vector<char> flags;
    bool computed = false;
};


class Interp {
public:
    explicit Interp(JsBridge* bridge = nullptr);
    ~Interp();

    void SetBridge(JsBridge* b) { bridge_ = b; }

    
    
    bool RunScript(const std::string& code, const std::string& name);
    
    bool EvalExpression(const std::string& src, JsValue* out);
    const std::string& error() const { return error_; }
    void ClearError() { error_.clear(); }

    std::shared_ptr<JsObject> Global() const { return global_; }
    
    void AddGlobal(const std::string& name, const JsValue& v);
    bool GetGlobal(const std::string& name, JsValue* out) const;

    JsValue NewObject();
    JsValue NewArray();
    JsValue NewArray(const std::vector<JsValue>& items);
    JsValue MakeNative(const std::string& name, NativeFn fn);
    JsValue MakeHost(void* host, const std::string& kind,
                     const std::string& name = "");

    
    std::string ToString(const JsValue& v);
    double ToNumber(const JsValue& v);
    bool ToBool(const JsValue& v);
    JsValue ToPrimitive(const JsValue& v);

    
    bool CallFunction(const JsValue& fn, const JsValue& this_val,
                      const std::vector<JsValue>& args, JsValue* out);

    
    JsValue GetProp(const JsValue& base, const std::string& key);
    void SetProp(const JsValue& base, const std::string& key,
                 const JsValue& v);

    
    JsValue MakeError(const std::string& message);
    bool IsCallable(const JsValue& v) const;

    
    void SetStepLimit(long long n) { step_limit_ = n; }
    long long Steps() const { return steps_; }
    void ResetSteps() { steps_ = 0; }
    bool LastRunExhausted() const { return exhausted_; }

    
    AstNode* NewNode(AstKind k);

    
    void BumpSteps(long long n = 1);
    struct JsValue EvalExpr(const AstNode* n, Env& env);
    void EvalStmt(const AstNode* n, Env& env);
    JsBridge* Bridge() const { return bridge_; }
    
    
    
    
    bool EnterCall() {
        if (call_depth_ >= kMaxCallDepth) return false;
        ++call_depth_;
        return true;
    }
    void LeaveCall() {
        if (call_depth_ > 0) --call_depth_;
    }
    static const int kMaxCallDepth = 200;

    
    std::string ErrorText(const JsValue& v);

    
    struct Stats {
        int scripts = 0;
        int failed = 0;
        long long steps = 0;
        std::string last_error;
    };
    const Stats& stats() const { return stats_; }
    
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

    
    std::vector<std::unique_ptr<AstNode>> arena_;
    std::vector<std::shared_ptr<Env>> envs_;

    const AstNode* Parse(const std::string& code, std::string* err);
    void InstallBuiltins();
};

}  
