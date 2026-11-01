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

## Current status

| Phase | Source/build progress | Physical evidence |
| --- | --- | --- |
| Research/repository | Private repo, pinned analysis, notices, feature branches and CI | Exact target not inspected |
| Capture | IOSurface pool, CARenderServer producer and GPU scaling; .deb compiles | Pixels, cross-app capture and rotation unverified |
| Video | VideoToolbox H.264, IDRs, bounded queues, adaptation and recovery | Hardware use, FPS, latency, power and heat unmeasured |
| Input | HID contact state, gestures, paced ASCII, keys/buttons and launch | Delivery remains experimental |
| Windows | Native receiver, decoding, input, metrics and CI harness | Check actual native CI; phone pairing not tested |
| Automation | Python, JSON-lines and MCP; simulator/security tests pass | No physical agent-to-iPhone session |
| Reliability | Consent/lock checks, cancellation, thermal pacing and launchd recovery | Soak/reboot/respring tests pending |

No release/tag is published by this ledger. The manual release workflow creates
only a draft from successful real artifacts for the same main commit. Publish
a milestone only when its functionality and evidence have been reviewed.
