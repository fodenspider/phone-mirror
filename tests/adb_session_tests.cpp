#include "adb_session.hpp"
#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc,char** argv) {
    if(argc>1) { // An isolated fake ADB process; never invokes a real device tool.
        char state[64]{}; GetEnvironmentVariableA("PM_TEST_ADB_STATE",state,64); const std::string mode=state;
        if(std::string(argv[1])=="devices") {
            std::cout<<"List of devices attached\n";
            if(mode=="unauthorized") std::cout<<"test-phone\tunauthorized\n";
            else if(mode=="offline") std::cout<<"test-phone\toffline\n";
            else if(mode!="none" && mode!="gone") std::cout<<"test-phone\tdevice product:test model:Test_Phone\n";
            if(mode=="multiple" || mode=="gone") std::cout<<"test-two\tdevice product:test model:Second_Phone\n";
            if(mode=="mixed") std::cout<<"test-two\tunauthorized\n";
            return 0;
        }
        char target[64]{}; GetEnvironmentVariableA("PM_TEST_TARGET",target,64);
        const std::string expected=*target?target:"test-phone";
        if(argc<4 || std::string(argv[1])!="-s" || std::string(argv[2])!=expected) return 1;
        const std::string operation=argv[3];
        if(operation=="install") {
            if(argc!=7 || std::string(argv[4])!="--no-streaming" || std::string(argv[5])!="-r") return 1;
            if(mode=="install-failed") {std::cout<<"Failure [TEST_ONLY]\n"; return 1;}
            std::cout<<"Success\n"; return 0;
        }
        if(operation=="shell") {
            if(mode=="missing") { std::cout<<"Error type 3\n"; return 1; }
            char expectedQuality[8]{}; GetEnvironmentVariableA("PM_TEST_QUALITY",expectedQuality,8);
            if(argc!=13 || std::string(argv[10])!="--ei" || std::string(argv[11])!="quality" || std::string(argv[12])!=(*expectedQuality?expectedQuality:"1")) return 1;
            return 0;
        }
        if(operation=="exec-out") {
            if(mode=="waiting") return 1;
            std::cout<<"{\"version\":1,\"port\":1234,\"token\":\"11111111111111111111111111111111\"}"; return 0;
        }
        if(operation=="forward") { if(argc>4 && std::string(argv[4])!="--remove") std::cout<<"4321\n"; return 0; }
        return 1;
    }
    try {
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
        const auto expect=[&](const char* mode,const char* error) {
            SetEnvironmentVariableA("PM_TEST_ADB_STATE",mode);
            try { pm::AdbSession session(path); session.connect({},[](std::wstring){}); }
            catch(const std::exception& e) { if(std::string(e.what())==error) return; throw; }
            throw std::runtime_error("missing expected state error");
        };
        expect("none","DEVICE_MISSING"); expect("unauthorized","DEVICE_UNAUTHORIZED");
        expect("offline","DEVICE_OFFLINE");
        expect("multiple","DEVICE_MULTIPLE"); expect("missing","PHONE_APP_MISSING");
        expect("mixed","DEVICE_MULTIPLE");
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","multiple");
        SetEnvironmentVariableA("PM_TEST_TARGET","test-two");
        { pm::AdbSession session(path); const auto devices=session.devices({});
          if(devices.size()!=2 || devices[1].label.find(L"Second Phone")==std::wstring::npos) throw std::runtime_error("device list mismatch");
          const auto endpoint=session.connect({},[](std::wstring){},L"test-two");
          if(endpoint.port!=4321) throw std::runtime_error("selected connection failed");
          // This executable is a stand-in file; only the fake subprocess receives it.
          session.install(path,{},L"test-two"); }
        SetEnvironmentVariableA("PM_TEST_TARGET",nullptr);
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","gone");
        for(bool install:{false,true}) {
            try { pm::AdbSession session(path);
                if(install) session.install(path,{},L"test-phone");
                else session.connect({},[](std::wstring){},L"test-phone");
                throw std::runtime_error("selected device silently changed");
            } catch(const std::exception& e) { if(std::string(e.what())!="DEVICE_SELECTED_MISSING") throw; }
        }
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","mixed");
        try { pm::AdbSession session(path); session.install(path,{},L"test-two"); throw std::runtime_error("unauthorized install accepted"); }
        catch(const std::exception& e) {if(std::string(e.what())!="DEVICE_UNAUTHORIZED") throw;}
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","install-failed");
        try { pm::AdbSession session(path); session.install(path,{}); throw std::runtime_error("failed install accepted"); }
        catch(const std::exception& e) {if(std::string(e.what())!="INSTALL_FAILED") throw;}
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","ready");
        for(int quality=0;quality<3;++quality) {
            SetEnvironmentVariableA("PM_TEST_QUALITY",std::to_string(quality).c_str());
            pm::AdbSession session(path); session.connect({},[](std::wstring){},{},quality);
        }
        SetEnvironmentVariableA("PM_TEST_QUALITY",nullptr);
        try { pm::AdbSession session(path); session.connect({},[](std::wstring){},{},3); throw std::runtime_error("invalid quality accepted"); }
        catch(const std::exception& e) {if(std::string(e.what())!="QUALITY_INVALID") throw;}
        { pm::AdbSession session(path); const auto endpoint=session.connect({},[](std::wstring){});
          if(endpoint.port!=4321 || endpoint.token[0]!=0x11) throw std::runtime_error("endpoint mismatch"); }
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","waiting");
        std::stop_source stop;
        std::jthread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(250)); stop.request_stop(); });
        const auto start=std::chrono::steady_clock::now();
        try { pm::AdbSession session(path); session.connect(stop.get_token(),[](std::wstring){}); throw std::runtime_error("missing cancellation"); }
        catch(const std::exception& e) { if(std::string(e.what())!="CANCELLED") throw; }
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(2)) throw std::runtime_error("slow cancellation");
        SetEnvironmentVariableA("PM_TEST_ADB_STATE",nullptr);
        std::cout<<"Desktop connection states and cancellation passed (fake device only)\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
