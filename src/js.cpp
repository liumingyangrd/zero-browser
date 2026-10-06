


#include "js.h"

#include "common.h"
#include "js_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace zb {

namespace {


enum class Tok {
    End, Ident, Num, Str, Punct, Keyword,
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    double num = 0;
    bool nl_before = false;   
    size_t pos = 0;
};

bool IsIdentStart(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$' || c >= 0x80;
}
bool IsIdentPart(unsigned char c) {
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}
bool IsDigit(unsigned char c) { return c >= '0' && c <= '9'; }

const char* kKeywords[] = {
    "var", "let", "const", "function", "return", "if", "else", "for", "while",
    "do", "break", "continue", "new", "delete", "typeof", "instanceof", "in",
    "this", "null", "true", "false", "undefined", "throw", "try", "catch",
    "finally", "switch", "case", "default", "void", "of", "debugger",
};

bool IsKeyword(const std::string& s) {
    for (const char* k : kKeywords) {
        if (s == k) return true;
    }
    return false;
}


const char* kPuncts[] = {
    ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "=>", "==", "!=",
    "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=",
    "%=", "&=", "|=", "^=", "**", "<<", ">>", "{", "}", "(", ")", "[", "]",
    ";", ",", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^", "!", "~",
    "?", ":", "=", ".",
};

struct Lexer {
    const std::string& src;
    size_t i = 0;
    std::string err;

    explicit Lexer(const std::string& s) : src(s) {}

    void SkipSpace(bool* saw_nl) {
        for (;;) {
            while (i < src.size() &&
                   (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' ||
                    src[i] == '\n' || src[i] == '\f' || src[i] == '\v')) {
                if (src[i] == '\n') *saw_nl = true;
                ++i;
            }
            if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
                while (i < src.size() && src[i] != '\n') ++i;
                continue;
            }
            if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
                i += 2;
                while (i + 1 < src.size() &&
                       !(src[i] == '*' && src[i + 1] == '/')) {
                    if (src[i] == '\n') *saw_nl = true;
                    ++i;
                }
                i = (i + 1 < src.size()) ? i + 2 : src.size();
                continue;
            }
            break;
        }
    }

    
    void Utf8Append(std::string* out) {
        unsigned char c = (unsigned char)src[i];
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        if (i + len > src.size()) len = 1;
        out->append(src, i, len);
        i += len;
    }

    Token Next() {
        Token t;
        bool nl = false;
        SkipSpace(&nl);
        t.nl_before = nl;
        t.pos = i;
        if (i >= src.size()) {
            t.kind = Tok::End;
            return t;
        }
        unsigned char c = (unsigned char)src[i];
        
        if (IsIdentStart(c)) {
            std::string id;
            while (i < src.size() && IsIdentPart((unsigned char)src[i])) {
                if ((unsigned char)src[i] >= 0x80) Utf8Append(&id);
                else id.push_back(src[i++]);
            }
            t.text = id;
            t.kind = IsKeyword(id) ? Tok::Keyword : Tok::Ident;
            return t;
        }
        
        if (IsDigit(c) || (c == '.' && i + 1 < src.size() &&
                           IsDigit((unsigned char)src[i + 1]))) {
            size_t start = i;
            if (c == '0' && i + 1 < src.size() &&
                (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                i += 2;
                while (i < src.size() && isxdigit((unsigned char)src[i])) ++i;
                t.num = (double)strtoll(src.substr(start, i - start).c_str(),
                                        nullptr, 16);
            } else {
                while (i < src.size() && IsDigit((unsigned char)src[i])) ++i;
                if (i < src.size() && src[i] == '.') {
                    ++i;
                    while (i < src.size() && IsDigit((unsigned char)src[i])) ++i;
                }
                if (i < src.size() && (src[i] == 'e' || src[i] == 'E')) {
                    size_t save = i;
                    ++i;
                    if (i < src.size() && (src[i] == '+' || src[i] == '-')) ++i;
                    if (i < src.size() && IsDigit((unsigned char)src[i])) {
                        while (i < src.size() && IsDigit((unsigned char)src[i])) ++i;
                    } else {
                        i = save;
                    }
                }
                t.num = strtod(src.substr(start, i - start).c_str(), nullptr);
            }
            t.kind = Tok::Num;
            return t;
        }
        
        if (c == '"' || c == '\'') {
            char quote = (char)c;
            ++i;
            std::string out;
            while (i < src.size() && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    char e = src[++i];
                    switch (e) {
                        case 'n': out.push_back('\n'); ++i; break;
                        case 't': out.push_back('\t'); ++i; break;
                        case 'r': out.push_back('\r'); ++i; break;
                        case 'b': out.push_back('\b'); ++i; break;
                        case 'f': out.push_back('\f'); ++i; break;
                        case 'v': out.push_back('\v'); ++i; break;
                        case '0': out.push_back('\0'); ++i; break;
                        case 'x': {
                            if (i + 2 < src.size()) {
                                int v = (int)strtol(
                                    src.substr(i + 1, 2).c_str(), nullptr, 16);
                                out.push_back((char)v);
                                i += 3;
                            } else {
                                ++i;
                            }
                            break;
                        }
                        case 'u': {
                            if (i + 4 < src.size()) {
                                int cp = (int)strtol(
                                    src.substr(i + 1, 4).c_str(), nullptr, 16);
                                i += 5;
                                
                                if (cp < 0x80) {
                                    out.push_back((char)cp);
                                } else if (cp < 0x800) {
                                    out.push_back((char)(0xC0 | (cp >> 6)));
                                    out.push_back((char)(0x80 | (cp & 0x3F)));
                                } else {
                                    out.push_back((char)(0xE0 | (cp >> 12)));
                                    out.push_back(
                                        (char)(0x80 | ((cp >> 6) & 0x3F)));
                                    out.push_back((char)(0x80 | (cp & 0x3F)));
                                }
                            } else {
                                ++i;
                            }
                            break;
                        }
                        default: out.push_back(e); ++i; break;
                    }
                    continue;
                }
                Utf8Append(&out);
            }
            if (i < src.size()) ++i;  
            t.kind = Tok::Str;
            t.text = out;
            return t;
        }
        
        if (c == '`') {
            ++i;
            std::string out;
            bool interp = false;
            while (i < src.size() && src[i] != '`') {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    char e = src[++i];
                    out.push_back(e == 'n' ? '\n' : (e == 't' ? '\t' : e));
                    ++i;
                    continue;
                }
                if (src[i] == '$' && i + 1 < src.size() && src[i + 1] == '{') {
                    interp = true;
                }
                Utf8Append(&out);
            }
            if (i < src.size()) ++i;
            if (interp) {
                err = "模板字符串插值 ${} 尚未实现";
                t.kind = Tok::End;
                return t;
            }
            t.kind = Tok::Str;
            t.text = out;
            return t;
        }
        
        for (const char* p : kPuncts) {
            size_t n = strlen(p);
            if (src.compare(i, n, p) == 0) {
                t.kind = Tok::Punct;
                t.text = p;
                i += n;
                return t;
            }
        }
        
        ++i;
        return Next();
    }
};


