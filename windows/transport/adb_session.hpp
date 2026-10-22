#pragma once
#include "usb_client.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace pm {
struct Device { std::wstring serial,label; bool authorized=false,offline=false; };
// Own only the forwarding created for this session; never stop the global ADB server.
class AdbSession {
public:
    explicit AdbSession(std::filesystem::path adb);
    ~AdbSession();
    std::vector<Device> devices(std::stop_token);
    UsbEndpoint connect(std::stop_token, std::function<void(std::wstring)> progress,const std::wstring& selected={});
    void install(const std::filesystem::path& apk,std::stop_token,const std::wstring& selected={});
    static std::filesystem::path locate();
    static void install_companion(const std::filesystem::path& apk,std::stop_token,const std::wstring& selected={});
    static void import_components(const std::filesystem::path& script,const std::filesystem::path& zip,std::stop_token);
private:
    std::filesystem::path adb_;
    std::wstring serial_;
    unsigned local_port_=0;
};
}
