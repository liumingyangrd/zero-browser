


#include "js_dom.h"

#include "common.h"
#include "css.h"
#include "engine.h"
#include "html.h"
#include "i18n.h"
#include "js_internal.h"
#include "layout.h"
#include "network.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace zb {

namespace {

double NowMs() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration_cast<duration<double, std::milli>>(steady_clock::now() -
                                                       start)
        .count();
}


std::string CamelToKebab(const std::string& s) {
    if (s == "cssText") return "cssText";
    if (s == "float" || s == "cssFloat") return "float";
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') {
            out.push_back('-');
            out.push_back((char)(c + 32));
        } else {
            out.push_back(c);
        }
    }
    return out;
}


std::string TagUpper(const Node* n) {
    if (!n) return "";
    if (n->type == NodeType::Text) return "#text";
    if (n->type == NodeType::Document) return "#document";
    std::string t = n->tag;
    for (char& c : t) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    }
    return t;
}


void EscapeHtml(const std::string& s, std::string* out) {
    for (char c : s) {
        switch (c) {
            case '&': *out += "&amp;"; break;
            case '<': *out += "&lt;"; break;
            case '>': *out += "&gt;"; break;
            case '"': *out += "&quot;"; break;
            default: out->push_back(c);
        }
    }
}

bool TagNeedsClose(const std::string& tag) {
    static const char* kVoid[] = {"area", "base",  "br",   "col",  "embed",
                                  "hr",   "img",   "input", "link", "meta",
                                  "param", "source", "track", "wbr"};
    for (const char* v : kVoid) {
        if (tag == v) return false;
    }
    return true;
}

std::string SerializeNode(const Node* n) {
    if (!n) return "";
    if (n->type == NodeType::Text) {
        std::string out;
        EscapeHtml(n->text, &out);
        return out;
    }
    if (n->type == NodeType::Document) {
        std::string out;
        for (const auto& c : n->children) out += SerializeNode(c.get());
        return out;
    }
    std::string out = "<" + n->tag;
    for (const auto& kv : n->attrs) {
        out += " " + kv.first + "=\"" + kv.second + "\"";
    }
    if (!TagNeedsClose(n->tag)) return out + ">";
    out += ">";
    for (const auto& c : n->children) out += SerializeNode(c.get());
    return out + "</" + n->tag + ">";
}



JsValue ArgOf(const std::vector<JsValue>& a, size_t i) {
    return i < a.size() ? a[i] : JsValue::Undef();
}


Node* NodeOfHost(void* host, const std::string& kind) {
    if (kind == "element" || kind == "text" || kind == "style" ||
        kind == "documentnode") {
        return (Node*)host;
    }
    return nullptr;
}




void SetStyleDecl(Node* node, const std::string& prop, const std::string& value) {
    if (!node || prop.empty()) return;
    std::string attr = node->Attr("style");
    std::vector<std::string> keep;
    for (const std::string& d : SplitStr(attr, ';')) {
        size_t c = d.find(':');
        if (c == std::string::npos) continue;
        std::string name = Lower(Trim(d.substr(0, c)));
        if (name.empty() || name == prop) continue;
        keep.push_back(Trim(d));
    }
    if (!Trim(value).empty()) keep.push_back(prop + ": " + Trim(value));
    std::string joined;
    for (size_t i = 0; i < keep.size(); ++i) {
        if (i) joined += "; ";
        joined += keep[i];
    }
    node->attrs["style"] = joined;
}


void AppendHtml(Node* target, const std::string& html) {
    if (!target || html.empty()) return;
    std::vector<std::unique_ptr<Node>> kids = ParseHtmlFragment(html);
    for (auto& k : kids) {
        k->parent = target;
        target->children.push_back(std::move(k));
    }
}

}  


JsRuntime::JsRuntime(Page* page, const std::string& url)
    : page_(page), interp_(this), url_(url) {
    interp_.SetBridge(this);
    interp_.SetStepLimit(20000000);
    InstallGlobals();
}

JsRuntime::~JsRuntime() = default;

Node* JsRuntime::DocumentNode() const {
    if (!page_) return nullptr;
    const Box* root = page_->RootBox();
    (void)root;
    
    if (page_->RootBox() && page_->RootBox()->node) return page_->RootBox()->node;
    return nullptr;
}

Node* JsRuntime::BodyNode() const {
    Node* doc = DocumentNode();
    if (!doc) return nullptr;
    const Node* body = FindFirstElement(doc, "body");
    if (body) return const_cast<Node*>(body);
    return doc;
}

const Box* JsRuntime::BoxFor(const Node* node) const {
    if (!page_ || !node || !page_->RootBox()) return nullptr;
    struct Walk {
        const Box* b;
        const Node* target;
        const Box* Find(const Box* box) {
            if (!box) return nullptr;
            if (box->node == target) return box;
            for (const auto& c : box->children) {
                if (const Box* r = Find(c.get())) return r;
            }
            return nullptr;
        }
    } w{nullptr, node};
    return w.Find(page_->RootBox());
}


JsValue JsRuntime::Wrap(Node* node) {
    if (!node) return JsValue::Null();
    auto it = wrappers_.find(node);
    if (it != wrappers_.end()) return it->second;
    const char* kind = "element";
    if (node->type == NodeType::Text) kind = "text";
    else if (node->type == NodeType::Document) kind = "documentnode";
    JsValue v = interp_.MakeHost(node, kind, TagUpper(node));
    wrappers_[node] = v;
    return v;
}

Node* JsRuntime::Unwrap(const JsValue& v) const {
    if (v.type != JsType::Host || !v.obj) return nullptr;
    const std::string& k = v.obj->host_kind;
    if (k == "element" || k == "text" || k == "style" || k == "documentnode") {
        return (Node*)v.obj->host;
    }
    return nullptr;
}

bool JsRuntime::IsElement(const JsValue& v) const { return Unwrap(v) != nullptr; }



std::unique_ptr<Node> JsRuntime::TakeOwnership(Node* node) {
    if (!node) return nullptr;
    for (size_t i = 0; i < detached_.size(); ++i) {
        if (detached_[i].get() == node) {
            std::unique_ptr<Node> out = std::move(detached_[i]);
            detached_.erase(detached_.begin() + i);
            return out;
        }
    }
    Node* parent = node->parent;
    if (parent) {
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == node) {
                std::unique_ptr<Node> out = std::move(parent->children[i]);
                parent->children.erase(parent->children.begin() + i);
                out->parent = nullptr;
                return out;
            }
        }
    }
    return nullptr;
}

void JsRuntime::InsertAt(Node* parent, Node* child, int index) {
    if (!parent || !child) return;
    std::unique_ptr<Node> owned = TakeOwnership(child);
    if (!owned) {
        
        owned.reset(child);
    }
    child->parent = parent;
    if (index < 0 || index > (int)parent->children.size()) {
        parent->children.push_back(std::move(owned));
    } else {
        parent->children.insert(parent->children.begin() + index,
                                std::move(owned));
    }
    MarkDirty();
}

