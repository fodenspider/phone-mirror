#pragma once
#include <array>
#include <string>
#include <string_view>

namespace pm::ui {
enum class Screen { home, guide, connecting, playing, stopped, error };
struct Step { const wchar_t* title; const wchar_t* body; const wchar_t* action; };
inline constexpr std::array<Step,4> steps{{
    {L"1 / 4   准备连接组件",L"首次使用需要安装连接组件。\n\n点击“获取组件”打开 Android 官方下载页面，下载 Windows 版 Platform-Tools。然后在“更多”里选择“导入连接组件”，选中下载的 ZIP 文件。\n\n组件仅保存在这台电脑上；不需要安装开发环境或输入命令。",L"获取组件"},
    {L"2 / 4   安装手机应用",L"用数据线连接并解锁手机，在手机设置中打开“开发者选项 → USB 调试”。出现确认提示时，允许这台电脑连接。\n\n点击下方按钮安装随程序提供的手机应用。若手机弹出安装提示，请确认。已有应用会尝试保留数据更新，签名不兼容时不会自动卸载。",L"安装手机应用"},
    {L"3 / 4   开始共享画面",L"点击“开始连接”，手机上的 Phone Mirror 会自动打开。\n\n在手机点击“USB 实时投屏”，允许共享整个屏幕，电脑便会显示画面。每次新的共享都需要你在手机上确认。\n\n等待期间可随时取消；拔线或停止后，可重新连接。",L"开始连接"},
    {L"4 / 4   可选：用鼠标操作",L"只看画面，不需要额外控制权限。\n\n如果想用电脑点击或滑动，在手机应用中打开“点击与滑动权限”，按系统提示启用 Phone Mirror 的无障碍服务，并勾选“本次允许电脑点击与滑动”。\n\n左键点击；拖动后松开执行滑动；滚轮滚动。当前不支持中文输入、音频和连续跟手拖动。",L"完成引导"}
}};
struct Failure { std::wstring message; std::string code; };
inline Failure failure(std::string_view code) {
    if(code=="DEVICE_MISSING") return {L"还没有发现手机。请检查数据线，解锁手机并打开 USB 调试，然后重试。","DEVICE_MISSING"};
    if(code=="DEVICE_OFFLINE") return {L"手机连接已离线。请重新插接数据线、解锁手机，然后刷新列表重试。","DEVICE_OFFLINE"};
    if(code=="DEVICE_UNAUTHORIZED") return {L"请在手机上允许这台电脑连接，然后重试。","DEVICE_UNAUTHORIZED"};
    if(code=="DEVICE_MULTIPLE") return {L"检测到多台手机。请在“更多”中刷新手机列表，然后选择要使用的手机。","DEVICE_MULTIPLE"};
    if(code=="DEVICE_SELECTED_MISSING") return {L"选中的手机已断开。请重连这台手机，或在“更多”中刷新列表并重新选择。","DEVICE_SELECTED_MISSING"};
    if(code=="ADB_MISSING") return {L"连接组件尚未准备好。请打开使用指南，完成第一步。","ADB_MISSING"};
    if(code=="PHONE_APP_MISSING" || code=="APK_MISSING") return {L"手机应用尚未准备好。请打开使用指南，完成第二步；若安装文件缺失，请重新安装完整程序包。","PHONE_APP_MISSING"};
    if(code=="INSTALL_FAILED") return {L"手机应用未安装成功。请查看手机上的提示，允许安装后重试；旧应用不会被自动卸载。","INSTALL_FAILED"};
    if(code=="PHONE_CONSENT_TIMEOUT") return {L"等待手机授权已超时。准备好后重新连接，并在手机上允许共享屏幕。","PHONE_CONSENT_TIMEOUT"};
    if(code=="ADB_TIMEOUT") return {L"手机暂时没有响应。请检查数据线和手机提示，然后重试。","ADB_TIMEOUT"};
    if(code=="COMPONENT_INSTALL") return {L"连接组件没有导入成功。请选择官方 Windows 版 Platform-Tools ZIP 文件；如果版本已变化，请更新 Phone Mirror。","COMPONENT_INSTALL"};
    return {L"连接或播放已中断。请检查连接后重试；仍有问题时，可在“更多”中复制诊断信息。","SESSION_FAILURE"};
}
inline std::wstring diagnostics(const Failure& value) {
    // Deliberately excludes arbitrary exception text, paths, device IDs and credentials.
    return L"Phone Mirror 0.6.1 preview\r\nWindows desktop\r\nCode: "+std::wstring(value.code.begin(),value.code.end())+L"\r\n";
}
}
