# 0.6 continuous touch preview

## Behavior

With a 0.6 desktop and companion, short stationary clicks use the completed-tap path. Held touch starts only after movement exceeds the normalized 500-unit slop or a stationary hold reaches 500 ms; pointer updates then follow at up to 20 Hz, and button up requests release. Holding stationary enables Android long-press behavior after this intent threshold. Wheel gestures remain bounded completed swipes and are suppressed during a held touch. Four saved wheel travel settings remain available.

Android uses StrokeDescription.continueStroke with 32 ms segments, preserving the previous segment endpoint. Only the latest target is used. A 500 ms heartbeat expiry releases the touch; stop sharing, loss of the session grant, capture loss, leaving the image, focus loss and resizing also request release. These are scheduling bounds, not a measured device response guarantee. Service disable relies on Android cancelling the disconnected service's injected gestures and remains a device acceptance item.

Reference: https://developer.android.com/reference/android/accessibilityservice/GestureDescription.StrokeDescription#continueStroke(android.graphics.Path,long,long,boolean)

## Wire compatibility

The existing 28-byte pointer payload gains actions 3 (down), 4 (update/heartbeat), 5 (up). These use identical start/end coordinates and duration 50; Android uses its segment cadence. Actions 1/2 retain existing behavior. Sequence validation, session-specific grant, bounds, and one-time authentication remain required.

The companion acknowledges authentication with PMOK0002 to advertise continuous touch. The 0.6 desktop accepts PMOK0001 too, falling back to release-triggered completed gestures for older companions; it never sends new actions to an old peer. Old desktop binaries reject PMOK0002, so upgrade the desktop before the companion. Session descriptor format stays version 1.

## Validation status

Protocol and capability regression plus pure Kotlin touch lease tests cover ordering, latest target, end, expiry and revocation. Real service continuation, long press, cancellation and system service disable must still be checked on a phone. The capability marker does not claim device acceptance. No screen content or touch coordinates are logged or published.

## 2026-10-25 build and installation

Commit `f4fd1ee` passed Windows, Linux, Android unit tests/lint and packaging in [CI 40192522679](https://github.com/fodenspider/phone-mirror/actions/runs/40192522679). Local desktop CTest passed all 11 checks.

Initial USB installation and file transfer timed out. After the user changed the connection, a 64 KiB probe passed its hash check, followed by the full APK transfer in 0.39 seconds with matching SHA-256. Android then rejected the update for signature mismatch. Under the user's existing replacement-install authorization, the application's own data was backed up and the old preview replaced. The device reports versionCode 5 / 0.6.0-preview. No other applications or their data were touched. Stable signing remains a release blocker; the cached development identity did not provide a compatible update in this test.

Continuous touch and long-press device acceptance is pending the user's result; installation and compilation do not establish that the gestures behave correctly on the phone.

## Device regression and tap isolation

The user reported that even a short click became a long press with the initial continuous-touch build. Continuous-touch acceptance therefore failed; the underlying continuation/release behavior is not yet proven correct on this device.

A desktop follow-up keeps short stationary clicks out of the held-touch protocol, reserving it for deliberate movement or a hold of at least 500 ms. This avoids relying on continued-stroke release for ordinary clicks; it does not establish the underlying long-press issue's root cause. Local 11-test CTest passed (6.94 seconds), including intent threshold boundaries. The window is labelled “单击修正版” to distinguish the candidate from still-open older builds. The user confirmed ordinary clicks are now normal and no longer become long presses. Long press and drag release remain unaccepted.

## Stationary continuation diagnosis (0.6.1)

The user confirmed ordinary clicks recovered, but dragging moved only a small initial distance, then stopped even with the cursor inside the image. Android's MotionEventInjector rejects an empty event list for a continuation. A zero-distance segment with willContinue=true has neither movement nor release, so repeatedly dispatching these is not a valid way to hold a pointer. This is a source-backed explanation consistent with both symptoms, pending device verification; the device's filtered injector log contained no matching diagnostics.

0.6.1 keeps the already-down pointer when the target is unchanged, checks the lease on a 16 ms scheduled callback, and dispatches only when movement or release is needed. A release still uses a final continued stroke with willContinue=false. No coordinates or screen content are logged. Added a stationary-heartbeat lease regression. Implementation reference: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/accessibility/java/com/android/server/accessibility/MotionEventInjector.java (empty event list rejection and movement generation).
