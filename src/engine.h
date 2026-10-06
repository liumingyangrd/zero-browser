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

// 前置声明：js_dom.h 会 include engine.h（要读 Page 的视口/根节点），
// 这里用 shared_ptr 持有即可，不需要完整类型。
class JsRuntime;

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
    // 导航线程已把 <img> 按绝对 URL 解码成 Image；这里按 src 绑定到 DOM 节点。
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
    // JS 运行时要读视口尺寸（window.innerWidth / scrollTo 的边界）。
    int ViewportW() const { return viewport_w_; }
    int ViewportH() const { return viewport_h_; }
    // 文档根节点（JS 的 document 对象要遍历整棵树）。
    const Node* RootNode() const { return root_.get(); }
    // 诊断用只读访问器：让 tools 下的探针能读到每个 <video> 的播放器状态。
    // 不参与渲染逻辑，也不转移所有权。
    const std::map<Node*, std::unique_ptr<MediaPlayer>>& MediaPlayers() const {
        return media_;
    }
    void ResetScroll() { scroll_y_ = 0; }
    int Scroll() const { return scroll_y_; }
    void SetScroll(int y);

    // ---- JavaScript（js_dom.h 的 JsRuntime）
    // 外链脚本正文由导航线程取好（与图片一样，只做传输）。
    void SetExternalScripts(const std::map<std::string, std::string>& texts);
    bool HasScripts() const { return !scripts_.empty(); }
    // 布局完成后执行脚本，并派发 load。返回是否有脚本改动了 DOM。
    bool RunScripts();
    // 页面上的点击：命中节点 -> 派发 click（冒泡 + 默认动作）。
    bool DispatchPageClick(int x, int y);
    // 命中测试：返回该点最深的节点（没有则 nullptr）。
    const Node* NodeAt(int x, int y) const;
    // 定时器：由外壳的 WM_TIMER 驱动。
    bool RunJsTimers();
    int NextJsTimerDelayMs() const;
    bool TakeJsDirty();
    std::string TakeJsNavigation();
    bool TakeJsReload();
    JsRuntime* Js() { return js_.get(); }
    const JsRuntime* Js() const { return js_.get(); }
    // 诊断：脚本数与失败数（--dump-boxes 会打印）。
    int JsScriptCount() const;
    int JsScriptFailures() const;
    std::string JsLastError() const;

private:
    void BuildMediaPlayers();
    void AttachImages(const std::map<std::string, std::shared_ptr<Image>>& images);

    PageData data_;
    std::unique_ptr<Node> root_;
    std::vector<CssRule> rules_;
    std::unique_ptr<Box> root_box_;
    std::map<Node*, std::unique_ptr<MediaPlayer>> media_;
    std::map<std::string, std::shared_ptr<Image>> images_;
    // 脚本按文档顺序排队：first = 是否外链，second = 外链地址或内联正文。
    std::vector<std::pair<bool, std::string>> scripts_;
    std::map<std::string, std::string> external_scripts_;
    std::shared_ptr<JsRuntime> js_;
    bool scripts_ran_ = false;
    int viewport_w_ = 0;
    int viewport_h_ = 0;
    int scroll_y_ = 0;
};

}  // namespace zb