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
            else if(mode!="none") std::cout<<"test-phone\tdevice\n";
            if(mode=="multiple") std::cout<<"test-two\tdevice\n";
            return 0;
        }
        if(argc<4 || std::string(argv[1])!="-s" || std::string(argv[2])!="test-phone") return 1;
        const std::string operation=argv[3];
        if(operation=="shell") { if(mode=="missing") { std::cout<<"Error type 3\n"; return 1; } return 0; }
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
        expect("multiple","DEVICE_MULTIPLE"); expect("missing","PHONE_APP_MISSING");
        SetEnvironmentVariableA("PM_TEST_ADB_STATE","ready");
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
