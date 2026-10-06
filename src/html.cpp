#include "html.h"

#include <cstdlib>
#include <set>

namespace zb {

namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool IsNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
}

size_t FindCaseInsensitive(const std::string& haystack,
                           const std::string& needle, size_t from) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return std::string::npos;
    }
    for (size_t p = from; p + needle.size() <= haystack.size(); ++p) {
        bool match = true;
        for (size_t k = 0; k < needle.size(); ++k) {
            unsigned char a = (unsigned char)haystack[p + k];
            unsigned char b = (unsigned char)needle[k];
            if (std::tolower(a) != std::tolower(b)) {
                match = false;
                break;
            }
        }
        if (match) return p;
    }
    return std::string::npos;
}

const std::set<std::string>& VoidTags() {
    static const std::set<std::string> tags = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"};
    return tags;
}

std::string DecodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '&') {
            out.push_back(s[i++]);
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(s[i++]);
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        std::string decoded;
        if (ent == "amp") decoded = "&";
        else if (ent == "lt") decoded = "<";
        else if (ent == "gt") decoded = ">";
        else if (ent == "quot") decoded = "\"";
        else if (ent == "apos") decoded = "'";
        else if (ent == "nbsp") decoded = " ";
        else if (ent == "copy") decoded = "\xC2\xA9";
        else if (ent == "reg") decoded = "\xC2\xAE";
        else if (ent == "euro") decoded = "\xE2\x82\xAC";
        else if (ent.size() >= 2 && ent[0] == '#') {
            int cp = 0;
            if (ent[1] == 'x' || ent[1] == 'X') {
                cp = (int)strtol(ent.c_str() + 2, nullptr, 16);
            } else {
                cp = (int)strtol(ent.c_str() + 1, nullptr, 10);
            }
            if (cp == 0x22) decoded = "\"";
            else if (cp == 0x26) decoded = "&";
            else if (cp == 0x27) decoded = "'";
            else if (cp == 0x3C) decoded = "<";
            else if (cp == 0x3E) decoded = ">";
            else if (cp == 0xA0) decoded = " ";
            else if (cp > 0 && cp <= 0x10FFFF) {
                
                if (cp < 0x80) {
                    decoded.push_back((char)cp);
                } else if (cp < 0x800) {
                    decoded.push_back((char)(0xC0 | (cp >> 6)));
                    decoded.push_back((char)(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    decoded.push_back((char)(0xE0 | (cp >> 12)));
                    decoded.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    decoded.push_back((char)(0x80 | (cp & 0x3F)));
                } else {
                    decoded.push_back((char)(0xF0 | (cp >> 18)));
                    decoded.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                    decoded.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    decoded.push_back((char)(0x80 | (cp & 0x3F)));
                }
            }
        }
        if (!decoded.empty()) {
            out += decoded;
            i = semi + 1;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

}  

std::unique_ptr<Node> ParseHtml(const std::string& source_raw) {
    
    
    
    std::string source = source_raw;
    if (source.size() >= 3 && (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF) {
        source.erase(0, 3);
    }
    auto root = std::unique_ptr<Node>(MakeElement("html"));

    bool has_html = source.find("<html") != std::string::npos;
    bool has_body = source.find("<body") != std::string::npos;
    (void)has_html;

    Node* body = nullptr;
    if (!has_body) {
        body = MakeElement("body");
        Append(root.get(), body);
    }

    std::vector<Node*> stack;
    stack.push_back(root.get());
    if (body) stack.push_back(body);

    size_t i = 0;
    const size_t n = source.size();

    
    std::string raw_tag;
    std::string raw_text;

    auto flush_raw = [&]() {
        if (!raw_tag.empty()) {
            std::string content = raw_text;
            if (raw_tag == "title" || raw_tag == "textarea") {
                content = DecodeEntities(content);
            }
            Node* text = MakeText(content);
            if (stack.empty()) {
                Append(body ? body : root.get(), text);
            } else {
                Append(stack.back(), text);
            }
            raw_tag.clear();
            raw_text.clear();
        }
    };

    while (i < n) {
        if (!raw_tag.empty()) {
            size_t close = FindCaseInsensitive(source, "</" + raw_tag, i);
            if (close == std::string::npos) {
                raw_text += source.substr(i);
                i = n;
                break;
            }
            raw_text += source.substr(i, close - i);
            i = close;
            flush_raw();
            continue;
        }

        if (source[i] == '<') {
            if (i + 4 <= n && source.compare(i, 4, "<!--") == 0) {
                size_t end = source.find("-->", i + 4);
                i = end == std::string::npos ? n : end + 3;
                continue;
            }
            
            
            
            
            if (i + 2 <= n && source[i + 1] == '!') {
                bool cdata = i + 9 <= n && Lower(source.substr(i, 9)) == "<![cdata[";
                if (cdata) {
                    size_t end = source.find("]]>", i + 9);
                    i = end == std::string::npos ? n : end + 3;
                } else {
                    size_t end = source.find('>', i + 2);
                    i = end == std::string::npos ? n : end + 1;
                }
                continue;
            }

            size_t j = i + 1;
            bool closing = false;
            if (j < n && source[j] == '/') {
                closing = true;
                j++;
            }

            while (j < n && IsSpace(source[j])) j++;
            std::string tag;
            while (j < n && IsNameChar(source[j])) {
                tag.push_back((char)std::tolower((unsigned char)source[j]));
                j++;
            }

            if (tag.empty()) {
                
                size_t end = source.find('<', i + 1);
                std::string text = source.substr(i, end == std::string::npos
                                                       ? n - i
                                                       : end - i);
                if (stack.empty()) {
                    Append(body ? body : root.get(), MakeText(text));
                } else {
                    Append(stack.back(), MakeText(text));
                }
                i = end == std::string::npos ? n : end;
                continue;
            }

            if (closing) {
                flush_raw();
                size_t end = source.find('>', j);
                
                for (int k = (int)stack.size() - 1; k >= 0; --k) {
                    if (stack[k]->type == NodeType::Element &&
                        stack[k]->tag == tag) {
                        if (k > 0) stack.resize((size_t)k);
                        break;
                    }
                }
                i = end == std::string::npos ? n : end + 1;
                continue;
            }

            
            Node* el = MakeElement(tag);
            bool self_close = false;
            while (j < n && source[j] != '>') {
                while (j < n && IsSpace(source[j])) j++;
                if (j >= n || source[j] == '>') break;
                if (source[j] == '/') {
                    if (j + 1 < n && source[j + 1] == '>') {
                        self_close = true;
                        j += 2;
                        break;
                    }
                    j++;
                    continue;
                }
                std::string name;
                while (j < n && source[j] != '=' && !IsSpace(source[j]) &&
                       source[j] != '>') {
                    name.push_back((char)std::tolower((unsigned char)source[j]));
                    j++;
                }
                std::string value;
                while (j < n && IsSpace(source[j])) j++;
                if (j < n && source[j] == '=') {
                    j++;
                    while (j < n && IsSpace(source[j])) j++;
                    if (j < n && (source[j] == '"' || source[j] == '\'')) {
                        char q = source[j++];
                        while (j < n && source[j] != q) {
                            value.push_back(source[j++]);
                        }
                        if (j < n) j++;
                    } else {
                        while (j < n && !IsSpace(source[j]) &&
                               source[j] != '>') {
                            value.push_back(source[j++]);
                        }
                    }
                }
                if (!name.empty()) {
                    el->attrs[Lower(name)] = DecodeEntities(value);
                }
            }
            if (j < n && source[j] == '>') j++;

            flush_raw();

            if (stack.empty()) {
                Append(body ? body : root.get(), el);
                stack.push_back(el);
            } else {
                Append(stack.back(), el);
                stack.push_back(el);
            }

            if (self_close || VoidTags().count(tag) > 0) {
                stack.pop_back();
            } else if (tag == "style" || tag == "script" || tag == "textarea" ||
                       tag == "title" || tag == "xmp") {
                raw_tag = tag;
                raw_text.clear();
            }
            i = j;
            continue;
        }

        
        size_t next = source.find('<', i);
        if (next == std::string::npos) next = n;
        std::string text = DecodeEntities(source.substr(i, next - i));
        Node* t = MakeText(text);
        if (stack.empty()) {
            Append(body ? body : root.get(), t);
        } else {
            Append(stack.back(), t);
        }
        i = next;
    }

    flush_raw();
    return root;
}





std::vector<std::unique_ptr<Node>> ParseHtmlFragment(const std::string& source) {
    std::vector<std::unique_ptr<Node>> out;
    std::unique_ptr<Node> root = ParseHtml(source);
    if (!root) return out;
    Node* container = root.get();
    if (container->tag == "html") {
        
        Node* body = nullptr;
        for (auto& c : container->children) {
            if (c->type == NodeType::Element && c->tag == "body") {
                body = c.get();
                break;
            }
        }
        container = body ? body : root.get();
    }
    for (auto& c : container->children) {
        if (container == root.get() && c->type == NodeType::Element &&
            c->tag == "head") {
            continue;  
        }
        out.push_back(std::move(c));
    }
    return out;
}

}  