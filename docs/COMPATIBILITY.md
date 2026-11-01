# Compatibility evidence

| Component | Intended target | Evidence required |
| --- | --- | --- |
| Daemon | arm64, iOS 16.1+ | macOS Xcode/Theos build and on-device TLS test |
| SpringBoard bridge | arm64 + arm64e, ElleKit | load, capture and HID tests on iOS 16.1.1 |
| Windows | Windows 11 x64 | Windows CI build, decoder and TLS tests |
| Automation | Python 3.11+ | protocol tests and TLS integration tests |
| macOS client | Python automation/CLI only initially | no native desktop client yet |

The A13 is arm64e-capable. A SpringBoard dylib needs the matching arm64e ABI;
an arm64-only daemon does not eliminate that requirement. Use a macOS runner
with Apple's linker for the SpringBoard component. Successful compilation
does not prove private symbol availability, privileges, screen contents,
coordinate correctness, input delivery or target-device performance.

ScreenCaptureKit is **not a capture backend for iOS 16.1.1**. Check deployment
availability, not just the platform list in current Apple documentation.

The recorded macos-15 builds selected Xcode 16.4 / iPhoneOS 18.5 SDK, deploying
at iOS 16.1. Logs inspect arm64/arm64e slices and rootless file layout. This
newer SDK does not establish target runtime availability. Public API availability
is checked against 16.1; private symbols/selectors are dynamically resolved and
need exact-device verification.

Python 3.11+ is the project target; the optional MCP SDK is pinned to 2.3.0.
The standard CLI needs no external library. Generic MCP discovery/routing and
real stdio startup are tested; host-to-physical-iPhone integration is not.
