#pragma once

#include "common.h"
#include "css.h"
#include "dom.h"
#include "image.h"

#include <map>

namespace zb {

struct TextRun {
    std::string text;
    Rect rect;
    std::string color;
    int font_size = 16;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool link = false;
    std::string href;
    // Image replaced element: when non-empty, draw image into rect and ignore text.
    std::shared_ptr<Image> image;
    // A failed image load still keeps its placeholder size, drawn as a "broken image"
    // block with a gray background and border.
    bool image_missing = false;
    // Alternative text shown inside the broken-image placeholder (from <img alt>).
    std::string alt_text;
    // Form control: an empty string means ordinary text/image; otherwise input/button/select/textarea.
    std::string widget;
    std::string widget_value;
    bool widget_focused = false;
};

struct LinkArea {
    Rect rect;
    std::string href;
    bool fixed = false;  // fixed-position subtree: no scroll offset should be added on hit
};

struct Box {
    Node* node = nullptr;
    Style style;
    const std::vector<CssRule>* rules = nullptr;
    Rect rect;
    Rect content;
    std::vector<std::unique_ptr<Box>> children;
    std::vector<TextRun> runs;
    std::vector<LinkArea> links;
    int scroll_height = 0;
    bool hidden = false;
    // fixed positioning: painting and hit testing use viewport-local coordinates and do not scroll with the page.
    bool fixed = false;
};

class Layout {
public:
    Layout(const Node* root, const std::vector<CssRule>& rules, int viewport_w,
           int viewport_h = 0)
        : root_(root), rules_(rules), viewport_w_(viewport_w),
          viewport_h_(viewport_h) {}

    std::unique_ptr<Box> Build(Canvas* measurer);

private:
    const Node* root_ = nullptr;
    const std::vector<CssRule>& rules_;
    int viewport_w_ = 0;
    int viewport_h_ = 0;
};

}  // namespace zb