# Implementation ledger

Target: iPhone12,8 / iOS 16.1.1 / Dopamine rootless / ElleKit. No physical
device is attached to this development environment. Compile evidence and
runtime evidence are tracked separately. No release is implied by a version
string or a successful CI build.

1. `feature/build-foundation`: private repository, reproducible CI, shared wire
   format and adversarial parser/coordinate tests.
2. `feature/ios-pipeline`: SpringBoard capture/HID bridge, bounded VideoToolbox
   pipeline, rootless launchd TLS daemon, local session consent and stop control.
3. `feature/windows-client`: native Win32 receiver with certificate pinning,
   FFmpeg D3D11VA selection and software fallback, controls and statistics.
4. `feature/automation`: Python authenticated client, screen snapshot and input
   operations, integration tests with a TLS device simulator.
5. Build, inspect logs, correct failures, merge the implemented work into main.
6. Physical validation and optimization: follow DEVICE_TESTING.md. Runtime
   milestones remain pending until those observations are recorded.

Performance priorities: stability, system-wide compatibility, latency, native
resolution, 60 FPS, automation. Audio, WebRTC, HEVC and accessibility trees may
be deferred when the first implementation cannot validate them reliably.
