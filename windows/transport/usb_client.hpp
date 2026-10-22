#pragma once
#include "phone_mirror/protocol.hpp"
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>

namespace pm {
struct UsbEndpoint {
    std::uint16_t port = 0;
    std::array<std::uint8_t,16> token{};
};
UsbEndpoint usb_endpoint(unsigned port, std::string_view hex);
// Cancellable, bounded reads over an ADB loopback forward. Never accepts LAN hosts.
class UsbClient {
public:
    explicit UsbClient(UsbEndpoint endpoint, std::stop_token stop = {});
    ~UsbClient();
    UsbClient(const UsbClient&) = delete;
    UsbClient& operator=(const UsbClient&) = delete;
    std::optional<Message> next();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
