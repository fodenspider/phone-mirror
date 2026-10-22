#include "playback.hpp"
#include "usb_client.hpp"
#include "adb_session.hpp"
#include "experience.hpp"
#include "preferences.hpp"
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
    HWND window=nullptr,video=nullptr,status=nullptr,compatibility=nullptr,description=nullptr;
    pm::ui::Preferences preferences;
    std::filesystem::path preferences_path;
    std::vector<pm::Device> available_devices;
    std::wstring selected_device;
    std::jthread worker;
    std::mutex mutex;
    std::wstring text=L"用数据线连接手机，然后点击“连接手机”。首次使用需要在手机上确认授权。";
    HFONT font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    HBRUSH background=CreateSolidBrush(RGB(247,248,245));
    std::atomic<pm::ui::Screen> screen{pm::ui::Screen::home};
    pm::ui::Failure last_failure=pm::ui::failure("");
    unsigned guide_step=0;
    bool ui_smoke=false;
    bool preview=false;
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
    static std::filesystem::path folder() { wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768); return std::filesystem::path(path).parent_path(); }
    int px(int value) const { return MulDiv(value,GetDpiForWindow(window),96); }
    void save_preferences() {
        if(smoke || ui_smoke || preview || preferences_path.empty()) return;
        preferences.software=SendMessageW(compatibility,BM_GETCHECK,0,0)==BST_CHECKED;
        preferences.guide_step=guide_step;
        if(!pm::ui::write_preferences(preferences_path,preferences)) report(L"本次设置可用，但未能保存到电脑。请检查用户目录是否可写。");
    }
    void check_preparation() {
        bool components=false;
        try { components=std::filesystem::is_regular_file(pm::AdbSession::locate()); } catch(...) {}
        std::error_code ec;
        const bool apk=std::filesystem::is_regular_file(folder()/L"companion/phone-mirror.apk",ec);
        const auto message=pm::ui::readiness(components,apk);
        MessageBoxW(window,message.c_str(),L"检查准备情况",MB_OK|MB_ICONINFORMATION);
    }
    void refresh_devices() {
        if(!finished) return;
        if(worker.joinable()) worker.join();
        {std::lock_guard lock(mutex); available_devices.clear();}
        finished=false; screen=pm::ui::Screen::connecting;
        report(L"正在刷新手机列表，可点击停止取消……");
        SetTimer(window,1,30,nullptr);
        worker=std::jthread([this](std::stop_token stop) {
            try {
                pm::AdbSession session(pm::AdbSession::locate());
                auto devices=session.devices(stop); const auto count=devices.size();
                {std::lock_guard lock(mutex); available_devices=std::move(devices);}
                screen=pm::ui::Screen::home;
                report(count?L"列表已更新。请打开“更多 → 选择手机”，选择后再连接或安装手机应用。":L"没有发现手机。请检查数据线和手机授权，再刷新列表。");
            } catch(const std::exception& e) {
                if(stop.stop_requested()) { screen=pm::ui::Screen::stopped; report(L"已取消刷新手机列表。"); }
                else { const auto problem=pm::ui::failure(e.what()); {std::lock_guard lock(mutex); last_failure=problem;} screen=pm::ui::Screen::error; report(problem.message); }
            }
            finished=true; PostMessageW(window,status_message,0,0);
        });
    }
    void layout() {
        RECT r{}; GetClientRect(window,&r); const int w=r.right,h=r.bottom;
        MoveWindow(GetDlgItem(window,20),px(24),px(20),w-px(48),px(28),TRUE);
        for(auto [id,x,width]:{std::array<int,3>{4,24,130},{2,168,80},{7,270,110},{1,398,80}})
            MoveWindow(GetDlgItem(window,id),px(x),px(60),px(width),px(36),TRUE);
        MoveWindow(status,px(24),px(110),std::max(1,w-px(48)),px(58),TRUE);
        MoveWindow(video,px(24),px(180),std::max(1,w-px(48)),std::max(1,h-px(264)),TRUE);
        RECT content{}; GetClientRect(video,&content);
        MoveWindow(description,px(20),px(20),std::max<int>(1,content.right-px(40)),std::max<int>(1,content.bottom-px(40)),TRUE);
        MoveWindow(GetDlgItem(window,8),px(24),h-px(64),px(160),px(36),TRUE);
        MoveWindow(GetDlgItem(window,9),w-px(242),h-px(64),px(96),px(36),TRUE);
        MoveWindow(GetDlgItem(window,10),w-px(128),h-px(64),px(104),px(36),TRUE);
    }
    void refresh_ui() {
        const auto state=screen.load(); const bool guide=state==pm::ui::Screen::guide;
        ShowWindow(description,state==pm::ui::Screen::playing?SW_HIDE:SW_SHOW);
        std::wstring title=L"把手机画面，轻轻放到电脑上";
        std::wstring body=L"连接数据线，点击上方“连接手机”。\r\n\r\n第一次使用？打开“使用指南”，跟随四个步骤完成准备。\r\n\r\n“更多 → 检查准备情况”可检查本机组件。画面只传到本机电脑，不保存录像，鼠标控制需单独授权。";
        if(guide) { title=pm::ui::steps[guide_step].title; body=pm::ui::steps[guide_step].body; }
        else if(state==pm::ui::Screen::connecting) { title=L"正在准备"; body=L"请留意窗口上方和手机上的提示。\r\n\r\n点击“停止”可以取消等待，准备好后可以再次尝试。"; }
        else if(state==pm::ui::Screen::error) { title=L"暂时没有完成"; body=L"请按上方提示检查后重试。\r\n\r\n可以打开使用指南，或从“更多”复制不含设备标识的诊断信息。"; }
        else if(state==pm::ui::Screen::stopped) { title=L"已停止"; body=L"此窗口已停止接收画面或等待操作。\r\n\r\n点击“连接手机”可重新开始，每次共享都需要你在手机确认。"; }
        std::wstring content=title+L"\r\n\r\n";
        for(size_t i=0;i<body.size();++i) { if(body[i]==L'\n' && (i==0 || body[i-1]!=L'\r')) content+=L'\r'; content+=body[i]; }
        const int length=GetWindowTextLengthW(description);
        std::wstring previous(length+1,L'\0'); GetWindowTextW(description,previous.data(),length+1); previous.resize(length);
        if(previous!=content) SetWindowTextW(description,content.c_str());
        ShowWindow(GetDlgItem(window,8),guide?SW_SHOW:SW_HIDE);
        ShowWindow(GetDlgItem(window,9),guide?SW_SHOW:SW_HIDE);
        ShowWindow(GetDlgItem(window,10),guide?SW_SHOW:SW_HIDE);
        if(guide) SetWindowTextW(GetDlgItem(window,8),pm::ui::steps[guide_step].action);
        EnableWindow(GetDlgItem(window,9),guide_step>0);
        EnableWindow(GetDlgItem(window,10),guide_step+1<pm::ui::steps.size());
        EnableWindow(GetDlgItem(window,4),finished);
        EnableWindow(GetDlgItem(window,7),finished);
        EnableWindow(GetDlgItem(window,2),!finished);
        if(state!=pm::ui::Screen::playing) InvalidateRect(video,nullptr,TRUE);
    }
    void show_guide() { if(!finished) return; screen=pm::ui::Screen::guide; refresh_ui(); }
    void connect_phone() { pending.clear(); pending_usb.reset(); pending_auto=true; request_stop(); SetTimer(window,1,30,nullptr); }
    void auxiliary(bool install,std::filesystem::path zip={}) {
        if(!finished) return; if(worker.joinable()) worker.join();
        screen=pm::ui::Screen::connecting; finished=false;
        report(install?L"正在安装手机应用，请留意手机上的确认提示。可点击停止取消等待。":L"正在校验并导入连接组件……");
        SetTimer(window,1,30,nullptr);
        worker=std::jthread([this,install,zip,selected=selected_device](std::stop_token stop) {
            try {
                if(install) pm::AdbSession::install_companion(folder()/L"companion/phone-mirror.apk",stop,selected);
                else {
                    auto script=folder()/L"scripts/import-components.ps1";
                    if(!std::filesystem::exists(script)) script=folder()/L"../scripts/import-components.ps1";
                    pm::AdbSession::import_components(script,zip,stop);
                }
                screen=pm::ui::Screen::home; report(install?L"手机应用已安装。现在可以连接并授权共享屏幕。":L"连接组件已准备好。请继续使用指南的下一步。");
            } catch(const std::exception& e) {
                if(stop.stop_requested()) { screen=pm::ui::Screen::stopped; report(L"已取消等待。若手机已经出现安装确认，请在手机上决定是否继续。"); }
                else { const auto problem=pm::ui::failure(e.what()); {std::lock_guard lock(mutex); last_failure=problem;} screen=pm::ui::Screen::error; report(problem.message); }
            }
            finished=true; PostMessageW(window,status_message,0,0);
        });
    }
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
    void close() { save_preferences(); closing=true; pending.clear(); request_stop(); SetTimer(window,1,30,nullptr); }
    void start() {
        const auto path=std::exchange(pending,{});
        const auto endpoint=std::exchange(pending_usb,std::nullopt);
        const bool automatic=std::exchange(pending_auto,false);
        const bool software=SendMessageW(compatibility,BM_GETCHECK,0,0)==BST_CHECKED;
        finished=false; completed=false; failed=false;
        screen=pm::ui::Screen::connecting;
        report(endpoint?L"正在连接手机 USB 会话……":software?L"正在播放（兼容解码模式）……":L"正在播放（优先使用 GPU 输出）……");
        phase_time=GetTickCount64();
        worker=std::jthread([this,path,software,endpoint,automatic,selected=selected_device](std::stop_token token) {
            try {
                auto live_endpoint=endpoint;
                std::unique_ptr<pm::AdbSession> session;
                if(automatic) {
                    session=std::make_unique<pm::AdbSession>(pm::AdbSession::locate());
                    live_endpoint=session->connect(token,[this](std::wstring message) { report(std::move(message)); },selected);
                }
                pm::PlaybackOptions options; options.window=video; options.paced=true;
                options.hold_last_frame=true; options.software=software;
                options.on_progress=[this](const pm::PlaybackStats&) { screen=pm::ui::Screen::playing; PostMessageW(window,status_message,0,0); };
                if(live_endpoint) {
                    options.paced=false; options.hold_last_frame=false;
                    options.on_progress=[this](const pm::PlaybackStats& s) {
                        screen=pm::ui::Screen::playing;
                        { std::lock_guard lock(mutex); video_width=s.width; video_height=s.height; }
                        report(L"USB 已显示 "+std::to_wstring(s.decoded_frames)+L" 帧。控制需手机授权：左键点击、拖动松开滑动、滚轮滚动。");
                    };
                    auto client=std::make_shared<pm::UsbClient>(*live_endpoint,token);
                    { std::lock_guard lock(mutex); live_client=client; }
                    const auto stats=pm::play_stream([&] { return client->next(); },options,token);
                    screen=pm::ui::Screen::stopped;
                    report(L"USB 共享已结束，共显示 "+std::to_wstring(stats.decoded_frames)+L" 帧。重新连接需在手机再次授权。");
                } else pm::play_capture(path,options,token,[this](const pm::PlaybackStats& s) {
                    report(L"播放完成："+std::to_wstring(s.decoded_frames)+L" 帧；GPU 输出 "+
                        std::to_wstring(s.gpu_frames)+L" 帧，软件输出 "+std::to_wstring(s.cpu_frames)+L" 帧。重新打开可再次播放。");
                    completed=true;
                });
            } catch(const std::exception& e) {
                failed=true;
                const std::string error=e.what();
                if(token.stop_requested() || error=="CANCELLED") { screen=pm::ui::Screen::stopped; report(L"已停止。准备好后可重新连接手机。"); }
                else { const auto problem=pm::ui::failure(error); {std::lock_guard lock(mutex); last_failure=problem;} screen=pm::ui::Screen::error; report(problem.message); }
            }
            { std::lock_guard lock(mutex); live_client.reset(); video_width=video_height=0; }
            finished=true;
            if(token.stop_requested()) screen=pm::ui::Screen::stopped;
            PostMessageW(window,status_message,0,0);
        });
    }
    void tick() {
        if(ui_smoke) {
            if(++smoke_phase<5) {
                guide_step=(smoke_phase-1)%4; show_guide();
                SetWindowPos(window,nullptr,0,0,px(smoke_phase%2?520:900),px(smoke_phase%2?420:820),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                RECT parent{},child{}; GetClientRect(video,&parent); GetWindowRect(description,&child);
                MapWindowPoints(nullptr,video,reinterpret_cast<POINT*>(&child),2);
                if(GetWindowTextLengthW(description)<30 || child.left<0 || child.top<0 || child.right>parent.right || child.bottom>parent.bottom) { exit_code=1; close(); }
                return;
            }
            if(smoke_phase==5) {
                available_devices={{L"fake-one",L"模拟手机 1",true},{L"fake-two",L"模拟手机 2",true}};
                SendMessageW(window,WM_COMMAND,101,0);
                if(selected_device!=L"fake-two") exit_code=1;
                SendMessageW(window,WM_COMMAND,16,0);
                if(!selected_device.empty()) exit_code=1;
                available_devices.clear();
                screen=pm::ui::Screen::error; report(pm::ui::failure("DEVICE_UNAUTHORIZED").message); return;
            }
            close();
        }
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
    if(message==WM_CTLCOLORSTATIC && app) {
        SetBkColor(reinterpret_cast<HDC>(wparam),RGB(255,255,255));
        SetTextColor(reinterpret_cast<HDC>(wparam),RGB(47,60,55));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT p{}; const auto dc=BeginPaint(window,&p);
        if(app && app->screen!=pm::ui::Screen::playing) {
            RECT r{}; GetClientRect(window,&r); FillRect(dc,&r,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        }
        EndPaint(window,&p); return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    switch(message) {
    case WM_NCCREATE:
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); return TRUE;
    case WM_CREATE:
        DeleteObject(app->font); app->font=CreateFontW(-app->px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        CreateWindowW(L"STATIC",L"Phone Mirror  ·  让手机与你的电脑相连",WS_CHILD|WS_VISIBLE,
            24,20,650,28,window,reinterpret_cast<HMENU>(20),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"使用指南",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            270,60,110,36,window,reinterpret_cast<HMENU>(7),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"获取组件",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,
            24,700,160,36,window,reinterpret_cast<HMENU>(8),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"上一步",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,
            400,700,96,36,window,reinterpret_cast<HMENU>(9),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"下一步",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,
            510,700,104,36,window,reinterpret_cast<HMENU>(10),nullptr,nullptr);
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
        SetWindowLongPtrW(app->video,GWL_EXSTYLE,GetWindowLongPtrW(app->video,GWL_EXSTYLE)|WS_EX_CONTROLPARENT);
        app->description=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
            20,20,600,400,app->video,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(app->compatibility,BM_SETCHECK,app->preferences.software?BST_CHECKED:BST_UNCHECKED,0);
        EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL { SendMessageW(child,WM_SETFONT,data,TRUE); return TRUE; },reinterpret_cast<LPARAM>(app->font));
        app->refresh_ui();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wparam),RGB(47,60,55));
        SetBkColor(reinterpret_cast<HDC>(wparam),RGB(247,248,245));
        return reinterpret_cast<LRESULT>(app->background);
    case WM_DRAWITEM: {
        const auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if(draw->CtlID!=4) break;
        const COLORREF color=(draw->itemState&ODS_DISABLED)?RGB(143,156,147):(draw->itemState&ODS_SELECTED)?RGB(63,91,78):RGB(82,113,96);
        const auto brush=CreateSolidBrush(color); const auto old=SelectObject(draw->hDC,brush);
        const auto pen=SelectObject(draw->hDC,GetStockObject(NULL_PEN));
        RoundRect(draw->hDC,draw->rcItem.left,draw->rcItem.top,draw->rcItem.right,draw->rcItem.bottom,12,12);
        SelectObject(draw->hDC,old); SelectObject(draw->hDC,pen); DeleteObject(brush);
        SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,RGB(255,255,255));
        const auto old_font=SelectObject(draw->hDC,app->font);
        RECT r=draw->rcItem; DrawTextW(draw->hDC,L"连接手机",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(draw->hDC,old_font);
        if(draw->itemState&ODS_FOCUS) { InflateRect(&r,-5,-5); DrawFocusRect(draw->hDC,&r); }
        return TRUE;
    }
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(window,&r); FillRect(reinterpret_cast<HDC>(wparam),&r,app->background); return 1; }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize={app?app->px(520):520,app?app->px(420):420}; return 0;
    case WM_DPICHANGED: {
        const auto r=reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        const auto old=app->font; app->font=CreateFontW(-app->px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL { SendMessageW(child,WM_SETFONT,data,TRUE); return TRUE; },reinterpret_cast<LPARAM>(app->font)); DeleteObject(old);
        app->layout(); return 0;
    }
    case WM_SIZE:
        if(app && app->video) {
            app->layout();
            if(wparam==SIZE_RESTORED) { RECT r{}; GetWindowRect(window,&r);
                app->preferences.width=MulDiv(r.right-r.left,96,GetDpiForWindow(window));
                app->preferences.height=MulDiv(r.bottom-r.top,96,GetDpiForWindow(window)); }
        } return 0;
    case WM_COMMAND:
        if(LOWORD(wparam)==1) {
            const auto menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,5,L"打开本地采集文件…");
            AppendMenuW(menu,MF_STRING,11,L"导入连接组件…");
            AppendMenuW(menu,MF_STRING,12,L"复制诊断信息");
            AppendMenuW(menu,MF_STRING,13,L"关于 Phone Mirror");
            AppendMenuW(menu,MF_STRING,14,L"检查准备情况");
            AppendMenuW(menu,MF_STRING|(app->finished?0:MF_GRAYED),15,L"刷新手机列表");
            const auto devices_menu=CreatePopupMenu();
            AppendMenuW(devices_menu,MF_STRING|(app->selected_device.empty()?MF_CHECKED:0),16,L"自动选择（仅一台时）");
            {std::lock_guard lock(app->mutex);
                for(size_t i=0;i<app->available_devices.size();++i) {
                    const auto& d=app->available_devices[i];
                    AppendMenuW(devices_menu,MF_STRING|(d.serial==app->selected_device?MF_CHECKED:0),100+i,d.label.c_str());
                }
            }
            AppendMenuW(menu,MF_POPUP|(app->finished?0:MF_GRAYED),reinterpret_cast<UINT_PTR>(devices_menu),L"选择手机");
            AppendMenuW(menu,MF_STRING|(SendMessageW(app->compatibility,BM_GETCHECK,0,0)==BST_CHECKED?MF_CHECKED:0),6,L"兼容解码（下次连接生效）");
            RECT r{}; GetWindowRect(GetDlgItem(window,1),&r);
            TrackPopupMenu(menu,TPM_LEFTALIGN,r.left,r.bottom,0,window,nullptr); DestroyMenu(menu);
        } else if(LOWORD(wparam)==15) { app->refresh_devices();
        } else if(LOWORD(wparam)==16 && app->finished) { app->selected_device.clear(); app->report(L"已改为自动选择：仅检测到一台手机时才会连接或安装。");
        } else if(LOWORD(wparam)>=100 && LOWORD(wparam)<228 && app->finished) {
            std::wstring label;
            {std::lock_guard lock(app->mutex); const size_t index=LOWORD(wparam)-100;
                if(index<app->available_devices.size()) { app->selected_device=app->available_devices[index].serial; label=app->available_devices[index].label; }
            }
            if(!label.empty()) app->report(L"已选择："+label+L"。连接和安装均只针对该手机。");
        } else if(LOWORD(wparam)==14) { app->check_preparation();
        } else if(LOWORD(wparam)==7) { app->show_guide();
        } else if(LOWORD(wparam)==9 || LOWORD(wparam)==10) {
            if(LOWORD(wparam)==9 && app->guide_step) --app->guide_step;
            if(LOWORD(wparam)==10 && app->guide_step+1<pm::ui::steps.size()) ++app->guide_step;
            app->refresh_ui();
            app->save_preferences();
        } else if(LOWORD(wparam)==8) {
            if(app->guide_step==0) ShellExecuteW(window,L"open",L"https://developer.android.com/tools/releases/platform-tools",nullptr,nullptr,SW_SHOWNORMAL);
            else if(app->guide_step==1) app->auxiliary(true);
            else if(app->guide_step==2) app->connect_phone();
            else { app->screen=pm::ui::Screen::home; app->refresh_ui(); }
        } else if(LOWORD(wparam)==11 && app->finished) {
            wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
            dialog.lpstrFilter=L"Android 官方连接组件\0*.zip\0"; dialog.lpstrFile=path; dialog.nMaxFile=32768;
            dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&dialog)) app->auxiliary(false,path);
        } else if(LOWORD(wparam)==12) {
            std::wstring text; {std::lock_guard lock(app->mutex); text=pm::ui::diagnostics(app->last_failure);}
            const auto memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
            if(memory) { const auto data=GlobalLock(memory); if(data) { memcpy(data,text.c_str(),(text.size()+1)*sizeof(wchar_t)); GlobalUnlock(memory);
                if(OpenClipboard(window)) { EmptyClipboard(); if(SetClipboardData(CF_UNICODETEXT,memory)) { CloseClipboard(); return 0; } CloseClipboard(); } } GlobalFree(memory); }
        } else if(LOWORD(wparam)==13) {
            MessageBoxW(window,L"Phone Mirror 0.4.1 预览版\n\n独立实现的 Android → Windows 投屏工具。\n当前支持 USB 画面；点击与滑动待真机验收。\n预览包尚未使用正式发布签名。",L"关于 Phone Mirror",MB_OK|MB_ICONINFORMATION);
        } else if(LOWORD(wparam)==6) {
            SendMessageW(app->compatibility,BM_SETCHECK,SendMessageW(app->compatibility,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);
            app->save_preferences();
        } else if(LOWORD(wparam)==5) {
            wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
            dialog.lpstrFilter=L"Phone Mirror 采集文件\0*.pmstream\0所有文件\0*.*\0";
            dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&dialog)) app->open(path);
        } else if(LOWORD(wparam)==2) {
            app->pending_auto=false; app->pending.clear(); app->pending_usb.reset(); app->request_stop(); app->report(L"正在停止共享。");
        } else if(LOWORD(wparam)==4) {
            app->connect_phone();
        }
        return 0;
    case WM_TIMER: app->tick(); return 0;
    case status_message: {
        {std::lock_guard lock(app->mutex); SetWindowTextW(app->status,app->text.c_str());} app->refresh_ui(); return 0;
    }
    case WM_CLOSE: app->close(); return 0;
    case WM_DESTROY: KillTimer(window,1); PostQuitMessage(app->exit_code); return 0;
    default: return DefWindowProcW(window,message,wparam,lparam);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW video{}; video.lpfnWndProc=video_proc; video.hInstance=instance; video.lpszClassName=L"PhoneMirrorVideo";
    video.hCursor=LoadCursorW(nullptr,IDC_ARROW); if(!RegisterClassW(&video)) return 1;
    WNDCLASSW wc{}; wc.lpfnWndProc=window_proc; wc.hInstance=instance; wc.lpszClassName=L"PhoneMirrorWindow";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc)) return 1;
    App app;
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    app.smoke=argv && argc==3 && std::wstring(argv[1])==L"--smoke";
    app.ui_smoke=argv && argc==2 && std::wstring(argv[1])==L"--ui-smoke";
    app.preview=argv && argc==2 && std::wstring(argv[1])==L"--preview";
    if(!app.smoke && !app.ui_smoke && !app.preview) {
        if(const auto local=_wgetenv(L"LOCALAPPDATA")) app.preferences_path=std::filesystem::path(local)/L"PhoneMirror/settings.txt";
        app.preferences=pm::ui::read_preferences(app.preferences_path); app.guide_step=app.preferences.guide_step;
    }
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
    const auto initial_preferences=app.preferences;
    HWND window=CreateWindowW(wc.lpszClassName,L"Phone Mirror · USB 实时预览",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,app.preferences.width,app.preferences.height,nullptr,nullptr,instance,&app);
    if(!window) { if(argv) LocalFree(argv); return 1; }
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=std::min<int>(app.px(initial_preferences.width),monitor.rcWork.right-monitor.rcWork.left);
    const int height=std::min<int>(app.px(initial_preferences.height),monitor.rcWork.bottom-monitor.rcWork.top);
    SetWindowPos(window,nullptr,monitor.rcWork.left,monitor.rcWork.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(window,(app.smoke || app.ui_smoke)?SW_HIDE:show);
    app.layout();
    if(app.smoke) app.open(app.smoke_file);
    else if(app.ui_smoke) SetTimer(window,1,100,nullptr);
    else if(app.pending_usb) SetTimer(window,1,30,nullptr);
    else if(argv && argc==2 && !app.preview) app.open(argv[1]);
    if(argv) LocalFree(argv);
    MSG message{}; BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0) { if(!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
    return result==-1?1:static_cast<int>(message.wParam);
}
