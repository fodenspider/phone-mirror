#include "adb_session.hpp"
#include <windows.h>
#include <chrono>
#include <sstream>
#include <regex>
#include <thread>

namespace pm {
namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring quote(const std::wstring& s) {
    std::wstring out=L"\""; unsigned slashes=0;
    for(wchar_t c:s) {
        if(c==L'\\') { ++slashes; continue; }
        out.append(slashes*(c==L'\"'?2:1),L'\\'); slashes=0;
        if(c==L'\"') out+=L'\\'; out+=c;
    }
    out.append(slashes*2,L'\\'); return out+L'\"';
}
struct Result { DWORD code; std::string text; };
std::wstring select_device(const std::vector<Device>& devices,const std::wstring& selected) {
    if(!selected.empty()) {
        for(const auto& device:devices) if(device.serial==selected) {
            if(device.offline) throw std::runtime_error("DEVICE_OFFLINE");
            if(!device.authorized) throw std::runtime_error("DEVICE_UNAUTHORIZED");
            return device.serial;
        }
        throw std::runtime_error("DEVICE_SELECTED_MISSING");
    }
    if(devices.empty()) throw std::runtime_error("DEVICE_MISSING");
    // Never silently pick one authorized phone while another phone awaits authorization.
    if(devices.size()!=1) throw std::runtime_error("DEVICE_MULTIPLE");
    if(devices.front().offline) throw std::runtime_error("DEVICE_OFFLINE");
    if(!devices.front().authorized) throw std::runtime_error("DEVICE_UNAUTHORIZED");
    return devices.front().serial;
}
Result run(const std::filesystem::path& executable,const std::vector<std::wstring>& args,std::stop_token stop={},unsigned timeout=8) {
    if(stop.stop_requested()) throw std::runtime_error("CANCELLED");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE}; Handle read,write;
    if(!CreatePipe(&read.value,&write.value,&security,0) || !SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("ADB_PIPE");
    std::wstring command=quote(executable.wstring());
    for(const auto& arg:args) command+=L" "+quote(arg);
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=startup.hStdError=write.value;
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi))
        throw std::runtime_error("ADB_LAUNCH");
    Handle process{pi.hProcess},thread{pi.hThread}; CloseHandle(write.value); write.value=nullptr;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);
    std::string output;
    for(;;) {
        DWORD available=0;
        if(PeekNamedPipe(read.value,nullptr,0,nullptr,&available,nullptr) && available) {
            char bytes[4096]; DWORD count=0;
            if(ReadFile(read.value,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr)) output.append(bytes,count);
        } else if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0) break;
        if(stop.stop_requested() || std::chrono::steady_clock::now()>=deadline || output.size()>65536) {
            TerminateProcess(process.value,1); WaitForSingleObject(process.value,1000);
            throw std::runtime_error(stop.stop_requested()?"CANCELLED":"ADB_TIMEOUT");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    DWORD code=1; GetExitCodeProcess(process.value,&code); return {code,output};
}
}
AdbSession::AdbSession(std::filesystem::path adb):adb_(std::move(adb)) {}
AdbSession::~AdbSession() {
    if(local_port_) try { run(adb_,{L"-s",serial_,L"forward",L"--remove",L"tcp:"+std::to_wstring(local_port_)}); } catch(...) {}
}
std::filesystem::path AdbSession::locate() {
    wchar_t path[32768]{}; const auto count=GetModuleFileNameW(nullptr,path,32768);
    if(!count || count>=32768) throw std::runtime_error("ADB_MISSING");
    const auto base=std::filesystem::path(path).parent_path();
    if(const auto local=_wgetenv(L"LOCALAPPDATA")) {
        const auto imported=std::filesystem::path(local)/L"PhoneMirror/components/platform-tools-37.0.1/adb.exe";
        if(std::filesystem::is_regular_file(imported)) return imported;
    }
    for(const auto& candidate:{base/L"platform-tools/adb.exe",base/L"adb.exe",base/L"../.tools/platform-tools/adb.exe",base/L"../../.tools/platform-tools/adb.exe"})
        if(std::filesystem::is_regular_file(candidate)) return std::filesystem::weakly_canonical(candidate);
    throw std::runtime_error("ADB_MISSING");
}
std::vector<Device> AdbSession::devices(std::stop_token stop) {
    const auto list=run(adb_,{L"devices",L"-l"},stop);
    if(list.code) throw std::runtime_error("ADB_LAUNCH");
    std::istringstream rows(list.text); std::string line; std::vector<Device> result;
    while(std::getline(rows,line)) {
        std::istringstream row(line); std::string serial,state; row>>serial>>state;
        if(state!="device" && state!="unauthorized" && state!="offline") continue;
        if(serial.empty() || serial.size()>256 || result.size()>=128) throw std::runtime_error("DEVICE_LIST_INVALID");
        std::string model,field;
        while(row>>field) if(field.starts_with("model:")) model=field.substr(6,80);
        for(auto& c:model) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-')) c=' ';
        std::wstring name=model.empty()?L"Android 手机":std::wstring(model.begin(),model.end());
        name+=L" · "+std::to_wstring(result.size()+1);
        name+=state=="device"?L"（可连接）":state=="offline"?L"（离线，请重连）":L"（请在手机授权）";
        result.push_back({std::wstring(serial.begin(),serial.end()),name,state=="device",state=="offline"});
    }
    return result;
}
UsbEndpoint AdbSession::connect(std::stop_token stop,std::function<void(std::wstring)> progress,const std::wstring& selected,int quality) {
    if(quality<0 || quality>2) throw std::runtime_error("QUALITY_INVALID");
    progress(L"正在查找手机……");
    serial_=select_device(devices(stop),selected);
    const auto opened=run(adb_,{L"-s",serial_,L"shell",L"am",L"start",L"-f",L"0x20000000",L"-n",L"io.github.fodenspider.phonemirror/.MainActivity",L"--ei",L"quality",std::to_wstring(quality)},stop);
    if(opened.code || opened.text.find("Error")!=std::string::npos) throw std::runtime_error("PHONE_APP_MISSING");
    progress(L"请在手机点击“USB 实时投屏”并允许共享屏幕。此处会自动连接，你也可以点击停止取消。");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    while(!stop.stop_requested() && std::chrono::steady_clock::now()<deadline) {
        auto response=run(adb_,{L"-s",serial_,L"exec-out",L"run-as",L"io.github.fodenspider.phonemirror",L"cat",L"files/usb-session.json"},stop);
        // Some adb exec-out transports return success even when remote cat reports
        // a missing file. Until consent creates a JSON descriptor, keep waiting.
        const auto first=response.text.find_first_not_of(" \t\r\n");
        if(response.code==0 && first!=std::string::npos && response.text[first]=='{') {
            const std::regex pattern(R"json(^\s*\{"version":1,"port":([0-9]{1,5}),"token":"([0-9a-f]{32})"\}\s*$)json");
            std::smatch match;
            if(!std::regex_match(response.text,match,pattern)) throw std::runtime_error("SESSION_INVALID");
            const auto port=std::stoul(match[1].str());
            auto endpoint=usb_endpoint(port,match[2].str());
            const auto forward=run(adb_,{L"-s",serial_,L"forward",L"tcp:0",L"tcp:"+std::to_wstring(port)},stop);
            if(forward.code || !std::regex_match(forward.text,std::regex("[0-9]{1,5}\\s*"))) throw std::runtime_error("ADB_FORWARD");
            local_port_=std::stoul(forward.text);
            if(!local_port_ || local_port_>65535) throw std::runtime_error("ADB_FORWARD");
            endpoint.port=static_cast<std::uint16_t>(local_port_);
            SecureZeroMemory(response.text.data(),response.text.size());
            return endpoint;
        }
        for(int i=0;i<10 && !stop.stop_requested();++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error(stop.stop_requested()?"CANCELLED":"PHONE_CONSENT_TIMEOUT");
}
void AdbSession::install_companion(const std::filesystem::path& apk,std::stop_token stop,const std::wstring& selected) {
    AdbSession session(locate()); session.install(apk,stop,selected);
}
void AdbSession::install(const std::filesystem::path& apk,std::stop_token stop,const std::wstring& selected) {
    if(!std::filesystem::is_regular_file(apk)) throw std::runtime_error("APK_MISSING");
    const auto target=select_device(devices(stop),selected);
    const auto result=run(adb_,{L"-s",target,L"install",L"--no-streaming",L"-r",apk.wstring()},stop,120);
    if(result.code || result.text.find("Success")==std::string::npos) throw std::runtime_error("INSTALL_FAILED");
}
void AdbSession::import_components(const std::filesystem::path& script,const std::filesystem::path& zip,std::stop_token stop) {
    wchar_t system[32768]{}; if(!GetSystemDirectoryW(system,32768) || !std::filesystem::is_regular_file(script)) throw std::runtime_error("COMPONENT_INSTALL");
    const auto shell=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
    const auto result=run(shell,{L"-NoProfile",L"-NonInteractive",L"-ExecutionPolicy",L"Bypass",L"-File",script.wstring(),L"-Archive",zip.wstring()},stop,60);
    if(result.code) throw std::runtime_error("COMPONENT_INSTALL");
}
}