struct Parser {
    std::vector<Token> toks;
    size_t p = 0;
    std::string err;
    Interp* interp = nullptr;

    AstNode* New(AstKind k) { return interp->NewNode(k); }

    
    
    const Token& Cur() const { return toks[std::min(p, toks.size() - 1)]; }
    const Token& Peek(size_t n = 1) const {
        return toks[std::min(p + n, toks.size() - 1)];
    }
    bool IsPunct(const char* s) const {
        return Cur().kind == Tok::Punct && Cur().text == s;
    }
    bool IsKw(const char* s) const {
        return Cur().kind == Tok::Keyword && Cur().text == s;
    }
    bool EatPunct(const char* s) {
        if (IsPunct(s)) {
            ++p;
            return true;
        }
        return false;
    }
    bool EatKw(const char* s) {
        if (IsKw(s)) {
            ++p;
            return true;
        }
        return false;
    }
    bool Fail(const std::string& m) {
        if (err.empty()) {
            
            char buf[160];
            snprintf(buf, sizeof(buf), " (第 %zu 个词法单元, 源码偏移 %zu, 当前词='%s')",
                     p, Cur().pos, Cur().text.c_str());
            err = m + buf;
        }
        return false;
    }

    
    bool Semicolon() {
        if (EatPunct(";")) return true;
        if (Cur().kind == Tok::End || Cur().nl_before) return true;
        if (IsPunct("}")) return true;
        return Fail("缺少分号");
    }

