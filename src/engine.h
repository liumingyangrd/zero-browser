#pragma once

#include "common.h"
#include "css.h"
#include "dom.h"
#include "gfx.h"
#include "image.h"
#include "layout.h"
#include "media.h"

#include <map>
#include <memory>

namespace zb {

struct PageData {
    std::string url;
    std::string final_url;
    std::string title;
    std::string html;
    std::string error;
    bool ready = false;
};

std::string BuiltinHtml(const std::string& key);

class Page {
public:
    Page() = default;
    ~Page() = default;
    Page(Page&&) = default;
    Page& operator=(Page&&) = default;
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    void ParseHtml(const std::string& html, const std::string& url);
    // The navigation thread has already decoded every <img> into an Image keyed by
    // absolute URL; here they are bound to DOM nodes by their src attribute.
    void SetImages(const std::map<std::string, std::shared_ptr<Image>>& images);
    void Relayout(int viewport_w, int viewport_h, Canvas* measurer);
    void Paint(Canvas* canvas, const Rect& viewport, int scroll_y) const;
    void CollectLinks(std::vector<LinkArea>& out) const;
    bool UpdateMedia();
    bool MediaClick(int x, int y, int fixed_x = -2147483647,
                int fixed_y = -2147483647);

    const PageData& Data() const { return data_; }
    PageData& MutData() { return data_; }
    int ContentHeight() const { return root_box_ ? root_box_->scroll_height : 0; }
    const Box* RootBox() const { return root_box_.get(); }
    // Read-only accessor for diagnostics: lets the probes under tools/ read each <video>'s
    // player state. It takes no part in rendering and transfers no ownership.
    const std::map<Node*, std::unique_ptr<MediaPlayer>>& MediaPlayers() const {
        return media_;
    }
    void ResetScroll() { scroll_y_ = 0; }
    int Scroll() const { return scroll_y_; }
    void SetScroll(int y);

private:
    void BuildMediaPlayers();
    void AttachImages(const std::map<std::string, std::shared_ptr<Image>>& images);

    PageData data_;
    std::unique_ptr<Node> root_;
    std::vector<CssRule> rules_;
    std::unique_ptr<Box> root_box_;
    std::map<Node*, std::unique_ptr<MediaPlayer>> media_;
    std::map<std::string, std::shared_ptr<Image>> images_;
    int viewport_w_ = 0;
    int viewport_h_ = 0;
    int scroll_y_ = 0;
};

}  // namespace zb