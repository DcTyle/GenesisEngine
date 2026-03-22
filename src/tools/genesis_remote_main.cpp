#include <cstdio>
#include <string>

#include "ew_cli_args.hpp"
#include "GE_remote_control.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::wstring utf8_to_wide_local(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}
#endif

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "genesis_remote: malformed args\n");
        return 2;
    }

    auto get_str_def = [&](const char* k, const char* defv) -> std::string {
        std::string out;
        if (ew::ew_cli_get_str(args, k, out)) return out;
        return std::string(defv ? defv : "");
    };
    auto get_u32_def = [&](const char* k, uint32_t defv) -> uint32_t {
        uint32_t out = defv;
        (void)ew::ew_cli_get_u32(args, k, out);
        return out;
    };

    std::string command = get_str_def("command", "");
    if (command.empty()) command = get_str_def("cmd", "");

    const std::string bootstrap = get_str_def("bootstrap", "");
    const std::string chat = get_str_def("chat", "");
    if (command.empty() && !bootstrap.empty()) command = std::string("bootstrap:") + bootstrap;
    if (command.empty() && !chat.empty()) command = std::string("chat:") + chat;
    if (command.empty()) {
        std::fprintf(stderr, "genesis_remote: command=<text> | bootstrap=<name> | chat=<text> required\n");
        return 2;
    }

#if defined(_WIN32)
    const std::string window_class_utf8 = get_str_def("window_class", "GenesisEngineWnd");
    const uint32_t timeout_ms = get_u32_def("timeout_ms", 3000u);
    const std::wstring window_class_w = utf8_to_wide_local(window_class_utf8);
    HWND hwnd = FindWindowW(window_class_w.empty() ? GE_REMOTE_WINDOW_CLASS_W : window_class_w.c_str(), nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "genesis_remote: target window not found class=%s\n", window_class_utf8.c_str());
        return 3;
    }

    COPYDATASTRUCT cds{};
    cds.dwData = GE_REMOTE_COPYDATA_MAGIC;
    cds.cbData = (DWORD)(command.size() + 1u);
    cds.lpData = (PVOID)command.c_str();

    DWORD_PTR result = 0;
    const LRESULT ok = SendMessageTimeoutW(hwnd,
                                           WM_COPYDATA,
                                           0,
                                           (LPARAM)&cds,
                                           SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                           timeout_ms,
                                           &result);
    if (!ok) {
        std::fprintf(stderr, "genesis_remote: send failed timeout_ms=%u last_error=%lu\n",
                     (unsigned)timeout_ms,
                     (unsigned long)GetLastError());
        return 4;
    }

    std::printf("GENESIS_REMOTE:sent class=%s bytes=%u\n",
                window_class_utf8.c_str(),
                (unsigned)cds.cbData);
    return 0;
#else
    std::fprintf(stderr, "genesis_remote: Windows-only tool\n");
    return 5;
#endif
}