    AstNode* ParseProgram() {
        AstNode* n = New(AstKind::Program);
        while (Cur().kind != Tok::End) {
            AstNode* s = ParseStatement();
            if (!s) return nullptr;
            n->kids.push_back(s);
        }
        return n;
    }

    AstNode* ParseStatement() {
        if (IsPunct("{")) return ParseBlock();
        if (IsPunct(";")) {
            ++p;
            return New(AstKind::Empty);
        }
        if (Cur().kind == Tok::Keyword) {
            const std::string& k = Cur().text;
            if (k == "var" || k == "let" || k == "const") return ParseVar();
            if (k == "function") return ParseFunctionDecl();
            if (k == "if") return ParseIf();
            if (k == "for") return ParseFor();
            if (k == "while") return ParseWhile();
            if (k == "do") return ParseDoWhile();
            if (k == "return") return ParseReturn();
            if (k == "break") { ++p; Semicolon(); return New(AstKind::Break); }
            if (k == "continue") {
                ++p;
                Semicolon();
                return New(AstKind::Continue);
            }
            if (k == "throw") return ParseThrow();
            if (k == "try") return ParseTry();
            if (k == "switch") return ParseSwitch();
            if (k == "debugger") {
                ++p;
                Semicolon();
                return New(AstKind::Empty);
            }
        }
        AstNode* e = ParseExpression();
        if (!e) return nullptr;
        if (!Semicolon()) return nullptr;
        AstNode* n = New(AstKind::ExprStmt);
        n->kids.push_back(e);
        return n;
    }

    AstNode* ParseBlock() {
        if (!EatPunct("{")) {
            Fail("期望 {");
            return nullptr;
        }
        AstNode* n = New(AstKind::Block);
        while (!IsPunct("}") && Cur().kind != Tok::End) {
            AstNode* s = ParseStatement();
            if (!s) return nullptr;
            n->kids.push_back(s);
        }
        if (!EatPunct("}")) {
            Fail("期望 }");
            return nullptr;
        }
        return n;
    }

    AstNode* ParseVar() {
        int kind = IsKw("var") ? 0 : (IsKw("let") ? 1 : 2);
        ++p;
        AstNode* n = New(AstKind::Var);
        for (;;) {
            if (Cur().kind != Tok::Ident) {
                Fail("期望变量名");
                return nullptr;
            }
            n->names.push_back(Cur().text);
            n->flags.push_back((char)kind);
            ++p;
            if (EatPunct("=")) {
                AstNode* init = ParseAssign();
                if (!init) return nullptr;
                n->kids.push_back(init);
            } else {
                n->kids.push_back(nullptr);
            }
            if (!EatPunct(",")) break;
        }
        if (!Semicolon()) return nullptr;
        return n;
    }

    AstNode* ParseFunctionDecl() {
        ++p;
        AstNode* n = New(AstKind::FunctionDecl);
        if (Cur().kind == Tok::Ident) {
            n->str = Cur().text;
            ++p;
        }
        if (!ParseParams(n)) return nullptr;
        AstNode* body = ParseBlock();
        if (!body) return nullptr;
        n->kids.push_back(body);
        return n;
    }

    bool ParseParams(AstNode* fn) {
        if (!EatPunct("(")) {
            Fail("期望 (");
            return false;
        }
        while (!IsPunct(")") && Cur().kind != Tok::End) {
            if (Cur().kind != Tok::Ident) {
                Fail("期望参数名");
                return false;
            }
            fn->names.push_back(Cur().text);
            ++p;
            if (!EatPunct(",")) break;
        }
        if (!EatPunct(")")) {
            Fail("期望 )");
            return false;
        }
        return true;
    }

    AstNode* ParseIf() {
        ++p;
        if (!EatPunct("(")) {
            Fail("期望 (");
            return nullptr;
        }
        AstNode* test = ParseExpression();
        if (!test) return nullptr;
        if (!EatPunct(")")) {
            Fail("期望 )");
            return nullptr;
        }
        AstNode* then = ParseStatement();
        if (!then) return nullptr;
        AstNode* n = New(AstKind::If);
        n->kids.push_back(test);
        n->kids.push_back(then);
        if (EatKw("else")) {
            AstNode* els = ParseStatement();
            if (!els) return nullptr;
            n->kids.push_back(els);
        } else {
            n->kids.push_back(nullptr);
        }
        return n;
    }

