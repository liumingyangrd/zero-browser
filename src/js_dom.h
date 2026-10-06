#pragma once

// 页面级 JavaScript 运行时：解释器（js.h）+ DOM 桥（JsBridge）+ 事件 + 定时器。
//
// 分工：
//   ws_dom.cpp 只认 Node / Box（本项目自己的 DOM 与布局），不认识 Win32；
//   Page 负责在布局完成后调用 RunPendingScripts()，在命中测试后调用
//   DispatchClick()，在 UI 定时器里调用 RunTimers()，并在 TakeDirty() 为真时重新布局。
//
// 安全约定（与 README 的"脚本错误不能搞崩浏览器"一致）：
//   * 每段脚本单独 RunScript，异常只记录在 error_ 里，绝不上抛；
//   * 死循环/无限递归由解释器的步数预算与调用深度护栏兜住；
//   * JS 改 DOM 只置脏标记，真正的重新布局由宿主在安全时机做。

#include "dom.h"
#include "js.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zb {

class Page;
struct Box;

class JsRuntime : public JsBridge {
public:
    JsRuntime(Page* page, const std::string& url);
    ~JsRuntime() override;

    // 脚本按文档顺序排队，布局完成后统一执行。
    void AddScript(const std::string& code, const std::string& name);
    bool HasPendingScripts() const { return !pending_.empty(); }
    bool RunPendingScripts();
    // 用解释器求值一个表达式（内联事件属性 onclick="..." 用得上）。
    bool EvalHandlerSource(const std::string& src, JsValue* out);

    // 事件派发：target 是命中测试选中的节点，逐级冒泡到 document。
    // 返回 true 表示有处理器执行过。坐标用于事件对象的 clientX/clientY。
    bool DispatchClick(const Node* target, int x = 0, int y = 0);
    bool DispatchEvent(const std::string& type, const Node* target,
                       bool bubbles = true);
    // 载入完成后派发 load（window/document/body 上的处理器）
    void DispatchLoad();

    // 定时器：由 UI 定时器周期调用；返回是否有定时器到期执行。
    bool RunTimers();
    // 下一次到期还有多少毫秒（没有定时器返回 -1），宿主用它决定要不要开定时器。
    int NextTimerDelayMs() const;
    int TimerCount() const { return (int)timers_.size(); }

    // JS 改过 DOM：宿主需要重新布局。
    bool TakeDirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }
    void MarkDirty() { dirty_ = true; }

    // location.assign / href= 请求的导航（宿主取走后清空）。
    std::string TakePendingNavigation();
    bool TakeReloadRequest();

    const std::string& LastError() const { return error_; }
    const Interp::Stats& Stats() const { return interp_.stats(); }
    size_t ListenerCount() const;
    const std::vector<std::string>& ScriptLog() const { return script_log_; }

    // JsBridge 实现
    bool HostGet(void* host, const std::string& kind, const std::string& key,
                 JsValue* out) override;
    bool HostSet(void* host, const std::string& kind, const std::string& key,
                 const JsValue& v) override;
    std::string HostToString(void* host, const std::string& kind) override;

    Interp& Interp_() { return interp_; }

private:
    struct Timer {
        int id = 0;
        JsValue fn;
        std::vector<JsValue> args;
        double due_ms = 0;
        double interval_ms = 0;
        bool repeating = false;
        bool cancelled = false;
    };
    struct Listener {
        std::string type;
        JsValue fn;
    };
    struct EventData {
        std::string type;
        JsValue target;
        JsValue current;
        double client_x = 0;
        double client_y = 0;
        bool prevented = false;
        bool stopped = false;
    };

    // ---- DOM 工具
    Node* DocumentNode() const;
    Node* BodyNode() const;
    JsValue Wrap(Node* node);
    Node* Unwrap(const JsValue& v) const;
    bool IsElement(const JsValue& v) const;
    std::unique_ptr<Node> TakeOwnership(Node* node);
    void InsertAt(Node* parent, Node* child, int index);
    void RemoveChild(Node* parent, Node* child);
    std::string InnerHtml(const Node* node) const;
    void SetInnerHtml(Node* node, const std::string& html);
    std::string TextContent(const Node* node) const;
    void CollectByTag(const Node* root, const std::string& tag,
                      std::vector<Node*>* out) const;
    void CollectByClass(const Node* root, const std::string& cls,
                        std::vector<Node*>* out) const;
    void CollectBySelector(const Node* root, const std::string& sel,
                           std::vector<Node*>* out) const;
    const Box* BoxFor(const Node* node) const;

    // ---- 事件
    EventData* NewEvent(const std::string& type, const JsValue& target);
    JsValue MakeEventObject(EventData* ev, const JsValue& current);
    bool FireOnNode(Node* node, const std::string& type, EventData* ev);

    // ---- 宿主对象构造
    JsValue MakeDocumentObject();
    JsValue MakeWindowObject();
    JsValue MakeLocationObject();
    void InstallGlobals();

    Page* page_ = nullptr;
    Interp interp_;
    std::string url_;
    std::string error_;
    bool dirty_ = false;
    bool loaded_dispatched_ = false;
    std::string pending_nav_;
    bool pending_reload_ = false;

    std::vector<std::pair<std::string, std::string>> pending_;  // (name, code)
    std::vector<std::string> script_log_;

    std::map<const Node*, JsValue> wrappers_;
    std::map<const Node*, std::vector<Listener>> listeners_;
    // 内联事件属性（onclick="..."）编出来的函数，按节点缓存。
    std::map<const Node*, std::map<std::string, JsValue>> inline_handlers_;
    std::vector<std::unique_ptr<Node>> detached_;
    std::vector<Timer> timers_;
    std::vector<std::shared_ptr<EventData>> live_events_;
    int next_timer_id_ = 1;
    // 文档级/窗口级监听器（target 为 document/window）
    std::vector<Listener> document_listeners_;
    std::vector<Listener> window_listeners_;
    JsValue window_obj_;
    JsValue document_obj_;
    JsValue location_obj_;
    JsValue body_obj_;
};

}  // namespace zb
