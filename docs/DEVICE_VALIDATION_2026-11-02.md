# Native pointer correction — 2026-11-02

Target: iPhone SE 2 / iPhone12,8, iOS 16.1.1, Dopamine rootless. The owner reported
position offsets, jumping and abnormal movement with the first native mouse mode.
The phone LAN address had changed; SSH verified the existing phone host key and
the pairing certificate/token were retained.

Splitting absolute positioning and relative buttons into distinct devices and
disabling edge calibration registered successfully, but different absolute target
coordinates did not reliably move the system pointer. That trial was superseded.
The final backend registers only a native keyboard and relative mouse. Legacy
mouse_event requests receive an update-client error instead of submitting absolute
reports. Touch mode remains available outside native mode.

Phone code `f6f3134da82c62fbc2887c51d5a9821a20e0eb46` passed
[iOS build 41352368532](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41352368532)
and the production Foundation validator. Live mode returned pointer_mode=relative
and nonzero registry IDs for both devices. Pinned connections and mode activation
completed without local approval prompts after the owner unlocked the phone.

While the owner kept the screen untouched, relative reports moved the native
pointer in the requested directions. A native left-button down/up over the
Calculator 6 key changed the displayed result from 0 to 6. Six mouse reports were
submitted during this short check. Disabling the mode returned both device IDs
as zero. Local screenshots remain private and are not in git. The experimental
launch_app API reported a refusal even though Calculator later appeared; that
API return/delivery behavior remains unverified.

Windows code `f43467c35398fd60e6cf7fa25a1454f84a6ff5f4` passed
[build 41352456736](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41352456736):
nine CTest cases, GUI checks, TLS/H.264 integration, certificate-pin protection
and color decoding. Tests cover HID report sizes and signed values, movement
coalescing, rotation transforms, bounds and discard on release. The Python suite
passed 16 tests in CI.

The installed viewer captures foreground Raw Input in F10 mode, hides the Windows
cursor and sends relative counts. F10, focus loss, resize, disconnect and closing
release capture. A click in the video recaptures after returning; that first click
is consumed locally. The final executable and ZIP were rebuilt and installed.
The owner confirmed that movement and control worked, but reported that the native
pointer still felt delayed. The first Windows version used only a low-priority
16-ms WM_TIMER to flush movement, while the TLS worker could wait another 10 ms.
The follow-up sends from raw events at up to 125 Hz, wakes the worker directly and
accumulates unsent motion without merging button edges. The title reports client
queue time. A cross-thread loopback wake test and queue-order regression test were
added. Final Windows source `f5ca7b845ace110f595d52bd04076a22c653c8f0` passed
[build 41354010336](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41354010336)
with all ten CTest cases, including input-delivery wake/queue checks, plus the
existing GUI, TLS/H.264 and color checks. The installed EXE matches that artifact;
the ZIP and checksums were regenerated with current instructions.

After testing continuous physical mouse movement on the updated viewer, the owner
reported **"明显更跟手了"** (noticeably more responsive). This confirms a subjective
improvement; no glass-to-glass latency number or long-duration stability result
has been established.