    AstNode* ParseFor() {
        ++p;
        if (!EatPunct("(")) {
            Fail("期望 (");
            return nullptr;
        }
        
        size_t save = p;
        bool is_decl = IsKw("var") || IsKw("let") || IsKw("const");
        int decl_kind = IsKw("var") ? 0 : (IsKw("let") ? 1 : 2);
        std::string loop_var;
        bool have_var = false;
        if (is_decl) {
            ++p;
            if (Cur().kind == Tok::Ident) {
                loop_var = Cur().text;
                have_var = true;
                ++p;  
            }
        } else if (Cur().kind == Tok::Ident) {
            loop_var = Cur().text;
            have_var = true;
            ++p;
        }
        if (have_var && (IsKw("in") || IsKw("of"))) {
            std::string mode = Cur().text;
            ++p;
            AstNode* obj = ParseExpression();
            if (!obj) return nullptr;
            if (!EatPunct(")")) {
                Fail("期望 )");
                return nullptr;
            }
            AstNode* body = ParseStatement();
            if (!body) return nullptr;
            AstNode* n = New(AstKind::ForIn);
            n->str = mode;
            n->names.push_back(loop_var);
            n->flags.push_back((char)(is_decl ? decl_kind : -1));
            n->kids.push_back(obj);
            n->kids.push_back(body);
            return n;
        }
        p = save;

        AstNode* init = nullptr;
        if (!IsPunct(";")) {
            if (IsKw("var") || IsKw("let") || IsKw("const")) {
                init = ParseVarNoSemi();
            } else {
                AstNode* e = ParseExpression();
                if (!e) return nullptr;
                init = New(AstKind::ExprStmt);
                init->kids.push_back(e);
            }
            if (!init) return nullptr;
        }
        if (!EatPunct(";")) {
            Fail("期望 ;");
            return nullptr;
        }
        AstNode* test = nullptr;
        if (!IsPunct(";")) {
            test = ParseExpression();
            if (!test) return nullptr;
        }
        if (!EatPunct(";")) {
            Fail("期望 ;");
            return nullptr;
        }
        AstNode* update = nullptr;
        if (!IsPunct(")")) {
            update = ParseExpression();
            if (!update) return nullptr;
        }
        if (!EatPunct(")")) {
            Fail("期望 )");
            return nullptr;
        }
        AstNode* body = ParseStatement();
        if (!body) return nullptr;
        AstNode* n = New(AstKind::For);
        n->kids.push_back(init);
        n->kids.push_back(test);
        n->kids.push_back(update);
        n->kids.push_back(body);
        return n;
    }

    
    AstNode* ParseVarNoSemi() {
        int kind = IsKw("var") ? 0 : (IsKw("let") ? 1 : 2);
        ++p;
        AstNode* n = New(AstKind::Var);
        for (;;) {
            if (Cur().kind != Tok::Ident) {
                Fail("期望变量名");
                return nullptr;
            }
            n->names.push_back(Cur().text);
            n->flags.push_back((char)kind);
            ++p;
            if (EatPunct("=")) {
                AstNode* init = ParseAssign();
                if (!init) return nullptr;
                n->kids.push_back(init);
            } else {
                n->kids.push_back(nullptr);
            }
            if (!EatPunct(",")) break;
        }
        return n;
    }

    AstNode* ParseWhile() {
        ++p;
        if (!EatPunct("(")) {
            Fail("期望 (");
            return nullptr;
        }
        AstNode* test = ParseExpression();
        if (!test) return nullptr;
        if (!EatPunct(")")) {
            Fail("期望 )");
            return nullptr;
        }
        AstNode* body = ParseStatement();
        if (!body) return nullptr;
        AstNode* n = New(AstKind::While);
        n->kids.push_back(test);
        n->kids.push_back(body);
        return n;
    }

    AstNode* ParseDoWhile() {
        ++p;
        AstNode* body = ParseStatement();
        if (!body) return nullptr;
        if (!EatKw("while")) {
            Fail("期望 while");
            return nullptr;
        }
        if (!EatPunct("(")) {
            Fail("期望 (");
            return nullptr;
        }
        AstNode* test = ParseExpression();
        if (!test) return nullptr;
        if (!EatPunct(")")) {
            Fail("期望 )");
            return nullptr;
        }
        Semicolon();
        AstNode* n = New(AstKind::DoWhile);
        n->kids.push_back(body);
        n->kids.push_back(test);
        return n;
    }

