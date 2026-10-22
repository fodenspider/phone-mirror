# Development status

Updated: 2026-10-22. Milestone: local capture playback through the Windows media backend.

## Implemented

- Independent C++ and Kotlin message header codecs with type-specific allocation bounds.
- Incremental C++ parser for fragmented and coalesced input; fatal handling of malformed streams.
- Shared wire fixture, truncation/bounds/version tests and deterministic header mutations.
- Native Windows local-capture player with timestamp pacing, aspect-preserving scaling, stop/reopen and software-decode option.
- Windows Media Foundation H.264 decoder connected to D3D11 video processing / DXGI presentation.
- GPU surface output avoids per-frame CPU readback; software output has an explicit upload path.
- Strict capture configuration, sequence, timestamp and initial-IDR validation.
- Offline framing inspector plus an actual decode/render verification tool.
- Android user-consented, foreground-service Surface/H.264 capture probe with 15-second and 32-MiB limits.
- Android privacy-preserving default: no network service, remote-control permission or background startup.
- Build/test workflow and development documentation.

## Not implemented / not claimed

- No real-time phone image in the Windows client yet.
- No transport/session authentication, remote gestures, IME or audio yet.
- No measured device compatibility, latency or power figures.
- A single device capture has been decoded successfully; permission revocation, broader encoder behavior and other devices still need testing.

## Verified build baseline

- Local Windows MinGW-w64 Release build and CTest passed.
- Commit `005003e`: GitHub Windows/MSVC and Linux builds/tests passed; Android unit tests, lint and debug APK build passed.
- [Verified CI run and downloadable development artifacts](https://github.com/fodenspider/phone-mirror/actions/runs/39596351558).
- Android 16 / API 36 on iQOO Z9 Turbo: exported 134 frames at 576×1280 and validated sequence/timestamps.
- Local GPU-surface and software decoder paths both decoded/rendered all 134 frames. Their final rendered frames match exactly.
- Hidden-window playback smoke test completed playback, resized, reopened, cancelled and exited successfully.
- A fresh USB-exported capture was retested: all 349 frames decoded on both paths, final BMP files matched, and hidden-window playback/resize/reopen/cancel passed. [Retest details](benchmarks/2026-10-22-usb-retest.md).
- No latency or power consumption result is claimed. See the dated benchmark record for limits.

## Next vertical slice

1. Add authenticated loopback sessions through ADB forwarding, bounded video queues and keyframe recovery.
2. Connect the validated decoder to live input; measure phone-to-display latency.
3. Add a separately consented accessibility control prototype and test dragging before promising full control.

Repository source is original to this project; earlier scrcpy documents are research history only.
