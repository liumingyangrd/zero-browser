#pragma once

#include "common.h"
#include "image.h"

#include <map>

namespace zb {

enum class NodeType {
    Document,
    Element,
    Text,
};

struct Node {
    NodeType type = NodeType::Element;
    std::string tag;
    std::string text;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    std::map<std::string, std::string> attrs;
    
    std::shared_ptr<Image> image;

    std::string Id() const {
        auto it = attrs.find("id");
        return it == attrs.end() ? std::string() : it->second;
    }

    std::string ClassList() const {
        auto it = attrs.find("class");
        return it == attrs.end() ? std::string() : it->second;
    }

    std::string Attr(const std::string& name) const {
        auto it = attrs.find(Lower(name));
        return it == attrs.end() ? std::string() : it->second;
    }

    bool HasAttr(const std::string& name) const {
        return attrs.find(Lower(name)) != attrs.end();
    }
};

inline Node* MakeElement(const std::string& tag) {
    auto n = new Node();
    n->type = NodeType::Element;
    n->tag = Lower(tag);
    n->parent = nullptr;
    return n;
}

inline Node* MakeText(const std::string& text) {
    auto n = new Node();
    n->type = NodeType::Text;
    n->text = text;
    return n;
}

inline void Append(Node* parent, Node* child) {
    if (!parent || !child) return;
    child->parent = parent;
    parent->children.emplace_back(child);
}

inline const Node* FindFirstElement(Node* node, const std::string& tag) {
    if (!node) return nullptr;
    if (node->type == NodeType::Element && node->tag == Lower(tag)) return node;
    for (const auto& c : node->children) {
        if (const Node* found = FindFirstElement(c.get(), tag)) return found;
    }
    return nullptr;
}

inline std::string NodeText(const Node* node) {
    std::string out;
    if (!node) return out;
    if (node->type == NodeType::Text) {
        out += node->text;
    }
    for (const auto& c : node->children) {
        out += NodeText(c.get());
    }
    return out;
}





inline std::string ImageSourceOf(const Node* node) {
    if (!node) return "";
    static const char* kAttrs[] = {"src",       "data-src",      "data-original",
                                   "data-lazy-src", "data-actualsrc",
                                   "data-original-src", "data-echo"};
    for (const char* a : kAttrs) {
        std::string v = Trim(node->Attr(a));
        if (v.empty()) continue;
        std::string lv = Lower(v);
        
        if (lv.find("blank.gif") != std::string::npos ||
            lv.find("placeholder") != std::string::npos ||
            lv.find("r0lgodlhaqab") != std::string::npos) {
            continue;
        }
        return v;
    }
    std::string srcset = Trim(node->Attr("data-srcset"));
    if (srcset.empty()) srcset = Trim(node->Attr("srcset"));
    if (!srcset.empty()) {
        auto parts = SplitStr(srcset, ',');
        if (!parts.empty()) {
            std::string first = Trim(parts[0]);
            size_t sp = first.find(' ');
            return sp == std::string::npos ? first : first.substr(0, sp);
        }
    }
    return "";
}

}  