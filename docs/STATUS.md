# Development status

Updated: 2026-10-21. Milestone: P0 foundations and local capture probe.

## Implemented

- Independent C++ and Kotlin message header codecs with type-specific allocation bounds.
- Incremental C++ parser for fragmented and coalesced input; fatal handling of malformed streams.
- Shared wire fixture, truncation/bounds/version tests and deterministic header mutations.
- Native Windows development window and command-line offline stream inspector.
- Android user-consented, foreground-service Surface/H.264 capture probe with 15-second and 32-MiB limits.
- Android privacy-preserving default: no network service, remote-control permission or background startup.
- Build/test workflow and development documentation.

## Not implemented / not claimed

- No real-time phone image in the Windows client yet.
- No transport/session authentication, decoder, GPU rendering, remote gestures, IME or audio yet.
- No measured device compatibility, latency or power figures.
- Real-device capture, permissions and encoder behavior still need verification; an APK build is not device validation.

## Verified build baseline

- Local Windows MinGW-w64 Release build and CTest passed.
- Commit `005003e`: GitHub Windows/MSVC and Linux builds/tests passed; Android unit tests, lint and debug APK build passed.
- [Verified CI run and downloadable development artifacts](https://github.com/fodenspider/phone-mirror/actions/runs/39596351558).
- No latency, power consumption or real-device compatibility result is claimed.

## Next vertical slice

1. Install the built Android APK and collect a real target-device capture; validate codec config and access units.
2. Implement offline H.264 decoding to D3D11 output as a testable Windows media backend.
3. Add authenticated loopback sessions through ADB forwarding, bounded video queues and keyframe recovery.
4. Connect that same decoder to live input; measure phone-to-display latency.
5. Add a separately consented accessibility control prototype and test dragging before promising full control.

Repository source is original to this project; earlier scrcpy documents are research history only.
