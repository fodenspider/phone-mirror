#include "playback.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <utility>

namespace {
constexpr UINT status_message=WM_APP+1;
struct App {
    HWND window=nullptr,video=nullptr,status=nullptr,compatibility=nullptr;
    std::jthread worker;
    std::mutex mutex;
    std::wstring text=L"打开手机导出的 .pmstream 文件进行本地播放。USB 实时投屏尚未接入。";
    std::filesystem::path pending,smoke_file;
    std::atomic<bool> finished{true},completed{false},failed{false};
    bool closing=false,smoke=false;
    unsigned smoke_phase=0;
    ULONGLONG deadline=0,phase_time=0;
    int exit_code=0;
    void report(std::wstring value) {
        { std::lock_guard lock(mutex); text=std::move(value); }
        PostMessageW(window,status_message,0,0);
    }
    void request_stop() { if(worker.joinable()) worker.request_stop(); }
    void open(const std::filesystem::path& path) {
        pending=path; request_stop(); SetTimer(window,1,30,nullptr);
    }
    void close() { closing=true; pending.clear(); request_stop(); SetTimer(window,1,30,nullptr); }
    void start() {
        const auto path=std::exchange(pending,{});
        const bool software=SendMessageW(compatibility,BM_GETCHECK,0,0)==BST_CHECKED;
        finished=false; completed=false;
        report(software?L"正在播放（兼容解码模式）……":L"正在播放（优先使用 GPU 输出）……");
        phase_time=GetTickCount64();
        worker=std::jthread([this,path,software](std::stop_token token) {
            try {
                pm::PlaybackOptions options; options.window=video; options.paced=true;
                options.hold_last_frame=true; options.software=software;
                pm::play_capture(path,options,token,[this](const pm::PlaybackStats& s) {
                    report(L"播放完成："+std::to_wstring(s.decoded_frames)+L" 帧；GPU 输出 "+
                        std::to_wstring(s.gpu_frames)+L" 帧，软件输出 "+std::to_wstring(s.cpu_frames)+L" 帧。重新打开可再次播放。");
                    completed=true;
                });
            } catch(const std::exception& e) {
                failed=true;
                const std::string error=e.what();
                report(L"播放失败："+std::wstring(error.begin(),error.end())+L"。可勾选兼容解码后重试。");
            }
            finished=true;
        });
    }
    void tick() {
        // Never join a rendering thread while it may synchronously message this window.
        if(worker.joinable() && finished) worker.join();
        if(closing) { if(!worker.joinable()) DestroyWindow(window); return; }
        if(!pending.empty() && !worker.joinable()) start();
        if(smoke) {
            if(failed || GetTickCount64()>deadline) { exit_code=1; close(); return; }
            if(smoke_phase==0 && completed) {
                SetWindowPos(window,nullptr,0,0,950,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                phase_time=GetTickCount64(); smoke_phase=1;
            } else if(smoke_phase==1 && GetTickCount64()-phase_time>400) {
                open(smoke_file); smoke_phase=2;
            } else if(smoke_phase==2 && pending.empty() && !completed && GetTickCount64()-phase_time>300) {
                request_stop(); smoke_phase=3;
            } else if(smoke_phase==3 && !worker.joinable()) close();
        } else if(pending.empty()) KillTimer(window,1);
    }
};
LRESULT CALLBACK video_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT) { PAINTSTRUCT p{}; BeginPaint(window,&p); EndPaint(window,&p); return 0; }
    return DefWindowProcW(window,message,wparam,lparam);
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    switch(message) {
    case WM_NCCREATE:
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); return TRUE;
    case WM_CREATE:
        CreateWindowW(L"BUTTON",L"打开采集文件…",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            12,10,150,32,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"停止",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            174,10,80,32,window,reinterpret_cast<HMENU>(2),nullptr,nullptr);
        app->compatibility=CreateWindowW(L"BUTTON",L"兼容解码",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            272,10,140,32,window,reinterpret_cast<HMENU>(3),nullptr,nullptr);
        app->status=CreateWindowW(L"STATIC",app->text.c_str(),WS_CHILD|WS_VISIBLE,
            12,48,760,44,window,nullptr,nullptr,nullptr);
        app->video=CreateWindowW(L"PhoneMirrorVideo",L"",WS_CHILD|WS_VISIBLE,
            0,96,800,600,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        return 0;
    case WM_SIZE:
        if(app && app->video) {
            const int w=LOWORD(lparam),h=HIWORD(lparam);
            MoveWindow(app->status,12,48,w>24?w-24:1,44,TRUE);
            MoveWindow(app->video,0,96,w,h>96?h-96:1,TRUE);
        } return 0;
    case WM_COMMAND:
        if(LOWORD(wparam)==1) {
            wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
            dialog.lpstrFilter=L"Phone Mirror 采集文件\0*.pmstream\0所有文件\0*.*\0";
            dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&dialog)) app->open(path);
        } else if(LOWORD(wparam)==2) {
            app->pending.clear(); app->request_stop(); app->report(L"已停止。打开采集文件可重新播放。");
        }
        return 0;
    case WM_TIMER: app->tick(); return 0;
    case status_message: {
        std::lock_guard lock(app->mutex); SetWindowTextW(app->status,app->text.c_str()); return 0;
    }
    case WM_CLOSE: app->close(); return 0;
    case WM_DESTROY: KillTimer(window,1); PostQuitMessage(app->exit_code); return 0;
    default: return DefWindowProcW(window,message,wparam,lparam);
    }
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDPIAware();
    WNDCLASSW video{}; video.lpfnWndProc=video_proc; video.hInstance=instance; video.lpszClassName=L"PhoneMirrorVideo";
    video.hCursor=LoadCursorW(nullptr,IDC_ARROW); if(!RegisterClassW(&video)) return 1;
    WNDCLASSW wc{}; wc.lpfnWndProc=window_proc; wc.hInstance=instance; wc.lpszClassName=L"PhoneMirrorWindow";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc)) return 1;
    App app;
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    app.smoke=argv && argc==3 && std::wstring(argv[1])==L"--smoke";
    if(app.smoke) { app.smoke_file=argv[2]; app.deadline=GetTickCount64()+30000; }
    HWND window=CreateWindowW(wc.lpszClassName,L"Phone Mirror · 本地采集播放",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,720,900,nullptr,nullptr,instance,&app);
    if(!window) { if(argv) LocalFree(argv); return 1; }
    ShowWindow(window,app.smoke?SW_HIDE:show);
    if(app.smoke) app.open(app.smoke_file);
    else if(argv && argc==2) app.open(argv[1]);
    if(argv) LocalFree(argv);
    MSG message{}; BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return result==-1?1:static_cast<int>(message.wParam);
}
