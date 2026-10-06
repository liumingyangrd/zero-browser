#pragma once

#include "dom.h"

#include <memory>
#include <vector>

namespace zb {

std::unique_ptr<Node> ParseHtml(const std::string& source);




std::vector<std::unique_ptr<Node>> ParseHtmlFragment(const std::string& source);

}  