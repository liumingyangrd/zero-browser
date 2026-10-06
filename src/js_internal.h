#pragma once




#include "js.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zb {


struct Env {
    std::shared_ptr<Env> parent;
    
    
    
    
    
    
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


struct JsThrow {
    JsValue value;
};
struct JsReturn {
    JsValue value;
};
struct JsBreak {};
struct JsContinue {};


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



double JsLengthOf(Interp& it, const JsValue& v);
JsValue JsGetIndex(Interp& it, const JsValue& v, double i);
void JsSetIndex(Interp& it, const JsValue& v, double i, const JsValue& val);


std::string JsNumberToString(double d);

double JsStringToNumber(const std::string& s);

bool JsIsArrayIndex(const std::string& key, double* out);




struct JsMethodDef {
    const char* name;
    NativeFn fn;
};
const JsMethodDef* JsStringMethods(size_t* count);
const JsMethodDef* JsArrayMethods(size_t* count);
const JsMethodDef* JsNumberMethods(size_t* count);
const JsMethodDef* JsObjectMethods(size_t* count);
const JsMethodDef* JsFunctionMethods(size_t* count);

JsValue JsMethodTableObject(const JsMethodDef* defs, size_t n);

}  