void JsRuntime::RemoveChild(Node* parent, Node* child) {
    if (!parent || !child) return;
    for (size_t i = 0; i < parent->children.size(); ++i) {
        if (parent->children[i].get() == child) {
            std::unique_ptr<Node> owned = std::move(parent->children[i]);
            parent->children.erase(parent->children.begin() + i);
            owned->parent = nullptr;
            detached_.push_back(std::move(owned));
            MarkDirty();
            return;
        }
    }
}

std::string JsRuntime::InnerHtml(const Node* node) const {
    std::string out;
    if (!node) return out;
    for (const auto& c : node->children) out += SerializeNode(c.get());
    return out;
}

void JsRuntime::SetInnerHtml(Node* node, const std::string& html) {
    if (!node) return;
    
    while (!node->children.empty()) {
        std::unique_ptr<Node> owned = std::move(node->children.back());
        node->children.pop_back();
        owned->parent = nullptr;
        detached_.push_back(std::move(owned));
    }
    if (html.empty()) {
        MarkDirty();
        return;
    }
    
    std::vector<std::unique_ptr<Node>> kids = ParseHtmlFragment(html);
    for (auto& k : kids) {
        k->parent = node;
        node->children.push_back(std::move(k));
    }
    MarkDirty();
}

std::string JsRuntime::TextContent(const Node* node) const {
    if (!node) return "";
    std::string out;
    if (node->type == NodeType::Text) out += node->text;
    for (const auto& c : node->children) out += TextContent(c.get());
    return out;
}

void JsRuntime::CollectByTag(const Node* root, const std::string& tag,
                             std::vector<Node*>* out) const {
    if (!root || !out) return;
    std::string want = Lower(tag);
    if (root->type == NodeType::Element) {
        if (want == "*" || root->tag == want) out->push_back((Node*)root);
    }
    for (const auto& c : root->children) CollectByTag(c.get(), tag, out);
}

void JsRuntime::CollectByClass(const Node* root, const std::string& cls,
                               std::vector<Node*>* out) const {
    if (!root || !out) return;
    if (root->type == NodeType::Element) {
        for (const std::string& c : SplitStr(root->ClassList(), ' ')) {
            if (Trim(c) == cls) {
                out->push_back((Node*)root);
                break;
            }
        }
    }
    for (const auto& c : root->children) CollectByClass(c.get(), cls, out);
}



void JsRuntime::CollectBySelector(const Node* root, const std::string& sel,
                                  std::vector<Node*>* out) const {
    if (!root || !out || sel.empty()) return;
    std::vector<std::string> groups = SplitStr(sel, ',');
    auto matchesOne = [&](const Node* n, const std::string& one) {
        std::string s = Trim(one);
        if (s.empty()) return false;
        
        std::vector<SelectorPart> chain;
        std::vector<std::string> parts = SplitStr(s, ' ');
        for (const std::string& p : parts) {
            std::string t = Trim(p);
            if (t.empty() || t == ">") continue;
            SelectorPart sp;
            size_t i = 0;
            while (i < t.size()) {
                if (t[i] == '#') {
                    size_t j = i + 1;
                    while (j < t.size() && (isalnum((unsigned char)t[j]) ||
                                            t[j] == '-' || t[j] == '_')) {
                        ++j;
                    }
                    sp.id = t.substr(i + 1, j - i - 1);
                    i = j;
                } else if (t[i] == '.') {
                    size_t j = i + 1;
                    while (j < t.size() && (isalnum((unsigned char)t[j]) ||
                                            t[j] == '-' || t[j] == '_')) {
                        ++j;
                    }
                    sp.classes.push_back(t.substr(i + 1, j - i - 1));
                    i = j;
                } else if (t[i] == ':') {
                    size_t j = i + 1;
                    while (j < t.size() && t[j] != ':' && t[j] != '.' &&
                           t[j] != '#') {
                        ++j;
                    }
                    sp.pseudos.push_back(t.substr(i + 1, j - i - 1));
                    i = j;
                } else if (t[i] == '[') {
                    size_t j = t.find(']', i);
                    if (j == std::string::npos) break;
                    std::string inner = t.substr(i + 1, j - i - 1);
                    AttrTest at;
                    size_t eq = inner.find('=');
                    std::string name = eq == std::string::npos
                                           ? inner
                                           : inner.substr(0, eq);
                    std::string val = eq == std::string::npos
                                          ? ""
                                          : Trim(inner.substr(eq + 1));
                    if (!val.empty() && (val[0] == '"' || val[0] == '\'')) {
                        val = val.substr(1, val.size() - 2);
                    }
                    if (!name.empty() && (name.back() == '^' ||
                                          name.back() == '$' ||
                                          name.back() == '*')) {
                        at.op = name.back();
                        name.pop_back();
                    }
                    at.name = Lower(name);
                    at.value = val;
                    sp.attrs.push_back(at);
                    i = j + 1;
                } else {
                    size_t j = i;
                    while (j < t.size() && t[j] != '.' && t[j] != '#' &&
                           t[j] != ':' && t[j] != '[') {
                        ++j;
                    }
                    sp.tag = Lower(t.substr(i, j - i));
                    i = j;
                }
            }
            chain.push_back(sp);
        }
        if (chain.empty()) return false;
        CssRule rule;
        rule.parts.push_back(chain);
        if (MatchesRule(n, rule)) return true;
        
        
        return PartMatches((Node*)n, chain.back());
    };
    
    std::vector<const Node*> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        for (const auto& c : n->children) stack.push_back(c.get());
        if (n == root) continue;  
        if (n->type != NodeType::Element) continue;
        for (const std::string& g : groups) {
            if (matchesOne(n, g)) {
                out->push_back((Node*)n);
                break;
            }
        }
    }
    
    std::vector<Node*> ordered;
    for (Node* n : *out) ordered.push_back(n);
    std::sort(ordered.begin(), ordered.end(), [](Node* a, Node* b) {
        return a < b;  
    });
    *out = ordered;
}


JsValue JsRuntime::MakeDocumentObject() {
    return interp_.MakeHost(nullptr, "document", "#document");
}
JsValue JsRuntime::MakeWindowObject() { return interp_.MakeHost(nullptr, "window", "window"); }
JsValue JsRuntime::MakeLocationObject() {
    return interp_.MakeHost(nullptr, "location", "location");
}


