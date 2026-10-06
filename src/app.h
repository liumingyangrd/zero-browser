#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#endif

#include "common.h"
#include "engine.h"
#include "gdi.h"

#include <string>
#include <utility>
#include <vector>

namespace zb {

enum class HitArea {
    None,
    Tab,
    AddTab,
    Back,
    Forward,
    Reload,
    Home,
    Address,
    CloseTab,
    Page,
};

struct HitTest {
    HitArea area = HitArea::None;
    int index = -1;
};

struct TabState {
    std::string url;
    std::string title;
    Page page;
    int scroll = 0;
    std::vector<std::string> history;
    int history_index = -1;
    int pending_seq = -1;
    bool loading = false;
};

class BrowserApp {
public:
    BrowserApp(HINSTANCE inst);
    // visible=false 时不显示窗口，仅用于 --shot 无窗口渲染诊断。
    bool CreateMainWindow(bool visible = true, int width = 1180,
                          int height = 820);
    int Run();

    // 非交互式截图诊断（--shot）。复用真实 Render / OnLButtonDown 代码路径，
    // 但不依赖桌面窗口截图，所以在无桌面/受限会话里也能取证。
    struct ShotOptions {
        std::string url;
        std::string out;
        std::string out2;
        int width = 1180;
        int height = 820;
        int wait_ms = 6000;
        int after_ms = 1500;
        // 可选：导航后先把页面滚动到指定 y（文档坐标），用于验证 fixed 悬浮。
        int scroll = 0;
        // 可重复的 --click X,Y，按顺序依次投递给真实的 OnLButtonDown。
        std::vector<std::pair<int, int>> clicks;
        // 地址栏输入测试：--set-address 设置内容，--focus-address 聚焦，
        // --type 逐字符走真实的 OnChar 路径（用于回归「输入即崩溃」这类问题）。
        std::string set_address;
        bool focus_address = false;
        std::string type_text;
        // 按 VK_BACK 走真实的 OnKey 路径，N 次（回归「按码点退格」）。
        int backspace = 0;
        // 地址栏剪贴板回归：--clipboard 先把文本放进系统剪贴板，
        // --paste N 走真实的 Ctrl+V 路径，--select-all / --copy 同理。
        std::string clipboard;
        int paste = 0;
        bool select_all = false;
        bool copy = false;
        bool cut = false;
        // 可重复的 --hotkey NAME：走真实 OnKeyEx 路径验证快捷键。
        // 支持 ctrl+l / ctrl+r / ctrl+a / ctrl+c / ctrl+v / ctrl+x / f5 /
        // escape / enter（无窗口会话没有键盘，GetKeyState 恒为 0，
        // 只能这样把修饰键显式喂进去）。
        std::vector<std::string> hotkeys;
        bool dump_boxes = false;
    };
    bool HeadlessShot(const ShotOptions& opt);

    HWND Hwnd() const { return hwnd_; }
    int Width() const { return width_; }
    int TabCount() const { return (int)tabs_.size(); }
    void NavigateTo(const std::string& url, bool add_history = true);
    static void Log(const std::string& msg);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM w,
                                          LPARAM l);
    LRESULT WndProc(UINT msg, WPARAM w, LPARAM l);

    void PumpMessages(int ms);
    void LogMediaState(const char* tag) const;

    void OnPaint();
    void OnSize(int w, int h);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnLButtonDblClk(int x, int y);
    void OnMouseMove(int x, int y, bool dragging);
    void OnRButtonUp(int x, int y);
    void OnMouseWheel(int delta);
    void OnKey(UINT key);
    // 修饰键显式传入：无窗口测试（--shot）里没有真实键盘，
    // GetKeyState 恒为 0，所以 Ctrl+V 这类快捷键必须能带参调用同一条路径。
    void OnKeyEx(UINT key, bool ctrl, bool shift);
    void OnChar(wchar_t ch);
    void OnTimer(UINT_PTR id);

    // 地址栏编辑：选择区、剪贴板、右键菜单。
    bool AddressHasSelection() const;
    size_t AddressSelBegin() const;
    size_t AddressSelEnd() const;
    std::string AddressSelectedText() const;
    void AddressClearSelection();
    void AddressSelectAll();
    void AddressDeleteSelection();
    void AddressInsert(const std::string& utf8);
    void AddressCopyToClipboard();
    void AddressCutToClipboard();
    void AddressPasteFromClipboard();
    // 按点击/拖动位置算插入点下标（绘制与命中必须同源）。
    size_t AddressCaretFromX(int x) const;
    void ShowAddressMenu(int x, int y);

    void Render(Canvas& canvas);
    void RenderTabs(Canvas& canvas);
    void RenderToolbar(Canvas& canvas);
    void RenderPage(Canvas& canvas);
    void RenderStatus(Canvas& canvas);

    HitTest HitTestPoint(int x, int y) const;
    void RelayoutActive();
    void RelayoutTab(int index);
    void SyncAddress();
    void StartNavigate(const std::string& url, bool add_history);
    void FinishNavigate(int tab_index, int seq, const std::string& requested,
                        const std::string& html, const std::string& final_url,
                        const std::string& error, bool add_history,
                        const std::map<std::string, std::shared_ptr<Image>>&
                            images);
    void OnNavigationDone(LPARAM l);
    std::string CurrentUrl() const;
    TabState& ActiveTab();
    const TabState& ActiveTab() const;
    // 标签宽度只在标签数量/窗口宽度变化时变，绘制与命中测试必须同源。
    int TabWidth() const {
        int n = (int)tabs_.size();
        return std::max(70, std::min(190, (width_ - 70) / std::max(1, n)));
    }

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    std::vector<TabState> tabs_;
    int active_ = 0;

    std::string address_text_;
    bool address_focused_ = false;
    int caret_ = 0;
    bool caret_visible_ = true;
    // 选择区锚点（字节下标）：<0 表示没有选择。选择区为 [min(anchor,caret),
    // max(anchor,caret))，与插入点共用同一套码点边界规则。
    int sel_anchor_ = -1;
    bool mouse_selecting_ = false;
    int drag_anchor_ = -1;
    int drag_down_x_ = 0;
    UINT_PTR caret_timer_ = 1;
    UINT_PTR video_timer_ = 2;
    int nav_seq_ = 0;

    Rect page_view_;
    Rect status_rect_;

    HDC measure_dc_ = nullptr;
    GdiCanvas* measure_canvas_ = nullptr;
};

}  // namespace zb