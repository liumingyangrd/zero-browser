#include "engine.h"
#include "network.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class ProbeCanvas : public zb::Canvas {
public:
    size_t MeasureText(const std::string& utf8, int font_size, bool bold,
                       int max_width = 0) const override {
        (void)max_width;
        return (size_t)utf8.size() * (size_t)(font_size / 2 + (bold ? 1 : 0));
    }
    void DrawText(const std::string&, int, int, int, uint32_t, bool, bool,
                  bool) override {}
    void FillRect(int, int, int, int, uint32_t) override {}
    void FillRoundRect(int, int, int, int, int, uint32_t) override {}
    void StrokeRect(int, int, int, int, uint32_t) override {}
    void StrokeLine(int, int, int, int, uint32_t, int) override {}
    void StrokeArc(const zb::Rect&, int, int, uint32_t) override {}
    void Clip(const zb::Rect&) override {}
    void ResetClip() override {}
    void DrawImage(const uint8_t*, int, int, int, int, int, int) override {}
};

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

void DumpRuns(const zb::Box* box, int depth) {
    if (!box) return;
    std::printf("%*sbox tag=%s rect=%d,%d,%d,%d hidden=%d\n", depth * 2, "",
                box->node ? box->node->tag.c_str() : "(anon)", box->rect.x,
                box->rect.y, box->rect.w, box->rect.h, box->hidden ? 1 : 0);
    for (const auto& r : box->runs) {
        std::printf("%*srun x=%d y=%d w=%d color=%s text=[%s]\n", depth * 2, "",
                    r.rect.x, r.rect.y, r.rect.w, r.color.c_str(),
                    r.text.c_str());
    }
    for (const auto& c : box->children) DumpRuns(c.get(), depth + 1);
}

}  // namespace

int main(int argc, char** argv) {
    std::string source = argc > 1 ? argv[1] : "http://example.com/";
    std::string html;
    std::string url = source;
    if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) {
        zb::FetchResult res;
        zb::FetchUrl(source, &res, 15000);
        html = res.html;
        if (!res.final_url.empty()) url = res.final_url;
        std::printf("fetch status=%d ok=%d bytes=%zu error=%s\n", res.status,
                    res.ok ? 1 : 0, res.html.size(), res.error.c_str());
    } else {
        html = ReadFile(source);
        std::printf("read bytes=%zu\n", html.size());
    }
    if (html.empty()) {
        std::printf("no html\n");
        return 1;
    }

    zb::Page page;
    page.ParseHtml(html, url);
    ProbeCanvas canvas;
    page.Relayout(1000, &canvas);
    std::vector<zb::LinkArea> links;
    page.CollectLinks(links);
    std::printf("title=%s\n", page.Data().title.c_str());
    std::printf("content_height=%d links=%zu\n", page.ContentHeight(),
                links.size());
    bool dump = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--runs") dump = true;
    }
    if (dump) {
        for (const auto& l : links) {
            std::printf("link x=%d y=%d w=%d h=%d href=%s\n", l.rect.x,
                        l.rect.y, l.rect.w, l.rect.h, l.href.c_str());
        }
        DumpRuns(page.RootBox(), 0);
    }
    std::printf("text_head=");
    std::string plain;
    for (char c : html.substr(0, 200)) {
        plain.push_back((c == '\n' || c == '\r') ? ' ' : c);
    }
    std::printf("%s\n", plain.c_str());
    return 0;
}