    AstNode* ParseReturn() {
        ++p;
        AstNode* n = New(AstKind::Return);
        
        if (Cur().kind == Tok::End || Cur().nl_before || IsPunct(";") ||
            IsPunct("}")) {
            n->kids.push_back(nullptr);
            Semicolon();
            return n;
        }
        AstNode* e = ParseExpression();
        if (!e) return nullptr;
        n->kids.push_back(e);
        Semicolon();
        return n;
    }

    AstNode* ParseThrow() {
        ++p;
        AstNode* e = ParseExpression();
        if (!e) return nullptr;
        Semicolon();
        AstNode* n = New(AstKind::Throw);
        n->kids.push_back(e);
        return n;
    }

    AstNode* ParseTry() {
        ++p;
        AstNode* block = ParseBlock();
        if (!block) return nullptr;
        AstNode* n = New(AstKind::Try);
        n->kids.push_back(block);
        n->names.push_back("");
        n->kids.push_back(nullptr);
        n->kids.push_back(nullptr);
        if (EatKw("catch")) {
            if (EatPunct("(")) {
                if (Cur().kind == Tok::Ident) {
                    n->names[0] = Cur().text;
                    ++p;
                }
                if (!EatPunct(")")) {
                    Fail("期望 )");
                    return nullptr;
                }
            }
            AstNode* c = ParseBlock();
            if (!c) return nullptr;
            n->kids[1] = c;
        }
        if (EatKw("finally")) {
            AstNode* f = ParseBlock();
            if (!f) return nullptr;
            n->kids[2] = f;
        }
        if (n->kids[1] == nullptr && n->kids[2] == nullptr) {
            Fail("try 需要 catch 或 finally");
            return nullptr;
        }
        return n;
    }

    AstNode* ParseSwitch() {
        ++p;
        if (!EatPunct("(")) {
            Fail("期望 (");
            return nullptr;
        }
        AstNode* disc = ParseExpression();
        if (!disc) return nullptr;
        if (!EatPunct(")")) {
            Fail("期望 )");
            return nullptr;
        }
        if (!EatPunct("{")) {
            Fail("期望 {");
            return nullptr;
        }
        AstNode* n = New(AstKind::Switch);
        n->kids.push_back(disc);
        while (!IsPunct("}") && Cur().kind != Tok::End) {
            AstNode* c = New(AstKind::Empty);
            c->kind = AstKind::ExprStmt;  
            if (EatKw("case")) {
                AstNode* t = ParseExpression();
                if (!t) return nullptr;
                c->kids.push_back(t);
            } else if (EatKw("default")) {
                c->kids.push_back(nullptr);
            } else {
                Fail("期望 case / default");
                return nullptr;
            }
            if (!EatPunct(":")) {
                Fail("期望 :");
                return nullptr;
            }
            AstNode* body = New(AstKind::Block);
            while (!IsPunct("}") && !IsKw("case") && !IsKw("default") &&
                   Cur().kind != Tok::End) {
                AstNode* s = ParseStatement();
                if (!s) return nullptr;
                body->kids.push_back(s);
            }
            c->kids.push_back(body);
            n->kids.push_back(c);
        }
        if (!EatPunct("}")) {
            Fail("期望 }");
            return nullptr;
        }
        return n;
    }

    
    AstNode* ParseExpression() {
        AstNode* first = ParseAssign();
        if (!first) return nullptr;
        if (!IsPunct(",")) return first;
        AstNode* n = New(AstKind::Sequence);
        n->kids.push_back(first);
        while (EatPunct(",")) {
            AstNode* e = ParseAssign();
            if (!e) return nullptr;
            n->kids.push_back(e);
        }
        return n;
    }

