#pragma once
#include "protocol.hpp"
#include <filesystem>
#include <istream>
#include <optional>

namespace pm {
struct VideoConfig {
    std::uint32_t width = 0, height = 0, geometry = 0;
    std::vector<std::uint8_t> parameter_sets;
};
// Strict offline P0 video semantics, separate from transport framing.
class CaptureValidator {
public:
    void accept(const Message& message);
    void finish() const;
    const VideoConfig& config() const { return config_; }
    std::uint64_t frames() const { return frames_; }
private:
    VideoConfig config_;
    std::uint64_t next_ = 0, frames_ = 0, last_pts_ = 0, first_pts_ = 0;
    std::uint32_t stream_ = 0, epoch_ = 0;
    bool configured_ = false;
};
// One message at a time: no whole-file loading and bounds checked before allocation.
std::optional<Message> read_message(std::istream& input);
}
