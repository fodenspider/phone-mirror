# Development status

Updated: 2026-10-22. Milestone: authenticated USB live-preview implementation; real-device live validation pending.

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
- Android loopback-only USB stream with one-time 128-bit authentication, bounded queue and explicit user consent. No LAN listener, remote-control permission or background startup.
- Windows cancellable USB receiver and launcher; credential passes through an inherited pipe, forwarding is cleaned up at receiver exit.
- Build/test workflow and development documentation.

## Not implemented / not claimed

- USB live code is implemented, but real-device live validation is still pending.
- No remote gestures, IME, audio or automatic keyframe recovery yet.
- No measured device compatibility, latency or power figures.
- A single device capture has been decoded successfully; permission revocation, broader encoder behavior and other devices still need testing.

## Verified build baseline

- USB preview source at `29e72d7`: Android JVM tests/lint/APK, Windows/MSVC tests and Linux tests all passed in [CI run 39762569499](https://github.com/fodenspider/phone-mirror/actions/runs/39762569499).
- Local authenticated loopback replay sent the existing capture at its original timestamps: 349 input / 349 decoded frames, all using GPU surfaces, clean STOP. This validates the receiver integration, not live phone capture or end-to-end latency.
- After the stream-source refactor, the same 349-frame capture still passed GPU/software decoding, identical final-frame comparison and hidden-window resize/reopen/cancel smoke tests.
- Local Windows MinGW-w64 Release build and CTest passed.
- Commit `005003e`: GitHub Windows/MSVC and Linux builds/tests passed; Android unit tests, lint and debug APK build passed.
- [Verified CI run and downloadable development artifacts](https://github.com/fodenspider/phone-mirror/actions/runs/39596351558).
- Android 16 / API 36 on iQOO Z9 Turbo: exported 134 frames at 576×1280 and validated sequence/timestamps.
- Local GPU-surface and software decoder paths both decoded/rendered all 134 frames. Their final rendered frames match exactly.
- Hidden-window playback smoke test completed playback, resized, reopened, cancelled and exited successfully.
- A fresh USB-exported capture was retested: all 349 frames decoded on both paths, final BMP files matched, and hidden-window playback/resize/reopen/cancel passed. [Retest details](benchmarks/2026-10-22-usb-retest.md).
- No latency or power consumption result is claimed. See the dated benchmark record for limits.

## Next vertical slice

1. Validate USB preview on the connected phone, including clean stop and cable disconnection.
2. Measure phone-to-display latency and CPU/memory use; add keyframe recovery after congestion.
3. Add a separately consented accessibility control prototype and test dragging before promising full control.

Repository source is original to this project; earlier scrcpy documents are research history only.
