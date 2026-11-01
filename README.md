# iOSRemoteBridge

High-performance iOS jailbreak remote desktop and AI automation with
hardware-accelerated video streaming and system-wide input control.

[![iOS package](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/build-ios.yml/badge.svg?branch=main)](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/build-ios.yml)
[![Windows client](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/build-client.yml/badge.svg?branch=main)](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/build-client.yml)
[![Tests](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/tests.yml/badge.svg?branch=main)](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/tests.yml)

**Experimental supervised prototype. Native-resolution capture and live Windows
playback have been verified on an iPhone SE 2, iOS 16.1.1, Dopamine and ElleKit.
Home, App Switcher, tap and swipe passed a supervised retest after fixing the
initial input failure.** See the
[dated device report](docs/DEVICE_VALIDATION_2026-11-01.md) for exact commits,
short measurements and remaining gates. Sustained 60 FPS, hardware encoder use,
glass-to-glass latency and long-term power/stability are not verified.
After bounded pipelining, a short Windows sample averaged 59.86 decoded FPS.
The owner confirmed corrected colors and smoother motion; see the report's limits.
The [November 2 native-pointer report](docs/DEVICE_VALIDATION_2026-11-02.md)
records the relative mouse correction and its physical test results.

## Implemented

- Rootless .deb containing a mobile LaunchDaemon and a SpringBoard-only library.
- Bounded IOSurface-backed capture, native rendering/GPU scaling requests,
  VideoToolbox H.264, IDR recovery, 30/60-FPS pacing and bitrate adaptation.
- Pinned TLS, random pairing tokens, automatic paired-client authorization and
  lock/expiry shutdown. No floating control overlay.
- System-HID multi-contact API, gestures, paced US-layout ASCII text, keyboard
  presses, Home/App Switcher, volume and experimental app launch.
- Native Win32 receiver with D3D11VA selection/software fallback, mouse/text
  control, rotation, aspect fitting, adjustable sharpening, quality controls,
  collapsible toolbar, VSync, FSR 1, an optional single-frame FSR 4/3/2.3 experiment,
  navigation shortcuts, captured native iOS relative mouse/keyboard input, reconnect and
  FPS/RTT/decode statistics.
- Python API/CLI, JSON-lines adapter and optional standard MCP stdio tools.
- GitHub Actions builds, TLS/H.264 simulator tests, validation, logs/artifacts
  and a manually gated draft-release workflow.

Other input modes and private-API paths need further device testing. There is one
authenticated session at a time. The transport is TLS/TCP with frame credit, not
Sunshine/Moonlight protocol compatibility. HEVC, audio, WebRTC, automatic app
orientation, arbitrary Unicode, UI hierarchy and a native macOS GUI remain
future work. ScreenCaptureKit is unavailable on iOS 16.1.1.

## Install and run

1. Select successful [iOS](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/build-ios.yml)
   and [Windows](https://github.com/Justintunsday/iOSRemoteBridge/actions/workflows/build-client.yml)
   Actions runs. Download `ios-rootless-deb`, `windows-client-x64` and
   `windows-ffmpeg-corresponding-source`. Artifacts require repository access.
2. Follow [DEVICE_TESTING.md](docs/DEVICE_TESTING.md) to provision over SSH,
   install the package, load the daemon and keep a recovery path available.
3. Extract the entire Windows artifact, launch iOSRemoteBridge.exe, select the
   local pairing JSON and connect to the unlocked iPhone; no app approval dialog.
4. For automation follow [AUTOMATION.md](docs/AUTOMATION.md). Viewing is the
   default; input needs `--control`; paired clients execute operations directly. Lock the iPhone or
   disconnect the client to end the session.

Pairing files stay local. No Mac must remain connected after installation.
A normal reboot requires jailbreak reactivation. Sleep/lock ends the session;
the implementation does not bypass a passcode or disable auto-lock.

## Engineering evidence

| Document | Contents |
| --- | --- |
| [RESEARCH](docs/RESEARCH.md) | Pinned source analysis, API availability, licenses and technology choices |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | Actual modules, trust boundaries and frame lifetimes |
| [COMPATIBILITY](docs/COMPATIBILITY.md) | Compile targets and runtime validation gates |
| [SECURITY](docs/SECURITY.md) | Authentication, automatic operation policy and threat-model limits |
| [PERFORMANCE](docs/PERFORMANCE.md) | Evidence, calculated budgets and measurement procedures |
| [CLIENT](docs/CLIENT.md) | Windows controls and source builds |
| [AUTOMATION](docs/AUTOMATION.md) | API operations, Python and MCP |
| [IMPLEMENTATION_PLAN](docs/IMPLEMENTATION_PLAN.md) | Milestone ledger and remaining work |

Portable tests use CMake/CTest and Python unittest. iOS compilation uses macOS,
Xcode's iPhoneOS SDK and pinned Theos. The SDK is not redistributed. Windows
uses Visual Studio and pinned vcpkg dependencies. Workflow logs report actual
SDK/library versions and build outcomes. No fabricated release is published.

Original code is [MIT](LICENSE). Retained ScreenMirror notices and dependencies
are documented in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md). This software
uses [FFmpeg](https://ffmpeg.org/) under LGPL-2.1-or-later; corresponding source
is in the source artifact beside each successful Windows binary artifact.
Reference repositories are not modified.
