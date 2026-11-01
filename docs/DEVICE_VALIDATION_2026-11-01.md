# Device validation — 2026-11-01

This report records a supervised test of the owner's iPhone SE 2 (iPhone12,8),
iOS 16.1.1 (20B101), Dopamine rootless and ElleKit 1.2. Pairing state, device
addresses, credentials and screen images are excluded from this repository.

## Capture and Windows playback

Tested package source: `e282f277bdb4945d6eef12c356c4a8d8feee11f0`.
[Successful iOS build](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41271853144).
Windows receiver source: `e052aa63809454280799ea271e485b1deca495f2`.
[Successful Windows build](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41269770032).

- The arm64 daemon loaded a provisioned PKCS12 identity and served pinned TLS
  over the phone's Wi-Fi. An incorrect certificate pin prevented authentication;
  an incorrect token with the correct pin was rejected by the physical daemon.
- The SpringBoard library loaded with the arm64e slice. Attaching its approval
  window to an existing UIWindowScene fixed the initially invisible prompt.
  The owner physically allowed a viewing session. A red STOP control appeared.
- The initial CARenderServer call produced blank frames while encoding and
  transport continued. Using the actual display name and the void function ABI
  corrected capture. Sixty Annex-B H.264 frames and a JPEG snapshot were received
  through the Python API. Independent PyAV decoding produced a recognizable
  Home Screen at **750 × 1334**. RGB channel extrema were 0..255, and the decoded
  image was visually inspected. This proves useful pixels for that scene, not
  protected-content handling or every app/orientation.
- The owner subsequently confirmed the **live screen was visible in the Windows
  app**. Its title reported D3D11VA decoding. One observed title sample was
  19.9 decoded FPS, 7.0 ms network RTT and 6.5 ms last-frame decode time. An
  earlier 29.7-FPS sample preceded the pixel fix and is not evidence of useful
  video throughput. These title samples are not a sustained benchmark.
- The owner reported that Home, App switcher and mouse input did **not** work
  in this version. Remote input must not be advertised as validated on the
  basis of successfully submitting an IOHID event.

One short post-fix Python session requested native resolution, 30 FPS and
8 Mbit/s. Its last telemetry sample reported 387 encoded frames in approximately
13.7 seconds (28.26 FPS mean), 4.463 ms last capture, 10.606 ms last encode,
28.798 ms capture-to-client-ack EWMA, 84,066,304 SpringBoard resident bytes,
nominal thermal state and Low Power Mode off. Python consumed encoded frames;
it did not decode or present them live. The independent decode was performed
after recording. This sample is not a 30/60-FPS comparison or a power/soak test.

VideoToolbox accepted low-latency mode. Hardware-encoder reporting was unknown
on this iOS version; hardware encoding has **not** been conclusively verified.
RTT and capture-to-ack exclude monitor scanout and are **not glass-to-glass
latency**. No sub-80-ms display-latency claim is made.

## Follow-up corrections awaiting device retest

- `fa43d37`: ignore the bridge STOP acknowledgement after the daemon has already
  disconnected, preserving the two-second normal reconnect delay. Local STOP
  or refusal during an active connection retains its 30-second cooldown.
- `fa43d37`: use unconditional launchd KeepAlive and explicitly disable pressured
  exit. A later inspection found the daemon absent after an idle-exit jetsam;
  the prior SuccessfulExit condition did not keep the listener available.
  Restarting the daemon restored the connection. The new policy still needs
  idle/recovery observation; it does not exempt the service from memory pressure.
- `0049e94`: separate HID observation from dispatch, restore aggregate hand fields
  after adding fingers, include contact identity transitions, avoid assigning
  the touch-panel sender to keyboard events, and dynamically use SpringBoard's
  Home/App Switcher handlers. Input diagnostics record readiness and submission
  counts without coordinates, typed text or key values. Delivery remains pending.

## Remaining validation

Retest input and reconnect after installing the follow-up package. Check
cross-app capture, all rotations and edges, multi-touch, long press, text,
volume, app launch, STOP during input, lock, Wi-Fi loss, respring/reboot,
60-FPS mode, half-resolution mode, protected content, memory growth and battery
use. A standard MCP host has been tested against a TLS simulator; a physical
MCP-host-to-phone session remains untested. Use DEVICE_TESTING.md for the exact
procedures. A successful package build does not close these runtime gates.
