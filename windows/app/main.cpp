#include "playback.hpp"
#include "usb_client.hpp"
#include "adb_session.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <utility>
#include <windowsx.h>

namespace {
constexpr UINT status_message=WM_APP+1;
struct App {
    HWND window=nullptr,video=nullptr,status=nullptr,compatibility=nullptr;
    std::jthread worker;
    std::mutex mutex;
    std::wstring text=L"用数据线连接手机，然后点击“连接手机”。首次使用需要在手机上确认授权。";
    HFONT font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    HBRUSH background=CreateSolidBrush(RGB(247,248,245));
    bool pending_auto=false;
    std::filesystem::path pending,smoke_file;
    std::optional<pm::UsbEndpoint> pending_usb;
    std::shared_ptr<pm::UsbClient> live_client;
    unsigned video_width=0,video_height=0;
    std::optional<pm::Point> drag_start;
    ULONGLONG drag_time=0,last_wheel=0;
    std::atomic<bool> finished{true},completed{false},failed{false};
    bool closing=false,smoke=false;
    unsigned smoke_phase=0;
    ULONGLONG deadline=0,phase_time=0;
    int exit_code=0;
    ~App() { DeleteObject(font); DeleteObject(background); }
    void report(std::wstring value) {
        { std::lock_guard lock(mutex); text=std::move(value); }
        PostMessageW(window,status_message,0,0);
    }
    void request_stop() { drag_start.reset(); if(worker.joinable()) worker.request_stop(); }
    std::optional<pm::Point> point(int x,int y) {
        RECT r{}; GetClientRect(video,&r); std::lock_guard lock(mutex);
        if(!live_client) return std::nullopt;
        return pm::map_pointer(x,y,r.right,r.bottom,video_width,video_height);
    }
    void gesture(const pm::Gesture& value) {
        std::shared_ptr<pm::UsbClient> client;
        { std::lock_guard lock(mutex); client=live_client; }
        if(client && !client->send_gesture(value)) report(L"控制队列忙，请稍后重试。手机需开启本次控制权限。");
    }
    void open(const std::filesystem::path& path) {
        pending_auto=false; pending_usb.reset(); pending=path; request_stop(); SetTimer(window,1,30,nullptr);
    }
    void close() { closing=true; pending.clear(); request_stop(); SetTimer(window,1,30,nullptr); }
    void start() {
        const auto path=std::exchange(pending,{});
        const auto endpoint=std::exchange(pending_usb,std::nullopt);
        const bool automatic=std::exchange(pending_auto,false);
        const bool software=SendMessageW(compatibility,BM_GETCHECK,0,0)==BST_CHECKED;
        finished=false; completed=false; failed=false;
        report(endpoint?L"正在连接手机 USB 会话……":software?L"正在播放（兼容解码模式）……":L"正在播放（优先使用 GPU 输出）……");
        phase_time=GetTickCount64();
        worker=std::jthread([this,path,software,endpoint,automatic](std::stop_token token) {
            try {
                auto live_endpoint=endpoint;
                std::unique_ptr<pm::AdbSession> session;
                if(automatic) {
                    session=std::make_unique<pm::AdbSession>(pm::AdbSession::locate());
                    live_endpoint=session->connect(token,[this](std::wstring message) { report(std::move(message)); });
                }
                pm::PlaybackOptions options; options.window=video; options.paced=true;
                options.hold_last_frame=true; options.software=software;
                if(live_endpoint) {
                    options.paced=false; options.hold_last_frame=false;
                    options.on_progress=[this](const pm::PlaybackStats& s) {
                        { std::lock_guard lock(mutex); video_width=s.width; video_height=s.height; }
                        report(L"USB 已显示 "+std::to_wstring(s.decoded_frames)+L" 帧。控制需手机授权：左键点击、拖动松开滑动、滚轮滚动。");
                    };
                    auto client=std::make_shared<pm::UsbClient>(*live_endpoint,token);
                    { std::lock_guard lock(mutex); live_client=client; }
                    const auto stats=pm::play_stream([&] { return client->next(); },options,token);
                    report(L"USB 共享已结束，共显示 "+std::to_wstring(stats.decoded_frames)+L" 帧。重新连接需在手机再次授权。");
                } else pm::play_capture(path,options,token,[this](const pm::PlaybackStats& s) {
                    report(L"播放完成："+std::to_wstring(s.decoded_frames)+L" 帧；GPU 输出 "+
                        std::to_wstring(s.gpu_frames)+L" 帧，软件输出 "+std::to_wstring(s.cpu_frames)+L" 帧。重新打开可再次播放。");
                    completed=true;
                });
            } catch(const std::exception& e) {
                failed=true;
                const std::string error=e.what();
                if(token.stop_requested() || error=="CANCELLED") report(L"已停止。准备好后可重新连接手机。");
                else if(error=="DEVICE_MISSING") report(L"还没有发现手机。请插好数据线、解锁手机并开启 USB 调试，然后重新连接。");
                else if(error=="DEVICE_UNAUTHORIZED") report(L"请在手机上允许这台电脑进行 USB 调试，然后重新连接。");
                else if(error=="DEVICE_MULTIPLE") report(L"检测到多台手机。此预览版请先只连接一台，再点击连接手机。");
                else if(error=="ADB_MISSING") report(L"连接组件缺失。请使用完整程序包；开发版需将 platform-tools 放在程序旁。");
                else if(error=="PHONE_APP_MISSING") report(L"手机尚未安装伴侣应用。请安装 Phone Mirror 手机应用后重试。");
                else if(error=="PHONE_CONSENT_TIMEOUT") report(L"等待共享授权已超时。准备好手机后，请重新连接。");
                else if(automatic || endpoint) report(L"连接已中断。请检查数据线并重新授权共享后重试。诊断："+std::wstring(error.begin(),error.end()));
                else report(L"暂时无法播放此文件，可选择兼容解码后重试。");
            }
            { std::lock_guard lock(mutex); live_client.reset(); video_width=video_height=0; }
            finished=true;
        });
    }
    void tick() {
        // Never join a rendering thread while it may synchronously message this window.
        if(worker.joinable() && finished) worker.join();
        if(closing) { if(!worker.joinable()) DestroyWindow(window); return; }
        if((!pending.empty() || pending_usb || pending_auto) && !worker.joinable()) start();
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
        } else if(pending.empty() && !pending_usb && !pending_auto && !worker.joinable()) KillTimer(window,1);
    }
};
LRESULT CALLBACK video_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(GetParent(window),GWLP_USERDATA));
    if(app) {
        if(message==WM_LBUTTONDOWN) {
            app->drag_start=app->point(GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam));
            if(app->drag_start) { app->drag_time=GetTickCount64(); SetCapture(window); SetFocus(window); }
            return 0;
        }
        if(message==WM_LBUTTONUP) {
            const auto start=app->drag_start;
            const auto end=app->point(GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam));
            app->drag_start.reset(); ReleaseCapture();
            if(start && end) {
                const bool tap=std::abs(int(start->x)-int(end->x))<500 && std::abs(int(start->y)-int(end->y))<500;
                app->gesture({tap?1u:2u,*start,tap?*start:*end,tap?50u:static_cast<unsigned>(std::clamp<ULONGLONG>(GetTickCount64()-app->drag_time,80,300))});
            }
            return 0;
        }
        if(message==WM_MOUSEWHEEL) {
            POINT p{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ScreenToClient(window,&p);
            if(app->point(p.x,p.y) && GetTickCount64()-app->last_wheel>=250) {
                app->last_wheel=GetTickCount64(); const bool down=GET_WHEEL_DELTA_WPARAM(wparam)<0;
                app->gesture({2,{32768,down?45875u:19660u},{32768,down?19660u:45875u},180});
            }
            return 0;
        }
        if(message==WM_CAPTURECHANGED || message==WM_SIZE) app->drag_start.reset();
    }
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
        CreateWindowW(L"STATIC",L"Phone Mirror  ·  让手机与你的电脑相连",WS_CHILD|WS_VISIBLE,
            24,20,650,28,window,nullptr,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"连接手机",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            24,60,130,36,window,reinterpret_cast<HMENU>(4),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"更多",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            270,60,90,36,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"停止",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            168,60,80,36,window,reinterpret_cast<HMENU>(2),nullptr,nullptr);
        app->compatibility=CreateWindowW(L"BUTTON",L"兼容解码",WS_CHILD|BS_AUTOCHECKBOX,
            440,60,140,36,window,reinterpret_cast<HMENU>(3),nullptr,nullptr);
        app->status=CreateWindowW(L"STATIC",app->text.c_str(),WS_CHILD|WS_VISIBLE,
            24,110,760,44,window,nullptr,nullptr,nullptr);
        app->video=CreateWindowW(L"PhoneMirrorVideo",L"",WS_CHILD|WS_VISIBLE,
            24,168,800,600,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL { SendMessageW(child,WM_SETFONT,data,TRUE); return TRUE; },reinterpret_cast<LPARAM>(app->font));
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam),RGB(47,60,55));
        SetBkColor(reinterpret_cast<HDC>(wparam),RGB(247,248,245));
        return reinterpret_cast<LRESULT>(app->background);
    case WM_DRAWITEM: {
        const auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if(draw->CtlID!=4) break;
        const COLORREF color=(draw->itemState&ODS_SELECTED)?RGB(63,91,78):RGB(82,113,96);
        const auto brush=CreateSolidBrush(color); const auto old=SelectObject(draw->hDC,brush);
        const auto pen=SelectObject(draw->hDC,GetStockObject(NULL_PEN));
        RoundRect(draw->hDC,draw->rcItem.left,draw->rcItem.top,draw->rcItem.right,draw->rcItem.bottom,12,12);
        SelectObject(draw->hDC,old); SelectObject(draw->hDC,pen); DeleteObject(brush);
        SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,RGB(255,255,255));
        RECT r=draw->rcItem; DrawTextW(draw->hDC,L"连接手机",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(draw->itemState&ODS_FOCUS) { InflateRect(&r,-5,-5); DrawFocusRect(draw->hDC,&r); }
        return TRUE;
    }
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(window,&r); FillRect(reinterpret_cast<HDC>(wparam),&r,app->background); return 1; }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize={680,500}; return 0;
    case WM_SIZE:
        if(app && app->video) {
            const int w=LOWORD(lparam),h=HIWORD(lparam);
            MoveWindow(app->status,24,110,w>48?w-48:1,48,TRUE);
            MoveWindow(app->video,24,168,w>48?w-48:1,h>192?h-192:1,TRUE);
        } return 0;
    case WM_COMMAND:
        if(LOWORD(wparam)==1) {
            const auto menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,5,L"打开本地采集文件…");
            AppendMenuW(menu,MF_STRING|(SendMessageW(app->compatibility,BM_GETCHECK,0,0)==BST_CHECKED?MF_CHECKED:0),6,L"兼容解码（下次连接生效）");
            RECT r{}; GetWindowRect(GetDlgItem(window,1),&r);
            TrackPopupMenu(menu,TPM_LEFTALIGN,r.left,r.bottom,0,window,nullptr); DestroyMenu(menu);
        } else if(LOWORD(wparam)==6) {
            SendMessageW(app->compatibility,BM_SETCHECK,SendMessageW(app->compatibility,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);
        } else if(LOWORD(wparam)==5) {
            wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
            dialog.lpstrFilter=L"Phone Mirror 采集文件\0*.pmstream\0所有文件\0*.*\0";
            dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&dialog)) app->open(path);
        } else if(LOWORD(wparam)==2) {
            app->pending_auto=false; app->pending.clear(); app->pending_usb.reset(); app->request_stop(); app->report(L"正在停止共享。");
        } else if(LOWORD(wparam)==4) {
            app->pending.clear(); app->pending_usb.reset(); app->pending_auto=true;
            app->request_stop(); SetTimer(window,1,30,nullptr);
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
    return DefWindowProcW(window,message,wparam,lparam);
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
    if(argv && argc==3 && std::wstring(argv[1])==L"--usb") {
        try {
            std::string secret(32,'\0'); DWORD used=0,total=0;
            while(total<32 && ReadFile(GetStdHandle(STD_INPUT_HANDLE),secret.data()+total,32-total,&used,nullptr) && used) total+=used;
            if(total!=32) throw pm::ProtocolError("USB credential missing");
            std::size_t consumed=0; const auto port=std::stoul(argv[2],&consumed);
            if(consumed!=std::wstring(argv[2]).size() || port>65535) throw pm::ProtocolError("invalid port");
            app.pending_usb=pm::usb_endpoint(static_cast<unsigned>(port),secret);
            SecureZeroMemory(secret.data(),secret.size());
        } catch(...) { LocalFree(argv); return 2; }
    }
    HWND window=CreateWindowW(wc.lpszClassName,L"Phone Mirror · USB 实时预览",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,720,900,nullptr,nullptr,instance,&app);
    if(!window) { if(argv) LocalFree(argv); return 1; }
    ShowWindow(window,app.smoke?SW_HIDE:show);
    if(app.smoke) app.open(app.smoke_file);
    else if(app.pending_usb) SetTimer(window,1,30,nullptr);
    else if(argv && argc==2) app.open(argv[1]);
    if(argv) LocalFree(argv);
    MSG message{}; BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0) { if(!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
    return result==-1?1:static_cast<int>(message.wParam);
}
