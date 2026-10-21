#include <windows.h>

namespace {
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Phone Mirror / P0", WS_CHILD | WS_VISIBLE,
                      28, 24, 650, 28, window, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC",
            L"Independent Android-to-Windows mirroring prototype.\n\n"
            L"Implemented: bounded PMIR protocol parser and Android capture probe.\n"
            L"Not connected yet: USB transport, hardware decode and remote control.\n\n"
            L"This build does not display your phone screen.\n"
            L"Use pm-inspect to validate a capture.pmstream exported from the Android probe.",
            WS_CHILD | WS_VISIBLE, 28, 70, 700, 210, window, nullptr, nullptr, nullptr);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    WNDCLASSW wc{}; wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.lpszClassName = L"PhoneMirrorWindow"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;
    auto window = CreateWindowW(wc.lpszClassName, L"Phone Mirror - P0 development build",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 400,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message{}; BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
