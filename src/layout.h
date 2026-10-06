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
    // 这个 run 属于哪个 DOM 节点（文本节点或控件所在的元素）。
    // 命中测试要按 run 反查节点才能把点击派发给 <button> 之类自绘控件。
    Node* node = nullptr;
    std::string color;
    int font_size = 16;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool link = false;
    std::string href;
    // 图片替换元素：非空时绘制 image 到 rect，text 忽略。
    std::shared_ptr<Image> image;
    // 图片加载失败也保留占位大小，绘制成灰底边框的“破图”块。
    bool image_missing = false;
    // 破图占位里显示的替代文本（来自 <img alt>）。
    std::string alt_text;
    // 表单控件：空串表示普通文本/图片；否则为 input/button/select/textarea。
    std::string widget;
    std::string widget_value;
    bool widget_focused = false;
};

struct LinkArea {
    Rect rect;
    std::string href;
    bool fixed = false;  // fixed 定位子树，命中时不应加滚动偏移
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
    // fixed 定位：绘制与命中测试按视口局部坐标处理，不随页面滚动。
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