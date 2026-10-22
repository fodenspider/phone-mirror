# P0 validation checklist

## Automated

- Configure and build the C++ targets with CMake; run CTest.
- Test the fixed byte fixture in both languages.
- Reject oversized/unknown headers before allocation; reject unsupported flags and versions.
- Parse every two-part split and byte-at-a-time input; detect truncated messages at EOF.
- Compile and lint the Android app; do not treat host tests as device compatibility evidence.

## Target device (partially verified; see benchmark record)

Record phone model, Android version, selected hardware encoder, output dimensions and frame count.

1. Deny projection consent: no capture service or output should start.
2. Grant full-screen consent: persistent foreground notification appears, capture ends within about 15 seconds.
3. Stop via app and notification: encoder/display/projection resources release; no background restart.
4. Revoke consent using system UI: service ends and reports the reason.
5. Rotate/change capture aspect ratio: probe ends and requests a fresh capture, rather than claiming correct input coordinates.
6. Rapidly tap Start and Stop: no overlapping capture ownership or repeated authorization-token use.
7. Export the file only after capture stops. `pm-inspect` validates framing; `pm-playback-check` verifies decode/render in automatic and `--software` modes.
8. Capture again: previous file is intentionally replaced. No screen contents or input text appear in application logs.

Do not commit actual screen captures. The prototype has no network control surface and does not request accessibility permission yet.

## USB live preview

- Android and Windows socket tests exercise authentication, malformed/truncated input, bounded queues and cancellation. CI success does not imply real-device live success.
- Authorize USB sharing on the phone, then run `scripts/start-usb.ps1 -CheckSeconds 20`. Scroll a non-sensitive page during the test. Record frame counts, dimensions and duration; this is not an end-to-end latency measurement.
- Authorize again and run `scripts/start-usb.ps1` for visible playback. Check aspect ratio, live motion and window resize, then stop from Windows and verify the phone service stops.
- In separate sessions, test phone Stop, system revocation, USB disconnection and rotation. Confirm no background restart and that the launcher's forward is removed after receiver exit.
- Debug APK identity is cached in Actions to support development upgrades. This is not a production signing identity; cache expiry/deletion can require reinstalling the test app. Never use this cache for release keys. Do not uninstall without first preserving needed app-private captures and obtaining user approval.

## Windows playback

- Run CTest for framing and capture semantics, including every truncated-message boundary.
- Decode the same capture normally and with `--software`; compare input/output frame counts and optional final BMP snapshots.
- `PhoneMirror.exe --smoke capture.pmstream` runs a hidden-window test: full paced playback, resize of final frame, reopen, mid-playback cancellation and clean shutdown. The test exits nonzero on error or a 30-second deadline; use a short capture for this test.
- Manually verify the visible window, resizing, stop, replay and readable error states before claiming a finished user interface.
- Do not use private phone captures as public CI fixtures. Hosted CI builds the media backend but has not been assumed to provide a suitable GPU for runtime verification.
