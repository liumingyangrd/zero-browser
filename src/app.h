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
    Settings,
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
    
    
    std::string pending_url;
    
    Node* field = nullptr;
    int field_caret = 0;
    bool field_focused = false;
    
    
    int layout_w = 0;
    int layout_h = 0;
};

class BrowserApp {
public:
    BrowserApp(HINSTANCE inst);
    
    bool CreateMainWindow(bool visible = true, int width = 1180,
                          int height = 820);
    int Run();

    
    
    struct ShotOptions {
        std::string url;
        std::string out;
        std::string out2;
        int width = 1180;
        int height = 820;
        int wait_ms = 6000;
        int after_ms = 1500;
        
        int scroll = 0;
        
        
        
        
        struct Action {
            enum class Kind { Click, TypeField, Press };
            Kind kind = Kind::Click;
            int x = 0;
            int y = 0;
            std::string text;  
        };
        std::vector<Action> actions;
        
        
        
        std::string set_address;
        bool focus_address = false;
        std::string type_text;
        
        int backspace = 0;
        
        
        std::string clipboard;
        int paste = 0;
        bool select_all = false;
        bool copy = false;
        bool cut = false;
        
        
        
        
        std::vector<std::string> hotkeys;
        bool dump_boxes = false;
        
        
        std::string set_lang;
        
        bool save_settings = false;
        
        bool dump_settings = false;
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
    
    
    void OnKeyEx(UINT key, bool ctrl, bool shift);
    void OnChar(wchar_t ch);
    void OnTimer(UINT_PTR id);
    
    void OnJsTick();

    
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
    
    
    bool EnsureLayout(int index);
    void EnsureLayoutActive();
    void SyncAddress();
    void StartNavigate(const std::string& url, bool add_history);
    void FinishNavigate(int tab_index, int seq, const std::string& requested,
                        const std::string& html, const std::string& final_url,
                        const std::string& error, bool add_history,
                        const std::map<std::string, std::shared_ptr<Image>>&
                            images,
                        const std::map<std::string, std::string>& scripts);
    void OnNavigationDone(LPARAM l);
    
    void OnAssetsDone(LPARAM l);
    
    
    std::string FieldText(const Node* n) const;
    void SetFieldText(Node* n, const std::string& v);
    
    void FocusField(Node* n, int click_x);
    
    
    
    bool SubmitFieldForm(Node* n, Node* activated = nullptr);
    
    bool AdvanceFieldFocus(bool backward);
    
    void LogFieldState(const char* tag) const;
    
    std::string post_pending_body_;
    std::string post_pending_type_;

    std::string CurrentUrl() const;
    
    std::string DisplayUrl() const;
    TabState& ActiveTab();
    const TabState& ActiveTab() const;
    
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
    
    
    int sel_anchor_ = -1;
    bool mouse_selecting_ = false;
    int drag_anchor_ = -1;
    int drag_down_x_ = 0;
    UINT_PTR caret_timer_ = 1;
    UINT_PTR video_timer_ = 2;
    
    UINT_PTR js_timer_ = 3;
    int nav_seq_ = 0;

    Rect page_view_;
    Rect status_rect_;

    HDC measure_dc_ = nullptr;
    GdiCanvas* measure_canvas_ = nullptr;
};

}  