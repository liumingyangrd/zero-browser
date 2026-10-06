#pragma once

// 解释器内部共享声明：作用域链、控制流异常、以及 js_eval.cpp / js_builtins.cpp
// 之间必须共用的一批小工具。对外（DOM 绑定层）只需要 js.h。

#include "js.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zb {

// 作用域链。vars 是词法绑定，this_val 单独存（不放进 vars，避免与用户变量重名）。
struct Env {
    std::shared_ptr<Env> parent;
    // 变量表用 shared_ptr 持有、对外暴露成引用成员 vars：
    // 于是"拷贝一个 Env"= 同一作用域的另一个句柄（变量表共享），而不是快照。
    // 这是必须的 —— 闭包要捕获外层作用域的**活**绑定，否则
    //   var total = 0; arr.forEach(function (x) { total += x; });
    // 里的 total 永远是 0；for 循环体里对外层变量的赋值同理。
    // 需要全新作用域时（函数调用）用默认构造，它会新建一张表。
    std::shared_ptr<std::map<std::string, JsValue>> vars_owner;
    std::map<std::string, JsValue>& vars;
    bool is_function_scope = false;
    JsValue this_val;
    bool has_this = false;

    Env()
        : vars_owner(std::make_shared<std::map<std::string, JsValue>>()),
          vars(*vars_owner) {}

    JsValue* Find(const std::string& name) {
        for (Env* e = this; e; e = e->parent.get()) {
            auto it = e->vars.find(name);
            if (it != e->vars.end()) return &it->second;
        }
        return nullptr;
    }
    // 赋值：写到最近的已有绑定；都没有就落到当前（最内层）环境，
    // 非严格模式下等价于创建全局变量。
    void Assign(const std::string& name, const JsValue& v) {
        for (Env* e = this; e; e = e->parent.get()) {
            auto it = e->vars.find(name);
            if (it != e->vars.end()) {
                it->second = v;
                return;
            }
        }
        vars[name] = v;
    }
};

// 用 C++ 异常传递 JS 控制流：实现最紧凑，且天然支持嵌套 try/catch/finally。
struct JsThrow {
    JsValue value;
};
struct JsReturn {
    JsValue value;
};
struct JsBreak {};
struct JsContinue {};

// ---------------------------------------------------------------- 内部工具
[[noreturn]] void JsThrowError(Interp& it, const std::string& msg);
std::string JsTypeOf(const JsValue& v);
bool JsStrictEquals(const JsValue& a, const JsValue& b);
bool JsLooseEquals(Interp& it, const JsValue& a, const JsValue& b);
bool JsIsCallable(const JsValue& v);

JsValue JsCall(Interp& it, const JsValue& fn, const JsValue& this_val,
               const std::vector<JsValue>& args);
JsValue JsGetProp(Interp& it, const JsValue& base, const std::string& key);
void JsSetProp(Interp& it, const JsValue& base, const std::string& key,
               const JsValue& v);

// 数组与类数组（arguments / 类数组对象）统一读取：内置数组方法一律以 this 为
// 操作对象，因此 `Array.prototype.slice.call(arguments)` 这类写法也能用。
double JsLengthOf(Interp& it, const JsValue& v);
JsValue JsGetIndex(Interp& it, const JsValue& v, double i);
void JsSetIndex(Interp& it, const JsValue& v, double i, const JsValue& val);

// 数字转字符串：整数值不带 ".0"，与 JS 一致。
std::string JsNumberToString(double d);
// 字符串转数字（ToNumber 规则），失败返回 NaN。
double JsStringToNumber(const std::string& s);
// "0" / "12" 这类数组下标判断。
bool JsIsArrayIndex(const std::string& key, double* out);

// ------------------------------------------------- 内置方法表（按接收者类型）
// 用"按接收者类型分派"代替原型链：`"abc".toUpperCase()` 与
// `Array.prototype.slice.call(arguments)` 都能工作，因为方法一律以 this 为操作对象。
struct JsMethodDef {
    const char* name;
    NativeFn fn;
};
const JsMethodDef* JsStringMethods(size_t* count);
const JsMethodDef* JsArrayMethods(size_t* count);
const JsMethodDef* JsNumberMethods(size_t* count);
const JsMethodDef* JsObjectMethods(size_t* count);
const JsMethodDef* JsFunctionMethods(size_t* count);
// 用方法表构造一个"原型对象"（Array.prototype 等）。
JsValue JsMethodTableObject(const JsMethodDef* defs, size_t n);

}  // namespace zb