    AstNode* ParseAssign() {
        
        if (Cur().kind == Tok::Ident && Peek().kind == Tok::Punct &&
            Peek().text == "=>") {
            AstNode* fn = New(AstKind::Function);
            fn->flags.push_back(1);
            fn->names.push_back(Cur().text);
            p += 2;
            return ParseArrowBody(fn);
        }
        if (IsPunct("(")) {
            size_t save = p;
            std::vector<std::string> params;
            ++p;
            bool ok = true;
            if (!IsPunct(")")) {
                for (;;) {
                    if (Cur().kind != Tok::Ident) {
                        ok = false;
                        break;
                    }
                    params.push_back(Cur().text);
                    ++p;
                    if (!EatPunct(",")) break;
                }
            }
            if (ok && EatPunct(")") && IsPunct("=>")) {
                ++p;
                AstNode* fn = New(AstKind::Function);
                fn->flags.push_back(1);
                fn->names = params;
                return ParseArrowBody(fn);
            }
            p = save;
        }

        AstNode* left = ParseConditional();
        if (!left) return nullptr;
        if (Cur().kind == Tok::Punct) {
            const std::string& op = Cur().text;
            if (op == "=" || op == "+=" || op == "-=" || op == "*=" ||
                op == "/=" || op == "%=" || op == "&=" || op == "|=" ||
                op == "^=" || op == "<<=" || op == ">>=" || op == ">>>=") {
                ++p;
                AstNode* right = ParseAssign();
                if (!right) return nullptr;
                AstNode* n = New(AstKind::Assign);
                n->str = op;
                n->kids.push_back(left);
                n->kids.push_back(right);
                return n;
            }
        }
        return left;
    }

    AstNode* ParseArrowBody(AstNode* fn) {
        AstNode* body = nullptr;
        if (IsPunct("{")) {
            body = ParseBlock();
        } else {
            AstNode* e = ParseAssign();
            if (e) {
                body = New(AstKind::Return);
                body->kids.push_back(e);
            }
        }
        if (!body) return nullptr;
        fn->kids.push_back(body);
        return fn;
    }

    AstNode* ParseConditional() {
        AstNode* test = ParseBinary(0);
        if (!test) return nullptr;
        if (!EatPunct("?")) return test;
        AstNode* cons = ParseAssign();
        if (!cons) return nullptr;
        if (!EatPunct(":")) {
            Fail("期望 :");
            return nullptr;
        }
        AstNode* alt = ParseAssign();
        if (!alt) return nullptr;
        AstNode* n = New(AstKind::Conditional);
        n->kids.push_back(test);
        n->kids.push_back(cons);
        n->kids.push_back(alt);
        return n;
    }

    static int Prec(const std::string& op) {
        if (op == "||" || op == "??") return 1;
        if (op == "&&") return 2;
        if (op == "|") return 3;
        if (op == "^") return 4;
        if (op == "&") return 5;
        if (op == "==" || op == "!=" || op == "===" || op == "!==") return 6;
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
        if (op == "in" || op == "instanceof") return 7;
        if (op == "<<" || op == ">>" || op == ">>>") return 8;
        if (op == "+" || op == "-") return 9;
        if (op == "*" || op == "/" || op == "%") return 10;
        if (op == "**") return 11;
        return -1;
    }

    bool CurIsBinOp(std::string* op) const {
        if (Cur().kind == Tok::Punct) {
            if (Prec(Cur().text) > 0) {
                *op = Cur().text;
                return true;
            }
        } else if (Cur().kind == Tok::Keyword &&
                   (Cur().text == "in" || Cur().text == "instanceof")) {
            *op = Cur().text;
            return true;
        }
        return false;
    }

    AstNode* ParseBinary(int min_prec) {
        AstNode* left = ParseUnary();
        if (!left) return nullptr;
        for (;;) {
            std::string op;
            if (!CurIsBinOp(&op)) break;
            int prec = Prec(op);
            if (prec < min_prec) break;
            ++p;
            AstNode* right = ParseBinary(op == "**" ? prec : prec + 1);
            if (!right) return nullptr;
            AstNode* n = New((op == "&&" || op == "||" || op == "??")
                                 ? AstKind::Logical
                                 : AstKind::Binary);
            n->str = op;
            n->kids.push_back(left);
            n->kids.push_back(right);
            left = n;
        }
        return left;
    }

