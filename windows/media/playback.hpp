#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <cstdint>

namespace pm {
struct PlaybackStats {
    std::uint64_t input_frames = 0, decoded_frames = 0, gpu_frames = 0, cpu_frames = 0;
    unsigned width = 0, height = 0;
    bool d3d_manager = false;
};
struct PlaybackOptions {
    HWND window = nullptr; // null = offscreen verification, no visible window
    bool software = false;
    bool paced = false;
    bool hold_last_frame = false;
    std::filesystem::path snapshot; // optional final rendered BMP; diagnostic only
};
PlaybackStats play_capture(const std::filesystem::path&, const PlaybackOptions&,
    std::stop_token = {}, std::function<void(const PlaybackStats&)> on_complete = {});
}
