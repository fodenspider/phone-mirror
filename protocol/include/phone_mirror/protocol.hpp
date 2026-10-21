#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace pm {
constexpr std::uint32_t magic = 0x504d4952; // PMIR
constexpr std::uint16_t version = 1;
constexpr std::size_t header_size = 40;
enum class Type : std::uint16_t {
    hello = 1, capabilities = 2, start = 3, stop = 4,
    video_config = 10, video_access_unit = 11,
    audio_config = 20, audio_block = 21,
    pointer_action = 30, text_commit = 31, global_action = 32,
    ack = 40, status = 41, ping = 50, pong = 51, request_keyframe = 52
};
struct Header {
    Type type = Type::hello;
    std::uint32_t flags = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t stream_id = 0;
    std::uint32_t stream_epoch = 0;
    std::uint64_t sequence = 0;
    std::uint64_t timestamp_us = 0;
};
struct Message { Header header; std::vector<std::uint8_t> payload; };
class ProtocolError : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
std::uint32_t payload_limit(Type type);
std::array<std::uint8_t, header_size> encode_header(const Header& header);
Header decode_header(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode(const Message& message);

// Incremental framing only. Authentication and per-channel ordering belong to
// the session layer. A malformed stream is fatal; never scan ahead for magic.
class Parser {
public:
    using Consumer = std::function<void(Message&&)>;
    void feed(std::span<const std::uint8_t> bytes, const Consumer& consumer);
    void finish() const;
    [[nodiscard]] bool failed() const { return failed_; }
private:
    std::array<std::uint8_t, header_size> header_bytes_{};
    std::size_t header_used_ = 0;
    Message current_;
    std::size_t payload_used_ = 0;
    bool failed_ = false;
};
}
