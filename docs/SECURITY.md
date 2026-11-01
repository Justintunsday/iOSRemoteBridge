# Security model and limits

This is an experimental, supervised remote-control prototype. Authorization
requires a pinned TLS connection with a random 256-bit bearer token and an
unlocked iPhone. At the device owner's request, authenticated sessions and all
operations execute without custom approval dialogs. Possession of the pairing
file authorizes viewing and requested control while unlocked.

- TLS is provided by Apple's Network/Security frameworks and OpenSSL or Python
  ssl. No custom cipher or password exchange is implemented. The clients verify
  the exact SHA-256 certificate pin before sending the token, even for a
  self-signed identity. Wrong-pin tests assert that no AUTH bytes are sent.
- Provision over an existing trusted SSH connection. Do not obtain a new pin
  from an untrusted network response. Pairing generates a different identity
  and token each time and never overwrites existing files. Rotate compromised
  pairing files on both endpoints, bootout the daemon and bootstrap it again.
- Device state is mobile-owned mode 0700; token JSON and PKCS12 identity are
  0600 regular files opened without following symlinks. The daemon refuses
  weaker permissions. Protect the client JSON with the current Windows user
  ACL; it is plaintext local credential storage. Credentials are excluded from
  git and no real device credentials are used in CI.
- The network process runs as mobile. The SpringBoard library inherits
  SpringBoard's process privileges. Signing a dylib with extra entitlements
  would not grant those entitlements to its host. The daemon requests platform-application and
  com.apple.hid.manager.user-access-device to create the native virtual HID
  mouse/keyboard, plus the IOKit exception for IOHIDResourceDeviceUserClient.
  PKCS12 import succeeded on the target; the exact necessity of
  platform-application has not been isolated. No protected-content,
  task-port or kernel-modification entitlements are added.
- Network clients cannot reach the private Unix socket directly. Its mode is
  0600 inside the 0700 directory, and the daemon checks the peer's UID. This is
  a boundary against network clients and sandboxed apps, **not** against root
  or another unsandboxed process already running as mobile on a compromised
  jailbreak. The socket does not cryptographically attest a SpringBoard PID.
- Framing rejects unknown types, flags, versions, sequence errors and oversized
  lengths before allocating a payload. Video caps at 8 MiB, control at 64 KiB,
  responses at 2 MiB. Both daemon and bridge validate operation schemas, numeric
  types/ranges and field names. Outgoing queues and command rate are bounded.
- Only one network session is accepted. Capture starts after pairing authentication
  and expires after 15 minutes. There is no persistent on-device session overlay.
  The owner can lock the iPhone or disconnect the client to stop it. Lock detection is
  dynamically checked and fails closed if the private selector is missing.
  Respring and daemon/bridge loss invalidate the session. The client discards
  queued input before reconnecting; it never retries an ambiguous gesture.
- Text, single keys, app launches and live system mouse/keyboard activation do
  not ask for additional confirmation. Read-only sessions still reject input.
  Native HID requests travel back through local IPC only after SpringBoard
  checks control and lock state; the daemon binds them to the TLS session
  generation. Network packets cannot invoke the local HID message type.
  Disconnect destroys the HID device and releases all buttons/keys. The viewer
  also releases held input on focus loss and uses focused-window messages rather
  than a global keyboard hook. F10 resets off on reconnect. AssistiveTouch settings
  changed by a normal session are restored when it ends; a process crash can
  leave an accessibility setting enabled until changed in Settings.
  Generic actions can interact with permission, payment or deletion screens;
  the app does not classify their meaning or provide a per-action approval gate.
  Read-only sessions remain available for inspection.
- Capture honors the system's output, including black/protected surfaces. There
  is no black-frame fallback, secure-layer override or DRM bypass.

The listener is Wi-Fi constrained. It has no UPnP, port forwarding or automatic
internet exposure. If remote internet use is needed, first validate the system
on-device and use a separately managed authenticated VPN. The current IPv4
listener worked over Wi-Fi in the dated device test. Do not treat the
prototype as security-audited or ready for a public endpoint.

Automated checks include fragmented TLS records, wrong certificate pins,
oversized/truncated frames, invalid coordinate ranges, hostile Foundation
argument types, reserved request IDs, wrong response sequences and simulated server rejection.
These protect specific invariants; they are not a penetration test of iOS or
the third-party codec libraries.
