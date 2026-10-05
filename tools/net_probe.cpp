#include "network.h"
#include "common.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::string url = argc > 1 ? argv[1] : "https://example.com/";
    zb::FetchResult res;
    bool ok = zb::FetchUrl(url, &res, 15000);
    std::printf("call_ok=%d status=%d ok=%d\n", ok ? 1 : 0, res.status,
                res.ok ? 1 : 0);
    std::printf("final_url=%s\n", res.final_url.c_str());
    std::printf("content_type=%s\n", res.content_type.c_str());
    std::printf("error=%s\n", res.error.c_str());
    std::printf("bytes=%zu\n", res.html.size());
    std::string head = res.html.substr(0, 300);
    for (char& c : head) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    std::printf("head=%s\n", head.c_str());
    return 0;
}
