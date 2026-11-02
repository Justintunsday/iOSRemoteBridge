# Lockscreen viewing and display wake — 2026-11-02

Target: iPhone SE 2 / iPhone12,8, iOS 16.1.1, Dopamine rootless.
Phone source `5ca88eb895da897a5f259630061c081b2dcae180` was built in
[build-ios 41443818628](https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/41443818628),
installed through the owner's SSH access, and loaded by a SpringBoard reload.
No pairing identity was changed. Real screenshots and device credentials remain
local and are excluded from the repository and CI artifacts.

The Python production TLS client connected with `lockscreen_view=true`, control
requested, 30 FPS and a one-frame credit window. The initial authorized state was
locked with input disabled. An explicit wake returned display-on while the phone
remained locked; a 750×1334 JPEG contained the system lock interface.

The owner unlocked locally, then explicitly locked and turned the display off.
The same client connection stayed active across these transitions. State reported
locked/display-off/input-disabled. Three one-second performance samples all
reported 829 encoded frames and zero frames in flight, confirming capture paused
while heartbeats and RPCs continued. A Home attempt was refused by the SDK.

An explicit `wake_screen` changed display-off to display-on without changing the
locked state or its input epoch. A new screenshot showed the system clock and
“Press home to unlock” lock screen. The capture pacing was 5 FPS while locked.
The owner unlocked locally again; the original connection reported input enabled
and a new input epoch, and a Home request returned submitted without reconnecting.
This verifies operation submission; no external latency measurement was made.

Separate live requests deliberately bypassed the SDK's input guard: the server
refused a mismatched-epoch Home request while unlocked and refused display wake
from a viewing-only session. The loopback TLS test also covers repeated lock state
events, cache clearing, stale-frame credits, wake, and input resumption with a new
epoch. Portable policy and Foundation schema tests cover locked input refusal.

This check covers the existing capture backend on this one jailbroken device.
It does not establish protected-content visibility, support on other iOS releases,
operation before jailbreak activation, or operation after the 15-minute lease.
The stock iOS controller's previously reported motion performance remains a
separate deferred task.