bool JsRuntime::HostGet(void* host, const std::string& kind,
                        const std::string& key, JsValue* out) {
    auto native = [&](const char* name, NativeFn fn) {
        *out = interp_.MakeNative(name, std::move(fn));
        return true;
    };

    if (kind == "window") {
        
        if (key == "document") {
            if (document_obj_.IsNullish()) document_obj_ = MakeDocumentObject();
            *out = document_obj_;
            return true;
        }
        if (key == "location") {
            if (location_obj_.IsNullish()) location_obj_ = MakeLocationObject();
            *out = location_obj_;
            return true;
        }
        JsValue g;
        if (interp_.GetGlobal(key, &g)) {
            *out = g;
            return true;
        }
        if (key == "innerWidth") {
            *out = JsValue::Num(page_ ? page_->ViewportW() : 0);
            return true;
        }
        if (key == "innerHeight") {
            *out = JsValue::Num(page_ ? page_->ViewportH() : 0);
            return true;
        }
        if (key == "scrollY" || key == "pageYOffset") {
            *out = JsValue::Num(page_ ? page_->Scroll() : 0);
            return true;
        }
        if (key == "scrollX" || key == "pageXOffset") {
            *out = JsValue::Num(0);
            return true;
        }
        if (key == "name" || key == "origin") {
            *out = JsValue::Str(key == "origin" ? url_ : "");
            return true;
        }
        if (key == "addEventListener") {
            return native("addEventListener",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              if (a.size() >= 2 && it.IsCallable(a[1])) {
                                  window_listeners_.push_back(
                                      Listener{it.ToString(a[0]), a[1]});
                              }
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "removeEventListener") {
            return native("removeEventListener",
                          [this](Interp&, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              if (a.size() >= 2) {
                                  std::string t = a[0].str;
                                  window_listeners_.erase(
                                      std::remove_if(
                                          window_listeners_.begin(),
                                          window_listeners_.end(),
                                          [&](const Listener& l) {
                                              return l.type == t &&
                                                     l.fn.obj == a[1].obj;
                                          }),
                                      window_listeners_.end());
                              }
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "scrollTo" || key == "scroll") {
            return native("scrollTo",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              if (page_) {
                                  page_->SetScroll((int)it.ToNumber(ArgOf(a, 0)));
                              }
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "getComputedStyle") {
            return native("getComputedStyle",
                          [](Interp&, void*, const std::string&,
                             const std::vector<JsValue>&, const JsValue&,
                             JsValue* o) -> bool {
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        return false;
    }

    if (kind == "location") {
        std::string u = url_;
        size_t scheme = u.find("://");
        std::string after = scheme == std::string::npos ? u : u.substr(scheme + 3);
        size_t slash = after.find('/');
        std::string hostname = slash == std::string::npos ? after : after.substr(0, slash);
        std::string path = slash == std::string::npos ? "/" : after.substr(slash);
        size_t qm = path.find('?');
        std::string search = qm == std::string::npos ? "" : path.substr(qm);
        std::string pathname = qm == std::string::npos ? path : path.substr(0, qm);
        size_t hm = pathname.find('#');
        std::string hash = "";
        if (hm != std::string::npos) {
            hash = pathname.substr(hm);
            pathname = pathname.substr(0, hm);
        }
        if (key == "href" || key == "toString" || key == "valueOf") {
            if (key == "href") {
                *out = JsValue::Str(url_);
                return true;
            }
        }
        if (key == "href") { *out = JsValue::Str(url_); return true; }
        if (key == "pathname") { *out = JsValue::Str(pathname); return true; }
        if (key == "search") { *out = JsValue::Str(search); return true; }
        if (key == "hash") { *out = JsValue::Str(hash); return true; }
        if (key == "host" || key == "hostname") { *out = JsValue::Str(hostname); return true; }
        if (key == "protocol") {
            *out = JsValue::Str(scheme == std::string::npos ? "" : u.substr(0, scheme + 1));
            return true;
        }
        if (key == "origin") {
            *out = JsValue::Str(scheme == std::string::npos ? "" : u.substr(0, scheme + 3));
            return true;
        }
        if (key == "assign" || key == "replace") {
            return native("assign",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              pending_nav_ = it.ToString(ArgOf(a, 0));
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "reload") {
            return native("reload",
                          [this](Interp&, void*, const std::string&,
                                 const std::vector<JsValue>&, const JsValue&,
                                 JsValue* o) -> bool {
                              pending_reload_ = true;
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "toString") {
            return native("toString",
                          [this](Interp&, void*, const std::string&,
                                 const std::vector<JsValue>&, const JsValue&,
                                 JsValue* o) -> bool {
                              *o = JsValue::Str(url_);
                              return true;
                          });
        }
        return false;
    }

    if (kind == "document" || kind == "documentnode") {
        if (key == "body") {
            if (body_obj_.IsNullish()) body_obj_ = Wrap(BodyNode());
            *out = body_obj_;
            return true;
        }
        if (key == "documentElement") {
            Node* doc = DocumentNode();
            Node* html = doc ? const_cast<Node*>(FindFirstElement(doc, "html")) : nullptr;
            *out = html ? Wrap(html) : Wrap(doc);
            return true;
        }
        if (key == "head") {
            Node* doc = DocumentNode();
            Node* head = doc ? const_cast<Node*>(FindFirstElement(doc, "head")) : nullptr;
            *out = Wrap(head);
            return true;
        }
        if (key == "title") {
            Node* doc = DocumentNode();
            const Node* t = doc ? FindFirstElement(doc, "title") : nullptr;
            *out = JsValue::Str(t ? NodeText(t) : "");
            return true;
        }
        if (key == "readyState") {
            *out = JsValue::Str(loaded_dispatched_ ? "complete" : "loading");
            return true;
        }
        if (key == "URL" || key == "documentURI") {
            *out = JsValue::Str(url_);
            return true;
        }
        if (key == "cookie") {
            auto jar = CookieJarDump();
            std::string joined;
            for (size_t i = 0; i < jar.size(); ++i) {
                if (i) joined += "; ";
                joined += jar[i];
            }
            *out = JsValue::Str(joined);
            return true;
        }
        if (key == "getElementById") {
            return native("getElementById",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              std::string id = it.ToString(ArgOf(a, 0));
                              Node* doc = DocumentNode();
                              std::vector<Node*> all;
                              CollectByTag(doc, "*", &all);
                              for (Node* n : all) {
                                  if (n->Id() == id) {
                                      *o = Wrap(n);
                                      return true;
                                  }
                              }
                              *o = JsValue::Null();
                              return true;
                          });
        }
        if (key == "getElementsByTagName") {
            return native("getElementsByTagName",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              std::vector<Node*> found;
                              CollectByTag(DocumentNode(), it.ToString(ArgOf(a, 0)),
                                           &found);
                              std::vector<JsValue> items;
                              for (Node* n : found) items.push_back(Wrap(n));
                              *o = interp_.NewArray(items);
                              return true;
                          });
        }
        if (key == "getElementsByClassName") {
            return native("getElementsByClassName",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              std::vector<Node*> found;
                              CollectByClass(DocumentNode(), it.ToString(ArgOf(a, 0)),
                                             &found);
                              std::vector<JsValue> items;
                              for (Node* n : found) items.push_back(Wrap(n));
                              *o = interp_.NewArray(items);
                              return true;
                          });
        }
        if (key == "querySelector" || key == "querySelectorAll") {
            bool all = key == "querySelectorAll";
            return native(all ? "querySelectorAll" : "querySelector",
                          [this, all](Interp& it, void*, const std::string&,
                                      const std::vector<JsValue>& a,
                                      const JsValue&, JsValue* o) -> bool {
                              std::vector<Node*> found;
                              CollectBySelector(DocumentNode(),
                                                it.ToString(ArgOf(a, 0)), &found);
                              if (!all) {
                                  *o = found.empty() ? JsValue::Null() : Wrap(found[0]);
                                  return true;
                              }
                              std::vector<JsValue> items;
                              for (Node* n : found) items.push_back(Wrap(n));
                              *o = interp_.NewArray(items);
                              return true;
                          });
        }
        if (key == "createElement") {
            return native("createElement",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              Node* n = MakeElement(it.ToString(ArgOf(a, 0)));
                              
                              detached_.push_back(std::unique_ptr<Node>(n));
                              *o = Wrap(n);
                              return true;
                          });
        }
        if (key == "createTextNode") {
            return native("createTextNode",
                          [this](Interp& it, void*, const std::string&,
                                 const std::vector<JsValue>& a, const JsValue&,
                                 JsValue* o) -> bool {
                              Node* n = MakeText(it.ToString(ArgOf(a, 0)));
                              detached_.push_back(std::unique_ptr<Node>(n));
                              *o = Wrap(n);
                              return true;
                          });
        }
        if (key == "write" || key == "writeln") {
            bool nl = key == "writeln";
            return native(nl ? "writeln" : "write",
                          [this, nl](Interp& it, void*, const std::string&,
                                     const std::vector<JsValue>& a,
                                     const JsValue&, JsValue* o) -> bool {
                              std::string html;
                              for (const JsValue& v : a) html += it.ToString(v);
                              if (nl) html += "\n";
                              Node* target = BodyNode();
                              if (target) {
                                  
                                  AppendHtml(target, html);
                                  MarkDirty();
                              }
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "addEventListener" || key == "removeEventListener") {
            bool add = key == "addEventListener";
            return native(add ? "addEventListener" : "removeEventListener",
                          [this, add](Interp&, void*, const std::string&,
                                      const std::vector<JsValue>& a,
                                      const JsValue&, JsValue* o) -> bool {
                              if (a.size() >= 2) {
                                  std::string t = a[0].str;
                                  if (add) {
                                      document_listeners_.push_back(
                                          Listener{t, a[1]});
                                  } else {
                                      document_listeners_.erase(
                                          std::remove_if(
                                              document_listeners_.begin(),
                                              document_listeners_.end(),
                                              [&](const Listener& l) {
                                                  return l.type == t &&
                                                         l.fn.obj == a[1].obj;
                                              }),
                                          document_listeners_.end());
                                  }
                              }
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        return false;
    }

    
    Node* node = NodeOfHost(host, kind);
    if (!node) return false;
    if (kind == "style") {
        
        std::string attr = node->Attr("style");
        std::string want = CamelToKebab(key);
        for (const std::string& d : SplitStr(attr, ';')) {
            size_t c = d.find(':');
            if (c == std::string::npos) continue;
            if (Lower(Trim(d.substr(0, c))) == want) {
                *out = JsValue::Str(Trim(d.substr(c + 1)));
                return true;
            }
        }
        if (key == "cssText") {
            *out = JsValue::Str(attr);
            return true;
        }
        if (key == "setProperty") {
            return native("setProperty",
                          [this, node](Interp& it, void*, const std::string&,
                                       const std::vector<JsValue>& a,
                                       const JsValue&, JsValue* o) -> bool {
                              SetStyleDecl(node, it.ToString(ArgOf(a, 0)),
                                           it.ToString(ArgOf(a, 1)));
                              *o = JsValue::Undef();
                              return true;
                          });
        }
        if (key == "getPropertyValue") {
            return native("getPropertyValue",
                          [](Interp&, void*, const std::string&,
                             const std::vector<JsValue>&, const JsValue&,
                             JsValue* o) -> bool {
                              *o = JsValue::Str("");
                              return true;
                          });
        }
        *out = JsValue::Str("");
        return true;
    }

    if (key == "tagName" || key == "nodeName") {
        *out = JsValue::Str(TagUpper(node));
        return true;
    }
    if (key == "nodeType") {
        *out = JsValue::Num(node->type == NodeType::Element ? 1
                            : node->type == NodeType::Text  ? 3
                                                            : 9);
        return true;
    }
    if (key == "id") { *out = JsValue::Str(node->Id()); return true; }
    if (key == "className") { *out = JsValue::Str(node->ClassList()); return true; }
    if (key == "textContent" || key == "innerText") {
        *out = JsValue::Str(TextContent(node));
        return true;
    }
    if (key == "innerHTML" || key == "outerHTML") {
        *out = JsValue::Str(key == "innerHTML" ? InnerHtml(node)
                                               : SerializeNode(node));
        return true;
    }
    if (key == "value") {
        
        std::string v = node->Attr("value");
        if (v.empty() && node->tag == "textarea") v = TextContent(node);
        *out = JsValue::Str(v);
        return true;
    }
    if (key == "checked") {
        *out = JsValue::Bool(node->HasAttr("checked"));
        return true;
    }
    if (key == "href" || key == "src") {
        *out = JsValue::Str(node->Attr(key));
        return true;
    }
    if (key == "children" || key == "childNodes") {
        std::vector<JsValue> items;
        for (const auto& c : node->children) {
            if (key == "children" && c->type != NodeType::Element) continue;
            items.push_back(Wrap(c.get()));
        }
        *out = interp_.NewArray(items);
        return true;
    }
    if (key == "childElementCount") {
        int n = 0;
        for (const auto& c : node->children) {
            if (c->type == NodeType::Element) ++n;
        }
        *out = JsValue::Num(n);
        return true;
    }
    if (key == "firstChild" || key == "lastChild") {
        if (node->children.empty()) {
            *out = JsValue::Null();
        } else {
            Node* c = key == "firstChild" ? node->children.front().get()
                                          : node->children.back().get();
            *out = Wrap(c);
        }
        return true;
    }
    if (key == "firstElementChild" || key == "lastElementChild") {
        Node* found = nullptr;
        if (key == "firstElementChild") {
            for (const auto& c : node->children) {
                if (c->type == NodeType::Element) { found = c.get(); break; }
            }
        } else {
            for (auto it2 = node->children.rbegin(); it2 != node->children.rend();
                 ++it2) {
                if ((*it2)->type == NodeType::Element) { found = it2->get(); break; }
            }
        }
        *out = found ? Wrap(found) : JsValue::Null();
        return true;
    }
    if (key == "parentNode" || key == "parentElement") {
        *out = node->parent ? Wrap(node->parent) : JsValue::Null();
        return true;
    }
    if (key == "nextSibling" || key == "previousSibling") {
        Node* parent = node->parent;
        if (!parent) {
            *out = JsValue::Null();
            return true;
        }
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() != node) continue;
            if (key == "nextSibling") {
                *out = i + 1 < parent->children.size()
                           ? Wrap(parent->children[i + 1].get())
                           : JsValue::Null();
            } else {
                *out = i > 0 ? Wrap(parent->children[i - 1].get())
                             : JsValue::Null();
            }
            return true;
        }
        *out = JsValue::Null();
        return true;
    }
    if (key == "classList") {
        
        
        
        
        JsValue list = interp_.NewObject();
                          auto setClass = [this, node](const std::string& cls,
                                                       bool add) {
                              std::string cur = node->ClassList();
                              std::vector<std::string> parts;
                              bool found = false;
                              for (const std::string& p : SplitStr(cur, ' ')) {
                                  std::string t = Trim(p);
                                  if (t.empty()) continue;
                                  if (t == cls) {
                                      found = true;
                                      if (!add) continue;
                                  }
                                  parts.push_back(t);
                              }
                              if (add && !found) parts.push_back(cls);
                              std::string joined;
                              for (size_t i = 0; i < parts.size(); ++i) {
                                  if (i) joined += " ";
                                  joined += parts[i];
                              }
                              node->attrs["class"] = joined;
                              MarkDirty();
                          };
                          list.obj->props["add"] = interp_.MakeNative(
                              "add",
                              [setClass](Interp& in, void*, const std::string&,
                                         const std::vector<JsValue>& a,
                                         const JsValue&, JsValue* r) -> bool {
                                  for (const JsValue& v : a) {
                                      setClass(in.ToString(v), true);
                                  }
                                  *r = JsValue::Undef();
                                  return true;
                              });
                          list.obj->props["remove"] = interp_.MakeNative(
                              "remove",
                              [setClass](Interp& in, void*, const std::string&,
                                         const std::vector<JsValue>& a,
                                         const JsValue&, JsValue* r) -> bool {
                                  for (const JsValue& v : a) {
                                      setClass(in.ToString(v), false);
                                  }
                                  *r = JsValue::Undef();
                                  return true;
                              });
                          list.obj->props["contains"] = interp_.MakeNative(
                              "contains",
                              [this, node](Interp& in, void*, const std::string&,
                                           const std::vector<JsValue>& a,
                                           const JsValue&, JsValue* r) -> bool {
                                  std::string want = in.ToString(ArgOf(a, 0));
                                  for (const std::string& p :
                                       SplitStr(node->ClassList(), ' ')) {
                                      if (Trim(p) == want) {
                                          *r = JsValue::Bool(true);
                                          return true;
                                      }
                                  }
                                  *r = JsValue::Bool(false);
                                  return true;
                              });
                          list.obj->props["toggle"] = interp_.MakeNative(
                              "toggle",
                              [setClass, node](Interp& in, void*, const std::string&,
                                               const std::vector<JsValue>& a,
                                               const JsValue&, JsValue* r) -> bool {
                                  std::string want = in.ToString(ArgOf(a, 0));
                                  bool has = false;
                                  for (const std::string& p :
                                       SplitStr(node->ClassList(), ' ')) {
                                      if (Trim(p) == want) has = true;
                                  }
                                  setClass(want, !has);
                                  *r = JsValue::Bool(!has);
                                  return true;
                              });
        *out = list;
        return true;
    }
    if (key == "style") {
        *out = interp_.MakeHost(node, "style", "style");
        return true;
    }
    if (key == "setAttribute" || key == "getAttribute" ||
        key == "removeAttribute" || key == "hasAttribute") {
        std::string op = key;
        return native(key.c_str(),
                      [this, node, op](Interp& it, void*, const std::string&,
                                       const std::vector<JsValue>& a,
                                       const JsValue&, JsValue* o) -> bool {
                          std::string name = Lower(it.ToString(ArgOf(a, 0)));
                          if (op == "setAttribute") {
                              node->attrs[name] = it.ToString(ArgOf(a, 1));
                              MarkDirty();
                              *o = JsValue::Undef();
                              return true;
                          }
                          if (op == "getAttribute") {
                              *o = node->HasAttr(name)
                                       ? JsValue::Str(node->Attr(name))
                                       : JsValue::Null();
                              return true;
                          }
                          if (op == "removeAttribute") {
                              node->attrs.erase(name);
                              MarkDirty();
                              *o = JsValue::Undef();
                              return true;
                          }
                          *o = JsValue::Bool(node->HasAttr(name));
                          return true;
                      });
    }
    if (key == "appendChild" || key == "insertBefore" ||
        key == "removeChild" || key == "replaceChild") {
        std::string op = key;
        return native(key.c_str(),
                      [this, node, op](Interp&, void*, const std::string&,
                                       const std::vector<JsValue>& a,
                                       const JsValue&, JsValue* o) -> bool {
                          Node* child = Unwrap(ArgOf(a, 0));
                          if (!child) {
                              *o = JsValue::Null();
                              return true;
                          }
                          if (op == "removeChild") {
                              RemoveChild(node, child);
                              *o = ArgOf(a, 0);
                              return true;
                          }
                          if (op == "appendChild") {
                              InsertAt(node, child, -1);
                              *o = ArgOf(a, 0);
                              return true;
                          }
                          if (op == "insertBefore") {
                              Node* ref = Unwrap(ArgOf(a, 1));
                              int index = -1;
                              if (ref) {
                                  for (size_t i = 0; i < node->children.size(); ++i) {
                                      if (node->children[i].get() == ref) {
                                          index = (int)i;
                                          break;
                                      }
                                  }
                              }
                              InsertAt(node, child, index);
                              *o = ArgOf(a, 0);
                              return true;
                          }
                          
                          Node* old = Unwrap(ArgOf(a, 1));
                          if (old) {
                              int index = -1;
                              for (size_t i = 0; i < node->children.size(); ++i) {
                                  if (node->children[i].get() == old) {
                                      index = (int)i;
                                      break;
                                  }
                              }
                              RemoveChild(node, old);
                              InsertAt(node, child, index);
                          }
                          *o = ArgOf(a, 1);
                          return true;
                      });
    }
    if (key == "addEventListener" || key == "removeEventListener") {
        bool add = key == "addEventListener";
        std::string op = key;
        return native(key.c_str(),
                      [this, node, add, op](Interp&, void*, const std::string&,
                                            const std::vector<JsValue>& a,
                                            const JsValue&, JsValue* o) -> bool {
                          if (a.size() >= 2) {
                              std::string t = a[0].str;
                              if (add) {
                                  listeners_[node].push_back(Listener{t, a[1]});
                              } else {
                                  auto& v = listeners_[node];
                                  v.erase(std::remove_if(
                                              v.begin(), v.end(),
                                              [&](const Listener& l) {
                                                  return l.type == t &&
                                                         l.fn.obj == a[1].obj;
                                              }),
                                          v.end());
                              }
                          }
                          *o = JsValue::Undef();
                          return true;
                      });
    }
    if (key == "click") {
        return native("click",
                      [this, node](Interp&, void*, const std::string&,
                                   const std::vector<JsValue>&, const JsValue&,
                                   JsValue* o) -> bool {
                          DispatchClick(node);
                          *o = JsValue::Undef();
                          return true;
                      });
    }
    if (key == "getBoundingClientRect") {
        return native("getBoundingClientRect",
                      [this, node](Interp& it, void*, const std::string&,
                                   const std::vector<JsValue>&, const JsValue&,
                                   JsValue* o) -> bool {
                          const Box* box = BoxFor(node);
                          JsValue r = it.NewObject();
                          double x = 0, y = 0, w = 0, h = 0;
                          if (box) {
                              x = box->rect.x;
                              y = box->rect.y;
                              w = box->rect.w;
                              h = box->rect.h;
                          }
                          r.obj->props["left"] = JsValue::Num(x);
                          r.obj->props["top"] = JsValue::Num(y);
                          r.obj->props["width"] = JsValue::Num(w);
                          r.obj->props["height"] = JsValue::Num(h);
                          r.obj->props["right"] = JsValue::Num(x + w);
                          r.obj->props["bottom"] = JsValue::Num(y + h);
                          r.obj->props["x"] = JsValue::Num(x);
                          r.obj->props["y"] = JsValue::Num(y);
                          *o = r;
                          return true;
                      });
    }
    if (key == "offsetWidth" || key == "offsetHeight" || key == "offsetTop" ||
        key == "offsetLeft") {
        const Box* box = BoxFor(node);
        double v = 0;
        if (box) {
            if (key == "offsetWidth") v = box->rect.w;
            else if (key == "offsetHeight") v = box->rect.h;
            else if (key == "offsetTop") v = box->rect.y;
            else v = box->rect.x;
        }
        *out = JsValue::Num(v);
        return true;
    }
    if (key == "querySelector" || key == "querySelectorAll" ||
        key == "getElementsByTagName" || key == "getElementsByClassName") {
        std::string op = key;
        return native(key.c_str(),
                      [this, node, op](Interp& it, void*, const std::string&,
                                       const std::vector<JsValue>& a,
                                       const JsValue&, JsValue* o) -> bool {
                          std::string q = it.ToString(ArgOf(a, 0));
                          std::vector<Node*> found;
                          if (op == "getElementsByTagName") {
                              CollectByTag(node, q, &found);
                          } else if (op == "getElementsByClassName") {
                              CollectByClass(node, q, &found);
                          } else {
                              CollectBySelector(node, q, &found);
                          }
                          if (op == "querySelector") {
                              *o = found.empty() ? JsValue::Null() : Wrap(found[0]);
                              return true;
                          }
                          std::vector<JsValue> items;
                          for (Node* n : found) items.push_back(Wrap(n));
                          *o = it.NewArray(items);
                          return true;
                      });
    }
    
    if (node->HasAttr(key)) {
        *out = JsValue::Str(node->Attr(key));
        return true;
    }
    return false;
}

bool JsRuntime::HostSet(void* host, const std::string& kind,
                        const std::string& key, const JsValue& v) {
    if (kind == "window") {
        
        interp_.AddGlobal(key, v);
        return true;
    }
    if (kind == "location") {
        if (key == "href") {
            pending_nav_ = interp_.ToString(v);
            return true;
        }
        if (key == "hash" || key == "search") {
            return true;
        }
        return false;
    }
    if (kind == "document" || kind == "documentnode") {
        if (key == "title") {
            Node* doc = DocumentNode();
            const Node* t = doc ? FindFirstElement(doc, "title") : nullptr;
            if (t) {
                const_cast<Node*>(t)->text = interp_.ToString(v);
                MarkDirty();
            }
            return true;
        }
        if (key == "cookie") {
            return true;  
        }
        return false;
    }
    if (kind == "style") {
        Node* node = (Node*)host;
        if (!node) return false;
        if (key == "cssText") {
            node->attrs["style"] = interp_.ToString(v);
            MarkDirty();
            return true;
        }
        SetStyleDecl(node, CamelToKebab(key), interp_.ToString(v));
        return true;
    }
    Node* node = NodeOfHost(host, kind);
    if (!node) return false;
    if (key == "innerHTML") {
        SetInnerHtml(node, interp_.ToString(v));
        return true;
    }
    if (key == "textContent" || key == "innerText") {
        while (!node->children.empty()) {
            std::unique_ptr<Node> owned = std::move(node->children.back());
            node->children.pop_back();
            owned->parent = nullptr;
            detached_.push_back(std::move(owned));
        }
        std::string text = interp_.ToString(v);
        if (!text.empty()) {
            Node* t = MakeText(text);
            t->parent = node;
            node->children.push_back(std::unique_ptr<Node>(t));
        }
        MarkDirty();
        return true;
    }
    if (key == "id") {
        node->attrs["id"] = interp_.ToString(v);
        MarkDirty();
        return true;
    }
    if (key == "className") {
        node->attrs["class"] = interp_.ToString(v);
        MarkDirty();
        return true;
    }
    if (key == "value") {
        node->attrs["value"] = interp_.ToString(v);
        MarkDirty();
        return true;
    }
    if (key == "checked") {
        if (interp_.ToBool(v)) {
            node->attrs["checked"] = "checked";
        } else {
            node->attrs.erase("checked");
        }
        MarkDirty();
        return true;
    }
    if (key == "href" || key == "src") {
        node->attrs[key] = interp_.ToString(v);
        MarkDirty();
        return true;
    }
    if (key == "onclick" || key == "onload" || key == "onchange" ||
        key == "oninput" || key == "onsubmit" || key == "onmouseover" ||
        key == "onmouseout" || key == "onkeydown") {
        node->attrs[key] = interp_.ToString(v);
        return true;
    }
    
    node->attrs[key] = interp_.ToString(v);
    MarkDirty();
    return true;
}

std::string JsRuntime::HostToString(void* host, const std::string& kind) {
    if (kind == "window") return "[object Window]";
    if (kind == "document" || kind == "documentnode") return "[object HTMLDocument]";
    if (kind == "location") return url_;
    if (kind == "event") return "[object Event]";
    if (kind == "style") return "[object CSSStyleDeclaration]";
    Node* node = (Node*)host;
    if (!node) return "[object Object]";
    if (node->type == NodeType::Text) return "#text";
    return "[object HTML" + TagUpper(node) + "Element]";
}


void JsRuntime::AddScript(const std::string& code, const std::string& name) {
    if (Trim(code).empty()) return;
    pending_.push_back({name, code});
}

bool JsRuntime::RunPendingScripts() {
    bool all_ok = true;
    std::vector<std::pair<std::string, std::string>> queue;
    queue.swap(pending_);
    for (const auto& s : queue) {
        bool ok = interp_.RunScript(s.second, s.first);
        char buf[256];
        snprintf(buf, sizeof(buf), "[js] %s ok=%d steps=%lld",
                 s.first.c_str(), ok ? 1 : 0, interp_.stats().steps);
        script_log_.push_back(buf);
        if (!ok) {
            all_ok = false;
            error_ = interp_.error();
            script_log_.push_back("[js] 错误: " + error_);
        }
    }
    return all_ok;
}

bool JsRuntime::EvalHandlerSource(const std::string& src, JsValue* out) {
    
    std::string code = "(function(event){" + src + "\n})";
    if (!interp_.EvalExpression(code, out)) {
        error_ = interp_.error();
        return false;
    }
    return true;
}

std::string JsRuntime::TakePendingNavigation() {
    std::string s = pending_nav_;
    pending_nav_.clear();
    return s;
}

bool JsRuntime::TakeReloadRequest() {
    bool r = pending_reload_;
    pending_reload_ = false;
    return r;
}

size_t JsRuntime::ListenerCount() const {
    size_t n = document_listeners_.size() + window_listeners_.size();
    for (const auto& kv : listeners_) n += kv.second.size();
    return n;
}


JsRuntime::EventData* JsRuntime::NewEvent(const std::string& type,
                                          const JsValue& target) {
    auto ev = std::make_shared<EventData>();
    ev->type = type;
    ev->target = target;
    ev->current = target;
    live_events_.push_back(ev);
    
    if (live_events_.size() > 32) live_events_.erase(live_events_.begin());
    return live_events_.back().get();
}

JsValue JsRuntime::MakeEventObject(EventData* ev, const JsValue& current) {
    JsValue o = interp_.NewObject();
    o.obj->props["type"] = JsValue::Str(ev->type);
    o.obj->props["target"] = ev->target;
    o.obj->props["currentTarget"] = current;
    o.obj->props["clientX"] = JsValue::Num(ev->client_x);
    o.obj->props["clientY"] = JsValue::Num(ev->client_y);
    o.obj->props["preventDefault"] = interp_.MakeNative(
        "preventDefault",
        [ev](Interp&, void*, const std::string&, const std::vector<JsValue>&,
             const JsValue&, JsValue* out) -> bool {
            ev->prevented = true;
            *out = JsValue::Undef();
            return true;
        });
    o.obj->props["stopPropagation"] = interp_.MakeNative(
        "stopPropagation",
        [ev](Interp&, void*, const std::string&, const std::vector<JsValue>&,
             const JsValue&, JsValue* out) -> bool {
            ev->stopped = true;
            *out = JsValue::Undef();
            return true;
        });
    o.obj->props["defaultPrevented"] = JsValue::Bool(ev->prevented);
    return o;
}

bool JsRuntime::FireOnNode(Node* node, const std::string& type, EventData* ev) {
    if (!node || !ev) return false;
    bool ran = false;
    JsValue target = Wrap(node);

    
    auto it = listeners_.find(node);
    if (it != listeners_.end()) {
        std::vector<JsValue> handlers;
        for (const Listener& l : it->second) {
            if (l.type == type) handlers.push_back(l.fn);
        }
        for (const JsValue& fn : handlers) {
            JsValue event_obj = MakeEventObject(ev, target);
            JsValue r;
            if (!interp_.CallFunction(fn, target, {event_obj}, &r)) {
                error_ = interp_.error();
                script_log_.push_back("[js] 事件处理器异常: " + error_);
            }
            if (r.type == JsType::Bool && !r.b) ev->prevented = true;
            ran = true;
            if (ev->stopped) return ran;
        }
    }

    
    std::string attr = "on" + Lower(type);
    if (node->HasAttr(attr)) {
        JsValue fn;
        auto cache = inline_handlers_.find(node);
        if (cache != inline_handlers_.end() &&
            cache->second.count(attr)) {
            fn = cache->second[attr];
        } else {
            JsValue compiled;
            if (EvalHandlerSource(node->Attr(attr), &compiled)) {
                fn = compiled;
                inline_handlers_[node][attr] = compiled;
            }
        }
        if (interp_.IsCallable(fn)) {
            JsValue event_obj = MakeEventObject(ev, target);
            JsValue r;
            if (!interp_.CallFunction(fn, target, {event_obj}, &r)) {
                error_ = interp_.error();
                script_log_.push_back("[js] 内联 " + attr + " 异常: " + error_);
            }
            if (r.type == JsType::Bool && !r.b) ev->prevented = true;
            ran = true;
        }
    }
    return ran;
}

bool JsRuntime::DispatchEvent(const std::string& type, const Node* target,
                              bool bubbles) {
    if (!target) return false;
    Node* node = const_cast<Node*>(target);
    EventData* ev = NewEvent(type, Wrap(node));
    bool ran = false;
    
    for (Node* n = node; n; n = n->parent) {
        if (FireOnNode(n, type, ev)) ran = true;
        if (ev->stopped || !bubbles) break;
    }
    if (bubbles && !ev->stopped) {
        JsValue doc = document_obj_.IsNullish() ? MakeDocumentObject() : document_obj_;
        document_obj_ = doc;
        for (const Listener& l : document_listeners_) {
            if (l.type != type) continue;
            JsValue event_obj = MakeEventObject(ev, doc);
            JsValue r;
            if (!interp_.CallFunction(l.fn, doc, {event_obj}, &r)) {
                error_ = interp_.error();
            }
            if (r.type == JsType::Bool && !r.b) ev->prevented = true;
            ran = true;
        }
        if (window_obj_.IsNullish()) window_obj_ = MakeWindowObject();
        for (const Listener& l : window_listeners_) {
            if (l.type != type) continue;
            JsValue event_obj = MakeEventObject(ev, window_obj_);
            JsValue r;
            if (!interp_.CallFunction(l.fn, window_obj_, {event_obj}, &r)) {
                error_ = interp_.error();
            }
            ran = true;
        }
    }
    return ran;
}


namespace {
std::string ResolveHref(const std::string& base, const std::string& href) {
    std::string h = Trim(href);
    if (h.empty()) return "";
    if (h.find("://") != std::string::npos || StartsWith(h, "data:") ||
        StartsWith(h, "mailto:") || StartsWith(h, "javascript:")) {
        return h;
    }
    size_t scheme = base.find("://");
    if (scheme == std::string::npos) return h;
    size_t host_end = base.find('/', scheme + 3);
    std::string origin = host_end == std::string::npos ? base : base.substr(0, host_end);
    if (!h.empty() && h[0] == '/') return origin + h;
    std::string dir = base;
    size_t slash = dir.find_last_of('/');
    if (slash != std::string::npos && slash > scheme + 2) dir = dir.substr(0, slash + 1);
    else dir = origin + "/";
    return dir + h;
}
}  

bool JsRuntime::DispatchClick(const Node* target, int x, int y) {
    if (!target) return false;
    EventData* ev = NewEvent("click", Wrap(const_cast<Node*>(target)));
    ev->client_x = x;
    ev->client_y = y;
    
    bool ran = false;
    for (Node* n = const_cast<Node*>(target); n; n = n->parent) {
        if (FireOnNode(n, "click", ev)) ran = true;
        if (ev->stopped) break;
    }
    if (!ev->stopped) {
        if (document_obj_.IsNullish()) document_obj_ = MakeDocumentObject();
        for (const Listener& l : document_listeners_) {
            if (l.type != "click") continue;
            JsValue event_obj = MakeEventObject(ev, document_obj_);
            JsValue r;
            if (!interp_.CallFunction(l.fn, document_obj_, {event_obj}, &r)) {
                error_ = interp_.error();
            }
            ran = true;
        }
    }
    
    if (!ev->prevented) {
        for (Node* n = const_cast<Node*>(target); n; n = n->parent) {
            if (n->type == NodeType::Element && n->tag == "a" &&
                n->HasAttr("href")) {
                std::string href = n->Attr("href");
                if (StartsWith(Lower(href), "javascript:")) break;
                std::string abs = ResolveHref(url_, href);
                if (!abs.empty()) pending_nav_ = abs;
                break;
            }
        }
    }
    return ran;
}

void JsRuntime::DispatchLoad() {
    if (loaded_dispatched_) return;
    loaded_dispatched_ = true;
    if (window_obj_.IsNullish()) window_obj_ = MakeWindowObject();
    if (document_obj_.IsNullish()) document_obj_ = MakeDocumentObject();
    EventData* ev = NewEvent("load", document_obj_);
    for (const Listener& l : window_listeners_) {
        if (l.type != "load") continue;
        JsValue event_obj = MakeEventObject(ev, window_obj_);
        JsValue r;
        if (!interp_.CallFunction(l.fn, window_obj_, {event_obj}, &r)) {
            error_ = interp_.error();
        }
    }
    for (const Listener& l : document_listeners_) {
        if (l.type != "load" && l.type != "DOMContentLoaded") continue;
        JsValue event_obj = MakeEventObject(ev, document_obj_);
        JsValue r;
        if (!interp_.CallFunction(l.fn, document_obj_, {event_obj}, &r)) {
            error_ = interp_.error();
        }
    }
    
    if (Node* body = BodyNode()) FireOnNode(body, "load", ev);
}


bool JsRuntime::RunTimers() {
    double now = NowMs();
    std::vector<int> due;
    for (const Timer& t : timers_) {
        if (!t.cancelled && t.due_ms <= now) due.push_back(t.id);
    }
    if (due.empty()) return false;
    for (int id : due) {
        Timer* found = nullptr;
        for (Timer& t : timers_) {
            if (t.id == id) {
                found = &t;
                break;
            }
        }
        if (!found || found->cancelled) continue;
        if (found->repeating) {
            found->due_ms = now + std::max(1.0, found->interval_ms);
        } else {
            found->cancelled = true;
        }
        JsValue r;
        if (!interp_.CallFunction(found->fn, JsValue::Undef(), found->args, &r)) {
            error_ = interp_.error();
            script_log_.push_back("[js] 定时器回调异常: " + error_);
        }
    }
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                 [](const Timer& t) { return t.cancelled; }),
                  timers_.end());
    return true;
}

int JsRuntime::NextTimerDelayMs() const {
    double best = -1;
    double now = NowMs();
    for (const Timer& t : timers_) {
        if (t.cancelled) continue;
        double d = t.due_ms - now;
        if (d < 0) d = 0;
        if (best < 0 || d < best) best = d;
    }
    return best < 0 ? -1 : (int)best;
}


void JsRuntime::InstallGlobals() {
    window_obj_ = MakeWindowObject();
    document_obj_ = MakeDocumentObject();
    location_obj_ = MakeLocationObject();
    interp_.AddGlobal("window", window_obj_);
    interp_.AddGlobal("document", document_obj_);
    interp_.AddGlobal("location", location_obj_);
    interp_.AddGlobal("navigator", [&] {
        JsValue o = interp_.NewObject();
        o.obj->props["userAgent"] =
            JsValue::Str("ZeroBrowser/0.1.7 (self-built engine)");
        
        o.obj->props["language"] =
            JsValue::Str(UiIsEnglish() ? "en-US" : "zh-CN");
        return o;
    }());
    interp_.AddGlobal("screen", [&] {
        JsValue o = interp_.NewObject();
        o.obj->props["width"] = JsValue::Num(page_ ? page_->ViewportW() : 0);
        o.obj->props["height"] = JsValue::Num(page_ ? page_->ViewportH() : 0);
        return o;
    }());
    interp_.AddGlobal("alert", interp_.MakeNative(
                                   "alert",
                                   [this](Interp& it, void*, const std::string&,
                                          const std::vector<JsValue>& a,
                                          const JsValue&, JsValue* out) -> bool {
                                       
                                       script_log_.push_back(
                                           "[js] alert: " + it.ToString(a.empty()
                                                                            ? JsValue::Undef()
                                                                            : a[0]));
                                       *out = JsValue::Undef();
                                       return true;
                                   }));

    auto addTimer = [this](const char* name, bool repeating) {
        interp_.AddGlobal(
            name, interp_.MakeNative(
                      name, [this, repeating](Interp& it, void*,
                                              const std::string&,
                                              const std::vector<JsValue>& a,
                                              const JsValue&,
                                              JsValue* out) -> bool {
                          if (a.empty() || !it.IsCallable(a[0])) {
                              *out = JsValue::Num(0);
                              return true;
                          }
                          Timer t;
                          t.id = next_timer_id_++;
                          t.fn = a[0];
                          double delay = a.size() > 1 ? it.ToNumber(a[1]) : 0;
                          if (delay < 0 || delay != delay) delay = 0;
                          if (delay > 600000) delay = 600000;
                          t.due_ms = NowMs() + delay;
                          t.interval_ms = delay;
                          t.repeating = repeating;
                          for (size_t i = 2; i < a.size(); ++i) {
                              t.args.push_back(a[i]);
                          }
                          
                          if (timers_.size() < 512) timers_.push_back(t);
                          *out = JsValue::Num((double)t.id);
                          return true;
                      }));
    };
    addTimer("setTimeout", false);
    addTimer("setInterval", true);
    interp_.AddGlobal(
        "clearTimeout",
        interp_.MakeNative("clearTimeout",
                           [this](Interp& it, void*, const std::string&,
                                  const std::vector<JsValue>& a, const JsValue&,
                                  JsValue* out) -> bool {
                               int id = (int)it.ToNumber(a.empty()
                                                             ? JsValue::Num(0)
                                                             : a[0]);
                               for (Timer& t : timers_) {
                                   if (t.id == id) t.cancelled = true;
                               }
                               *out = JsValue::Undef();
                               return true;
                           }));
    interp_.AddGlobal("clearInterval", interp_.Global()->props["clearTimeout"]);
    interp_.AddGlobal(
        "requestAnimationFrame",
        interp_.MakeNative("requestAnimationFrame",
                           [this](Interp& it, void*, const std::string&,
                                  const std::vector<JsValue>& a, const JsValue&,
                                  JsValue* out) -> bool {
                               if (a.empty() || !it.IsCallable(a[0])) {
                                   *out = JsValue::Num(0);
                                   return true;
                               }
                               Timer t;
                               t.id = next_timer_id_++;
                               t.fn = a[0];
                               t.due_ms = NowMs() + 16;
                               t.args.push_back(JsValue::Num(NowMs()));
                               if (timers_.size() < 512) timers_.push_back(t);
                               *out = JsValue::Num((double)t.id);
                               return true;
                           }));
}

}  