    AstNode* ParseUnary() {
        if (Cur().kind == Tok::Punct) {
            const std::string& op = Cur().text;
            if (op == "-" || op == "+" || op == "!" || op == "~") {
                ++p;
                AstNode* e = ParseUnary();
                if (!e) return nullptr;
                AstNode* n = New(AstKind::Unary);
                n->str = op;
                n->kids.push_back(e);
                return n;
            }
            if (op == "++" || op == "--") {
                ++p;
                AstNode* e = ParseUnary();
                if (!e) return nullptr;
                AstNode* n = New(AstKind::Update);
                n->str = op;
                n->computed = true;  
                n->kids.push_back(e);
                return n;
            }
        }
        if (Cur().kind == Tok::Keyword) {
            const std::string& k = Cur().text;
            if (k == "typeof" || k == "void" || k == "delete" || k == "!") {
                ++p;
                AstNode* e = ParseUnary();
                if (!e) return nullptr;
                AstNode* n = New(AstKind::Unary);
                n->str = k;
                n->kids.push_back(e);
                return n;
            }
        }
        return ParsePostfix();
    }

    AstNode* ParsePostfix() {
        AstNode* e = ParseCallMember();
        if (!e) return nullptr;
        if (Cur().kind == Tok::Punct && !Cur().nl_before &&
            (Cur().text == "++" || Cur().text == "--")) {
            AstNode* n = New(AstKind::Update);
            n->str = Cur().text;
            n->computed = false;  
            n->kids.push_back(e);
            ++p;
            return n;
        }
        return e;
    }

    AstNode* ParseCallMember() {
        AstNode* e = nullptr;
        if (IsKw("new")) {
            ++p;
            AstNode* callee = ParseCallMember();
            if (!callee) return nullptr;
            AstNode* n = New(AstKind::New);
            if (callee->kind == AstKind::Call) {
                n->kids.push_back(callee->kids[0]);
                for (size_t i = 1; i < callee->kids.size(); ++i) {
                    n->kids.push_back(callee->kids[i]);
                }
            } else {
                n->kids.push_back(callee);
            }
            e = n;
        } else {
            e = ParsePrimary();
            if (!e) return nullptr;
        }
        for (;;) {
            if (EatPunct(".")) {
                if (Cur().kind != Tok::Ident && Cur().kind != Tok::Keyword) {
                    Fail("期望属性名");
                    return nullptr;
                }
                AstNode* n = New(AstKind::Member);
                n->str = Cur().text;
                n->computed = false;
                n->kids.push_back(e);
                ++p;
                e = n;
                continue;
            }
            if (EatPunct("[")) {
                AstNode* idx = ParseExpression();
                if (!idx) return nullptr;
                if (!EatPunct("]")) {
                    Fail("期望 ]");
                    return nullptr;
                }
                AstNode* n = New(AstKind::Member);
                n->computed = true;
                n->kids.push_back(e);
                n->kids.push_back(idx);
                e = n;
                continue;
            }
            if (IsPunct("(")) {
                ++p;
                AstNode* n = New(AstKind::Call);
                n->kids.push_back(e);
                while (!IsPunct(")") && Cur().kind != Tok::End) {
                    AstNode* a = ParseAssign();
                    if (!a) return nullptr;
                    n->kids.push_back(a);
                    if (!EatPunct(",")) break;
                }
                if (!EatPunct(")")) {
                    Fail("期望 )");
                    return nullptr;
                }
                e = n;
                continue;
            }
            break;
        }
        return e;
    }

