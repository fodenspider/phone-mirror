# P0 validation checklist

## Automated

- Configure and build the C++ targets with CMake; run CTest.
- Test the fixed byte fixture in both languages.
- Reject oversized/unknown headers before allocation; reject unsupported flags and versions.
- Parse every two-part split and byte-at-a-time input; detect truncated messages at EOF.
- Compile and lint the Android app; do not treat host tests as device compatibility evidence.

## Target device (not yet run)

Record phone model, Android version, selected hardware encoder, output dimensions and frame count.

1. Deny projection consent: no capture service or output should start.
2. Grant full-screen consent: persistent foreground notification appears, capture ends within about 15 seconds.
3. Stop via app and notification: encoder/display/projection resources release; no background restart.
4. Revoke consent using system UI: service ends and reports the reason.
5. Rotate/change capture aspect ratio: probe ends and requests a fresh capture, rather than claiming correct input coordinates.
6. Rapidly tap Start and Stop: no overlapping capture ownership or repeated authorization-token use.
7. Export the file only after capture stops. `pm-inspect` validates framing; a future decoder test must also verify playable AVC.
8. Capture again: previous file is intentionally replaced. No screen contents or input text appear in application logs.

Do not commit actual screen captures. The prototype has no network control surface and does not request accessibility permission yet.
