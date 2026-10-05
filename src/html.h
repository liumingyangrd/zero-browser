#pragma once

#include "dom.h"

namespace zb {

std::unique_ptr<Node> ParseHtml(const std::string& source);

}  // namespace zb