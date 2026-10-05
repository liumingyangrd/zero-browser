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
    void OnMouseWheel(int delta);
    void OnKey(UINT key);
    void OnChar(wchar_t ch);
    void OnTimer(UINT_PTR id);

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
    UINT_PTR caret_timer_ = 1;
    UINT_PTR video_timer_ = 2;
    int nav_seq_ = 0;

    Rect page_view_;
    Rect status_rect_;

    HDC measure_dc_ = nullptr;
    GdiCanvas* measure_canvas_ = nullptr;
};

}  // namespace zb