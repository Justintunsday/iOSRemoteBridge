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

## Follow-up corrections and retest

- `fa43d37`: ignore the bridge STOP acknowledgement after the daemon has already
  disconnected, preserving the two-second normal reconnect delay. Local STOP
  or refusal during an active connection retains its 30-second cooldown.
- `fa43d37`: use unconditional launchd KeepAlive and explicitly disable pressured
  exit. A later inspection found the daemon absent after an idle-exit jetsam;
  the prior SuccessfulExit condition did not keep the listener available.
  Restarting the daemon restored the connection. The new policy still needs
  idle/recovery observation; it does not exempt the service from memory pressure.
  After the update, the same daemon PID remained running through the next idle,
  lock and approved input sessions; launchctl reported one run and KeepAlive.
- `0049e94`: separate HID observation from dispatch, restore aggregate hand fields
  after adding fingers, include contact identity transitions, avoid assigning
  the touch-panel sender to keyboard events, and dynamically use SpringBoard's
  Home/App Switcher handlers. Input diagnostics record readiness and submission
  counts without coordinates, typed text or key values.

The `0049e942d05969f0bed5f5f6bb1aa1712c9d8bc1` package from
[build 41286100021](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41286100021)
was installed and SpringBoard reloaded. Following fresh physical approval:

- App Switcher opened and its captured screen showed app cards.
- Home returned from App Switcher to the Home Screen.
- A normalized tap on Clock opened Clock's timer screen; no timer was started.
- A 400-ms horizontal swipe moved from the first Home Screen page to the second.
- Captured Settings, Clock, Home Screen and App Switcher contents demonstrate
  capture across those system views. Other apps and protected surfaces remain
  outside the verified set.
- Reconfiguration to half scale produced a recognizable 374x666 JPEG (even
  encoder dimensions round down from 375x667). Native 750x1334 / 30-FPS mode
  was restored before returning the connection to Windows.
- Captured images confirmed the visible results, rather than treating the
  submission replies as delivery proof. Images remain private and untracked.
- An earlier retest ended when the phone locked; the daemon logged the lock
  and closed TLS, confirming this shutdown path in that session.

This retest exercises the Python control API and the same phone-side input
path used by Windows. The Windows mouse/buttons still need an interactive
retest of this package. Text, keyboard, multi-touch and volume are not implied
to work by these observations.

Two additional short native Python telemetry windows were collected on this
package, on the same Wi-Fi with Low Power Mode off and nominal thermal state:

| Requested FPS | Sample interval | Encoded FPS over interval | Last-capture sample range | Last-encode sample range | Capture-to-ack EWMA range |
| --- | --- | --- | --- | --- | --- |
| 30 | 7.376 s | 23.73 | 4.329–9.507 ms | 10.566–13.325 ms | 27.917–98.887 ms |
| 60 | 8.186 s | 25.04 | 7.009–8.663 ms | 10.375–12.451 ms | 29.155–45.296 ms |

Intervals use the difference in encoded-frame counters divided by the difference
in session elapsed times (recovered from each counter and its reported mean).
The zero-frame startup sample was excluded. These are periodic last-frame
samples, not per-frame percentile distributions. SpringBoard resident memory
ranged 169,426,944–174,030,848 bytes in the 30-FPS window and 169,132,032 bytes
in the 60-FPS window; it includes the entire SpringBoard process and its other
tweaks. Content and run durations were not controlled for a sustained benchmark.
**The native 60-FPS target was not achieved.** One-frame credit serializes
capture/encode/transfer/ack, and display-link ticks can be skipped while that
cycle is outstanding. More throughput work and longer measurements are needed.

## Remaining validation

Retest Windows input and reconnect with the follow-up package. Check
cross-app capture, all rotations and edges, multi-touch, long press, text,
volume, app launch, STOP during input, Wi-Fi loss, respring/reboot recovery,
sustained 30/60-FPS operation, protected content, memory growth and battery
use. A standard MCP host has been tested against a TLS simulator; a physical
MCP-host-to-phone session remains untested. Use DEVICE_TESTING.md for the exact
procedures. A successful package build does not close these runtime gates.
