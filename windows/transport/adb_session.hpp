#pragma once
#include "usb_client.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace pm {
// Own only the forwarding created for this session; never stop the global ADB server.
class AdbSession {
public:
    explicit AdbSession(std::filesystem::path adb);
    ~AdbSession();
    UsbEndpoint connect(std::stop_token, std::function<void(std::wstring)> progress);
    static std::filesystem::path locate();
private:
    std::filesystem::path adb_;
    std::wstring serial_;
    unsigned local_port_=0;
};
}