    AstNode* ParsePrimary() {
        const Token& t = Cur();
        switch (t.kind) {
            case Tok::Num: {
                AstNode* n = New(AstKind::Num);
                n->num = t.num;
                ++p;
                return n;
            }
            case Tok::Str: {
                AstNode* n = New(AstKind::Str);
                n->str = t.text;
                ++p;
                return n;
            }
            case Tok::Ident: {
                AstNode* n = New(AstKind::Ident);
                n->str = t.text;
                ++p;
                return n;
            }
            case Tok::Keyword: {
                if (t.text == "true" || t.text == "false") {
                    AstNode* n = New(AstKind::Bool);
                    n->num = t.text == "true" ? 1 : 0;
                    ++p;
                    return n;
                }
                if (t.text == "null") {
                    ++p;
                    return New(AstKind::Null);
                }
                if (t.text == "undefined") {
                    ++p;
                    return New(AstKind::Undefined);
                }
                if (t.text == "this") {
                    ++p;
                    return New(AstKind::This);
                }
                if (t.text == "function") {
                    ++p;
                    AstNode* n = New(AstKind::Function);
                    if (Cur().kind == Tok::Ident) {
                        n->str = Cur().text;
                        ++p;
                    }
                    if (!ParseParams(n)) return nullptr;
                    AstNode* body = ParseBlock();
                    if (!body) return nullptr;
                    n->kids.push_back(body);
                    return n;
                }
                break;
            }
            case Tok::Punct: {
                if (t.text == "(") {
                    ++p;
                    AstNode* e = ParseExpression();
                    if (!e) return nullptr;
                    if (!EatPunct(")")) {
                        Fail("期望 )");
                        return nullptr;
                    }
                    return e;
                }
                if (t.text == "[") {
                    ++p;
                    AstNode* n = New(AstKind::Array);
                    while (!IsPunct("]") && Cur().kind != Tok::End) {
                        if (IsPunct(",")) {
                            n->kids.push_back(New(AstKind::Undefined));
                            ++p;
                            continue;
                        }
                        AstNode* e = ParseAssign();
                        if (!e) return nullptr;
                        n->kids.push_back(e);
                        if (!EatPunct(",")) break;
                    }
                    if (!EatPunct("]")) {
                        Fail("期望 ]");
                        return nullptr;
                    }
                    return n;
                }
                if (t.text == "{") {
                    ++p;
                    AstNode* n = New(AstKind::Object);
                    while (!IsPunct("}") && Cur().kind != Tok::End) {
                        std::string key;
                        bool computed = false;
                        if (Cur().kind == Tok::Str) {
                            key = Cur().text;
                            ++p;
                        } else if (Cur().kind == Tok::Num) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%g", Cur().num);
                            key = buf;
                            ++p;
                        } else if (Cur().kind == Tok::Ident ||
                                   Cur().kind == Tok::Keyword) {
                            key = Cur().text;
                            ++p;
                        } else if (IsPunct("[")) {
                            computed = true;
                            ++p;
                            AstNode* k = ParseAssign();
                            if (!k) return nullptr;
                            if (!EatPunct("]")) {
                                Fail("期望 ]");
                                return nullptr;
                            }
                            n->names.push_back("");
                            n->kids.push_back(k);
                            n->flags.push_back(1);
                            
                            if (!EatPunct(":")) {
                                Fail("期望 :");
                                return nullptr;
                            }
                            AstNode* v = ParseAssign();
                            if (!v) return nullptr;
                            n->names.push_back("");
                            n->kids.push_back(v);
                            n->flags.push_back(2);
                            if (!EatPunct(",")) break;
                            continue;
                        } else {
                            Fail("期望属性名");
                            return nullptr;
                        }
                        
                        if (IsPunct("(")) {
                            AstNode* fn = New(AstKind::Function);
                            fn->str = key;
                            if (!ParseParams(fn)) return nullptr;
                            AstNode* body = ParseBlock();
                            if (!body) return nullptr;
                            fn->kids.push_back(body);
                            n->names.push_back(key);
                            n->kids.push_back(fn);
                            n->flags.push_back(computed ? 1 : 0);
                            if (!EatPunct(",")) break;
                            continue;
                        }
                        if (!EatPunct(":")) {
                            Fail("期望 :");
                            return nullptr;
                        }
                        AstNode* v = ParseAssign();
                        if (!v) return nullptr;
                        n->names.push_back(key);
                        n->kids.push_back(v);
                        n->flags.push_back(computed ? 1 : 0);
                        if (!EatPunct(",")) break;
                    }
                    if (!EatPunct("}")) {
                        Fail("期望 }");
                        return nullptr;
                    }
                    return n;
                }
                break;
            }
            default:
                break;
        }
        Fail("无法解析的表达式");
        return nullptr;
    }
};

}  

AstNode* Interp::NewNode(AstKind k) {
    arena_.push_back(std::unique_ptr<AstNode>(new AstNode()));
    AstNode* n = arena_.back().get();
    n->kind = k;
    return n;
}

const AstNode* Interp::Parse(const std::string& code, std::string* err) {
    Lexer lex(code);
    Parser ps;
    ps.interp = this;
    for (;;) {
        Token t = lex.Next();
        if (!lex.err.empty()) {
            *err = lex.err;
            return nullptr;
        }
        ps.toks.push_back(t);
        if (t.kind == Tok::End) break;
    }
    const AstNode* prog = ps.ParseProgram();
    if (!prog) {
        *err = ps.err.empty() ? "语法错误" : ps.err;
        return nullptr;
    }
    return prog;
}



}  
