#pragma once

#include "dom.h"

#include <memory>
#include <vector>

namespace zb {

std::unique_ptr<Node> ParseHtml(const std::string& source);

// HTML 片段解析（innerHTML / document.write 用）：不做 html/body 外壳包装，
// 返回源里最外层的节点。用整页解析器会把片段套进合成的 html>body，
// 插进 DOM 后就多一层嵌套壳。
std::vector<std::unique_ptr<Node>> ParseHtmlFragment(const std::string& source);

}  // namespace zb