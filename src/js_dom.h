#pragma once













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

    
    void AddScript(const std::string& code, const std::string& name);
    bool HasPendingScripts() const { return !pending_.empty(); }
    bool RunPendingScripts();
    
    bool EvalHandlerSource(const std::string& src, JsValue* out);

    
    
    bool DispatchClick(const Node* target, int x = 0, int y = 0);
    bool DispatchEvent(const std::string& type, const Node* target,
                       bool bubbles = true);
    
    void DispatchLoad();

    
    bool RunTimers();
    
    int NextTimerDelayMs() const;
    int TimerCount() const { return (int)timers_.size(); }

    
    bool TakeDirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }
    void MarkDirty() { dirty_ = true; }

    
    std::string TakePendingNavigation();
    bool TakeReloadRequest();

    const std::string& LastError() const { return error_; }
    const Interp::Stats& Stats() const { return interp_.stats(); }
    size_t ListenerCount() const;
    const std::vector<std::string>& ScriptLog() const { return script_log_; }

    
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

    
    EventData* NewEvent(const std::string& type, const JsValue& target);
    JsValue MakeEventObject(EventData* ev, const JsValue& current);
    bool FireOnNode(Node* node, const std::string& type, EventData* ev);

    
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

    std::vector<std::pair<std::string, std::string>> pending_;  
    std::vector<std::string> script_log_;

    std::map<const Node*, JsValue> wrappers_;
    std::map<const Node*, std::vector<Listener>> listeners_;
    
    std::map<const Node*, std::map<std::string, JsValue>> inline_handlers_;
    std::vector<std::unique_ptr<Node>> detached_;
    std::vector<Timer> timers_;
    std::vector<std::shared_ptr<EventData>> live_events_;
    int next_timer_id_ = 1;
    
    std::vector<Listener> document_listeners_;
    std::vector<Listener> window_listeners_;
    JsValue window_obj_;
    JsValue document_obj_;
    JsValue location_obj_;
    JsValue body_obj_;
};

}  
