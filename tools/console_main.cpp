#include <windows.h>

#include "app.h"

#include <cstdio>

int main(int argc, char** argv) {
    zb::BrowserApp app(GetModuleHandleA(nullptr));
    bool created = app.CreateMainWindow();
    std::printf("create_window=%d\n", created ? 1 : 0);
    if (!created) return 1;
    const char* url = argc > 1 ? argv[1] : "browser://home";
    app.NavigateTo(url, true);
    std::printf("navigating=%s\n", url);
    std::fflush(stdout);
    int rc = app.Run();
    std::printf("message_loop_exit=%d\n", rc);
    return rc;
}
