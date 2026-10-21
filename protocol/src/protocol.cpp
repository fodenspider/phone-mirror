#include "phone_mirror/protocol.hpp"
#include <algorithm>
#include <limits>

namespace pm {
namespace {
void put(std::span<std::uint8_t> out, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        out[offset + i] = static_cast<std::uint8_t>(value >> (8 * (count - 1 - i)));
}
std::uint64_t get(std::span<const std::uint8_t> in, std::size_t offset, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value = (value << 8) | in[offset + i];
    return value;
}
void validate(const Header& h) {
    if (h.payload_length > payload_limit(h.type)) throw ProtocolError("payload exceeds type limit");
    const auto allowed = h.type == Type::video_access_unit ? 1u : 0u;
    if ((h.flags & ~allowed) != 0) throw ProtocolError("unsupported flags");
}
}
std::uint32_t payload_limit(Type type) {
    switch (type) {
    case Type::video_access_unit: return 8 * 1024 * 1024;
    case Type::text_commit: return 16 * 1024;
    case Type::hello: case Type::capabilities: case Type::start: case Type::stop:
    case Type::video_config: case Type::audio_config: case Type::audio_block:
    case Type::pointer_action: case Type::global_action: case Type::ack:
    case Type::status: case Type::ping: case Type::pong: case Type::request_keyframe:
        return 64 * 1024;
    }
    throw ProtocolError("unknown message type");
}
std::array<std::uint8_t, header_size> encode_header(const Header& h) {
    validate(h);
    std::array<std::uint8_t, header_size> out{};
    put(out, 0, magic, 4); put(out, 4, version, 2);
    put(out, 6, static_cast<std::uint16_t>(h.type), 2);
    put(out, 8, h.flags, 4); put(out, 12, h.payload_length, 4);
    put(out, 16, h.stream_id, 4); put(out, 20, h.stream_epoch, 4);
    put(out, 24, h.sequence, 8); put(out, 32, h.timestamp_us, 8);
    return out;
}
Header decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != header_size) throw ProtocolError("header must be exactly 40 bytes");
    if (get(bytes, 0, 4) != magic) throw ProtocolError("invalid magic");
    if (get(bytes, 4, 2) != version) throw ProtocolError("unsupported protocol version");
    Header h;
    h.type = static_cast<Type>(get(bytes, 6, 2));
    h.flags = static_cast<std::uint32_t>(get(bytes, 8, 4));
    h.payload_length = static_cast<std::uint32_t>(get(bytes, 12, 4));
    h.stream_id = static_cast<std::uint32_t>(get(bytes, 16, 4));
    h.stream_epoch = static_cast<std::uint32_t>(get(bytes, 20, 4));
    h.sequence = get(bytes, 24, 8); h.timestamp_us = get(bytes, 32, 8);
    validate(h);
    return h;
}
std::vector<std::uint8_t> encode(const Message& message) {
    if (message.payload.size() != message.header.payload_length)
        throw ProtocolError("payload length mismatch");
    auto header = encode_header(message.header);
    std::vector<std::uint8_t> result(header.begin(), header.end());
    result.insert(result.end(), message.payload.begin(), message.payload.end());
    return result;
}
void Parser::feed(std::span<const std::uint8_t> bytes, const Consumer& consumer) {
    if (failed_) throw ProtocolError("parser is poisoned after an earlier failure");
    try {
        while (!bytes.empty()) {
            if (header_used_ < header_size) {
                auto n = std::min(header_size - header_used_, bytes.size());
                std::copy_n(bytes.begin(), n, header_bytes_.begin() + header_used_);
                header_used_ += n; bytes = bytes.subspan(n);
                if (header_used_ != header_size) continue;
                current_.header = decode_header(header_bytes_);
                // Allocation happens only after the type-specific bound check.
                current_.payload.resize(current_.header.payload_length);
                payload_used_ = 0;
            }
            auto n = std::min(current_.payload.size() - payload_used_, bytes.size());
            std::copy_n(bytes.begin(), n, current_.payload.begin() + payload_used_);
            payload_used_ += n; bytes = bytes.subspan(n);
            if (payload_used_ == current_.payload.size()) {
                auto complete = std::move(current_);
                current_ = {}; header_used_ = 0; payload_used_ = 0;
                consumer(std::move(complete));
            }
        }
    } catch (...) { failed_ = true; throw; }
}
void Parser::finish() const {
    if (failed_) throw ProtocolError("parser failed");
    if (header_used_ != 0) throw ProtocolError("truncated message");
}
}
