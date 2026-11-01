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

This initial retest exercises the Python control API and the same phone-side
input path used by Windows. The later Windows test is recorded below. Text,
keyboard, multi-touch and volume are not implied to work by these observations.

The next Windows retest exposed a separate client failure: each action reply
closed the session and requested approval again. The observed window title was
`[json.exception.type_error.302] type must be boolean, but is number`; the daemon
logged `peer closed TLS stream` while SpringBoard and the daemon stayed running.
The bridge's `@(!error)` expression boxed a C integer, so JSON emitted `ok: 1`
instead of `ok: true`. The Windows callback requested a strict boolean and threw.
The correction serializes explicit Foundation booleans and lets Windows accept
the installed prototype's exact integer 0/1 form. A native TLS regression now
exercises repeated Home/touch RPCs, an operation rejection, another RPC and
decoded video after all actions on the same connection. The corrected Windows
client at `55209321f261536749a8dd067d64c38b4c58d5dc`, from
[build 41287183336](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41287183336),
was opened against the installed `0049e94` phone package. App Switcher showed
app cards, Home returned to the Home Screen, and a viewer mouse click opened
Clock. The viewer continued to display FPS/D3D11VA after these actions; the
phone's log showed no further disconnect during that test. A final drag test
was stopped by the owner's physical Escape key and remains unverified.

The owner subsequently reported sudden changes in brightness. The existing
GDI paint cleared the displayed video area before every frame. The Windows
client at `ceae5bfca3ff2bfbf59f79b766a3d306e16ee1da` composes each paint in a
reused offscreen bitmap and presents the completed image in one pass. It also
adds device-aspect resizing, Fit/F6, Rotate/F1 and compact controls. The
[main Windows build](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41288462720)
passed repeated-color/resize rendering, portrait/landscape edge-and-corner
sizing, GUI startup and the TLS/action regression checks.

After this viewer was opened and the owner was asked to watch the live image
for 20–30 seconds, the owner reported **"Brightness is stable now."** This is
a supervised visual confirmation that the reported brightness flicker was
resolved in that test, not an instrumented luminance or long-duration test.
The viewer remained active with D3D11VA; one title sample showed 21.9 FPS,
5.3 ms RTT and 7.7 ms last decode time. Those are instantaneous client samples,
not sustained throughput or glass-to-glass measurements.

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
**That version did not achieve the native 60-FPS target.** One-frame credit serializes
capture/encode/transfer/ack, and display-link ticks can be skipped while that
cycle is outstanding.

## Three-frame pipeline and corrected colors

The updated phone package at `120d772704e39b184fc6c998ada84fb156ea7b18`, from
[iOS build 41291465234](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41291465234),
was installed and SpringBoard reloaded. It negotiates three ordered frame slots
and permits two overlapping encoder submissions. The owner unlocked the phone
and approved each new test session. Native-resolution telemetry reported
750x1334, Low Power Mode off and nominal thermal state.

| Requested FPS | Positive-counter interval | Encoded FPS over interval | Last-capture sample range | Last-encode sample range | Capture-to-ack EWMA range |
| --- | --- | --- | --- | --- | --- |
| 60 | 17.282 s | 59.95 | 3.976–6.583 ms | 10.062–10.715 ms | 20.326–30.791 ms |
| 30 | 8.128 s | 30.02 | 6.969–7.749 ms | 10.893–12.094 ms | 24.879–26.220 ms |

The Python client consumes encoded frames without decoding/displaying them. The
30-FPS configuration followed the 60-FPS sample in the same approved session;
the pipeline drained and restarted successfully. SpringBoard resident memory
ranged 122,798,080–123,076,608 bytes at 60 FPS and 124,403,712–124,452,864 bytes
at 30 FPS. These are whole-process samples, not isolated bridge allocations.
They are short telemetry windows, not per-frame traces or a thermal soak.

The Windows client at `ae2477818b6230ebffd5d3e5ede318c3a1395d43`, from
[Windows build 41291645892](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41291645892),
preserves color properties on GPU download and explicitly applies full/limited
range and the source matrix with accurate conversion. The phone stream reports
full-range YUV; treating downloaded NV12 as limited range had clipped highlights.
Encoded full/limited grayscale fixtures passed on this PC with both software
and D3D11VA decoding (maximum channel errors 2 and 3 levels respectively).
For a private native-resolution phone IDR, software and D3D11VA RGB output matched
exactly (mean and maximum error zero). Private screen pixels remain local.

The new live Windows viewer was opened, approved on the phone, and sampled
once per second. After omitting two startup readings, 14 title readings averaged
**59.86 decoded FPS** (range 58.0–60.6) and 7.86 ms decode/conversion/handoff time.
The decoder reported D3D11VA. Title counters are sampled estimates; a reading
above 60 does not establish more than 60 physically displayed frames per second.
These samples precede the optional post-scale sharpening filter.

After watching the live viewer, the owner reported **"Colors look correct and
motion is smoother."** This confirms the reported visual improvement in that
session. Physical monitor scanout, glass-to-glass latency, hardware encoder use,
long-duration 60-FPS stability and battery use remain unverified. No new capture
or acknowledgement timeout was observed during these short tests.

## Adjustable Windows sharpening

The Windows client at `74d14959cd596a98b780b90989e1e55ce57b4ec9`, from
[build 41293093548](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41293093548),
adds a local 0–100% post-scale sharpening slider, initially 40%, and F7/F8
shortcuts. Five CTest checks, GUI startup/aspect checks, TLS/action regression
and encoded range fixtures passed. The same fixtures passed locally using
D3D11VA; the private phone IDR still matched software output exactly.

The new viewer was opened and live native-resolution playback observed. The
owner adjusted the slider (a later accessibility reading showed 10%) and
reported **"all fine"** before requesting a Windows executable package.
Two additional 16-reading title samples averaged 46.09 and 58.03 decoded FPS
(ranges 27.0–60.5 and 54.9–60.1). The first window included active touch input
and RTT readings up to 163.7 ms; the second reached at most 20.2 ms RTT.
Content and slider strength were not controlled or recorded per reading, so
these do not isolate sharpening cost or prove a stable rate during interaction.
They are retained alongside the earlier pre-sharpening sample rather than
being presented as sustained 60-FPS evidence.

## Floating control button removed

At the owner's request, `3ae6cf2ab1872055435c2a54bba90700b8e30dcf` removes the
red REMOTE CONTROL / SCREEN SHARING floating button. The approval window is
released after a decision. Connection approval, sensitive-action prompts,
lock shutdown and the 15-minute session deadline remain in place. The prompt
now explains that locking the iPhone or disconnecting the client ends a session.

The package from [iOS build 41302176006](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41302176006)
was installed over SSH and SpringBoard reloaded once. The same commit passed
[main iOS build 41302299854](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41302299854)
and [portable tests 41302299857](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41302299857).
The existing Windows executable reconnected after the owner unlocked and
approved on the phone; no Windows binary change was needed. The owner confirmed
**"已消失，画面正常"** (the button is gone and the picture is normal).
Earlier STOP-button observations above describe previous versions.

## Remaining validation

Check longer Windows input/reconnect sessions, broader cross-app capture,
all rotations and edges, multi-touch, long press, text,
volume, app launch, disconnect/lock during input, Wi-Fi loss, respring/reboot recovery,
sustained 30/60-FPS operation, protected content, memory growth and battery
use. A standard MCP host has been tested against a TLS simulator; a physical
MCP-host-to-phone session remains untested. Use DEVICE_TESTING.md for the exact
procedures. A successful package build does not close these runtime gates.
