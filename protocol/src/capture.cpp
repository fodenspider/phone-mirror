#include "phone_mirror/capture.hpp"
#include <limits>

namespace pm {
namespace {
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at+1]) << 16) |
           (std::uint32_t(b[at+2]) << 8) | b[at+3];
}
bool nal_is(std::span<const std::uint8_t> b, unsigned type) {
    std::size_t prefix = 0;
    if (b.size() >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 1) prefix = 3;
    else if (b.size() >= 5 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1) prefix = 4;
    return prefix && (b[prefix] & 0x80) == 0 && (b[prefix] & 31) == type;
}
bool contains_nal(std::span<const std::uint8_t> b, unsigned type) {
    for (std::size_t i = 0; i + 3 < b.size(); ++i)
        if (nal_is(b.subspan(i), type)) return true;
    return false;
}
}
std::optional<Message> read_message(std::istream& input) {
    std::array<std::uint8_t, header_size> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (input.gcount() == 0 && input.eof()) return std::nullopt;
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) throw ProtocolError("truncated header");
    Message m; m.header = decode_header(bytes);
    m.payload.resize(m.header.payload_length);
    if (!m.payload.empty()) {
        input.read(reinterpret_cast<char*>(m.payload.data()), m.payload.size());
        if (input.gcount() != static_cast<std::streamsize>(m.payload.size())) throw ProtocolError("truncated payload");
    }
    return m;
}
void CaptureValidator::accept(const Message& m) {
    if (m.payload.size() != m.header.payload_length) throw ProtocolError("payload length mismatch");
    if (m.header.sequence != next_) throw ProtocolError("non-contiguous capture sequence");
    if (m.header.type == Type::video_config) {
        if (configured_ || next_ != 0) throw ProtocolError("P0 capture must have one initial configuration");
        const auto b = std::span(m.payload);
        if (b.size() < 20) throw ProtocolError("short video configuration");
        const auto w = u32(b,0), h = u32(b,4), g = u32(b,8), sps = u32(b,12), pps = u32(b,16);
        if (!w || !h || w > 4096 || h > 4096 || (w & 1) || (h & 1) || !g)
            throw ProtocolError("unsupported video dimensions/geometry (even, up to 4096)");
        if (20ull + sps + pps != b.size()) throw ProtocolError("invalid SPS/PPS lengths");
        if (!nal_is(b.subspan(20,sps),7) || !nal_is(b.subspan(20+sps,pps),8))
            throw ProtocolError("expected Annex B SPS and PPS");
        if (!m.header.stream_id || !m.header.stream_epoch) throw ProtocolError("invalid stream identity");
        config_ = {w,h,g,{b.begin()+20,b.end()}};
        stream_ = m.header.stream_id; epoch_ = m.header.stream_epoch; configured_ = true;
    } else if (m.header.type == Type::video_access_unit) {
        if (!configured_) throw ProtocolError("video before configuration");
        if (m.header.stream_id != stream_ || m.header.stream_epoch != epoch_) throw ProtocolError("stream/epoch changed");
        if (m.payload.empty() || !(contains_nal(m.payload,1) || contains_nal(m.payload,5)))
            throw ProtocolError("missing Annex B coded slice");
        if (frames_ == 0) {
            if (!(m.header.flags & 1) || !contains_nal(m.payload,5)) throw ProtocolError("capture must begin with IDR");
            first_pts_ = m.header.timestamp_us;
        }
        if (m.header.timestamp_us > std::uint64_t(std::numeric_limits<std::int64_t>::max()/10) ||
            (frames_ && m.header.timestamp_us < last_pts_)) throw ProtocolError("invalid timestamp");
        // P0 player deliberately caps timeline length, avoiding unbounded waits on hostile files.
        if (m.header.timestamp_us - first_pts_ > 60ull*60*1000000) throw ProtocolError("capture exceeds one-hour playback limit");
        last_pts_ = m.header.timestamp_us; ++frames_;
    } else throw ProtocolError("not a P0 video capture message");
    ++next_;
}
void CaptureValidator::finish() const {
    if (!configured_ || !frames_) throw ProtocolError("capture contains no video");
}
}
