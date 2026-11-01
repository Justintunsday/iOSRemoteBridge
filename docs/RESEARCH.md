# Technical research — 2026-11-01

Findings below distinguish inspected source, upstream claims and target-device
evidence. The reference repositories were read only; none was modified. Their
default-branch revisions were pinned when inspected:

| Reference | Commit | License |
| --- | --- | --- |
| ScreenMirror | d8b801e9805e9946a96af57c8a99d65831d76854 | MIT |
| ZXTouch Rootless | a602da112ef1a688fb2601424443abb1ccc28c63 | GPL-3.0 |
| Appium WebDriverAgent | 3d6a4e03cdd673e540289c610f7e189f8ffea434 | BSD-3-Clause |

## ScreenMirror: useful foundation, not a demonstrated 60-FPS pipeline

Its [controller](https://github.com/e1abrador/ScreenMirror/blob/d8b801e9805e9946a96af57c8a99d65831d76854/ios/src/Tweak.x)
loads in SpringBoard and pushes encoded video to a Mac listener. The default
pipeline requests 0.4 native scale, 15 FPS and 800 kbps; the high preset is 0.65,
24 FPS and 2.5 Mbps. These source settings do not establish full-resolution
60-FPS capability. Its README lists iPhone 8/X iOS-16 palera1n observations, not
the exact iPhone12,8 / 16.1.1 / Dopamine combination. The new server should accept
clients on the phone rather than require a permanently listening Mac.

The [capture implementation](https://github.com/e1abrador/ScreenMirror/blob/d8b801e9805e9946a96af57c8a99d65831d76854/ios/src/ScreenCapture.m)
resolves CARenderServerRenderDisplay dynamically and renders to an IOSurface,
wrapped as a CVPixelBuffer. It reuses one surface while the downstream encoder
can operate asynchronously. Its screenshot fallback adds image drawing and
allocation; blue-channel sampling can miss a change, and black pixels are not
proof of capture failure. iOSRemoteBridge uses a bounded buffer pool, holds no
mutable shared capture frame across encoder work, and does not fall back on
black or protected output. Capture still needs exact-device visual verification.

The [encoder](https://github.com/e1abrador/ScreenMirror/blob/d8b801e9805e9946a96af57c8a99d65831d76854/ios/src/VideoEncoder.m)
already uses VideoToolbox, disables reordering and converts AVCC NAL units to
Annex B. However, expected FPS is hardcoded to 15, sample duration to 1/30, and
the cast of CMTimeGetSeconds precedes multiplication by one million, losing
fractional seconds. Hardware usage is not checked. These are concrete reasons
to reuse its API approach while revising timestamps, frame lifetime, bitrate,
keyframes and backpressure.

The [network code](https://github.com/e1abrador/ScreenMirror/blob/d8b801e9805e9946a96af57c8a99d65831d76854/ios/src/NetworkClient.m)
uses TCP streams with a custom password/X25519/AES-GCM protocol. Replacing that
interoperability burden with platform TLS and mandatory certificate pinning is
the first prototype's security choice; this is not a cryptographic audit of
ScreenMirror. The [HID code](https://github.com/e1abrador/ScreenMirror/blob/d8b801e9805e9946a96af57c8a99d65831d76854/ios/src/TouchInjector.m)
provides useful digitizer/keyboard event patterns, but hardcodes a sender ID and
implements only a small ASCII subset. We resolve symbols and observe the real
touchscreen sender, retain all active contacts in a report and reject unsupported
text before side effects. The MIT notice is preserved.

## ZXTouch Rootless

The fork's [Touch.xm](https://github.com/Epic0001/zxtouchrootless/blob/a602da112ef1a688fb2601424443abb1ccc28c63/pccontrol/Touch.xm)
illustrates multi-contact digitizer state and why sender identity should be
learned from a real event rather than assumed. Its SpringBoard-centered design
avoids injecting touch code into every app. The
[socket service](https://github.com/Epic0001/zxtouchrootless/blob/a602da112ef1a688fb2601424443abb1ccc28c63/pccontrol/SocketServer.xm)
and broader dashboard have different interfaces and access rules, so their
security cannot be inferred from the presence of a dashboard token. We do not
expose or depend on the legacy touch socket.

The [README](https://github.com/Epic0001/zxtouchrootless/blob/a602da112ef1a688fb2601424443abb1ccc28c63/README.md)
reports Dopamine tests on 16.4.1/16.6.1 and other versions. Those upstream claims
support investigation, not a compatibility assertion for 16.1.1. Its
[build file](https://github.com/Epic0001/zxtouchrootless/blob/a602da112ef1a688fb2601424443abb1ccc28c63/pccontrol/Makefile)
also includes many unrelated image/OCR/UI modules. The GPL-3.0 code and binaries
are not incorporated into this MIT implementation. Any optional future ZXTouch
adapter must have a clearly separate distribution/licensing decision.

## WebDriverAgent

[Screenshot handling](https://github.com/appium/WebDriverAgent/blob/3d6a4e03cdd673e540289c610f7e189f8ffea434/WebDriverAgentLib/Utilities/FBScreenshot.m)
uses XCTest daemon screenshot requests. Its
[MJPEG service](https://github.com/appium/WebDriverAgent/blob/3d6a4e03cdd673e540289c610f7e189f8ffea434/WebDriverAgentLib/Utilities/FBMjpegServer.m)
repeatedly requests and JPEG-encodes screenshots with scaling and bounded sends;
a 60-FPS setting is not a hardware video-capture guarantee. Its
[W3C action synthesizer](https://github.com/appium/WebDriverAgent/blob/3d6a4e03cdd673e540289c610f7e189f8ffea434/WebDriverAgentLib/Utilities/FBW3CActionsSynthesizer.m)
is useful for understanding gesture semantics. XCTest runner provisioning,
test-daemon sessions and signing make it less suitable as the core independent
low-latency server. BSD-3-Clause permits reuse with notices, but none is bundled.
An optional accessibility-tree adapter remains future work; no UI hierarchy is
currently fabricated from pixels.

## Capture candidates for iOS 16.1.1

| Path | Verified basis | Decision / uncertainty |
| --- | --- | --- |
| RPScreenRecorder in-app capture | [Apple API](https://developer.apple.com/documentation/replaykit/rpscreenrecorder) captures the app | Not full-device daemon capture |
| ReplayKit Broadcast Upload Extension | [RPBroadcastSampleHandler](https://developer.apple.com/documentation/replaykit/rpbroadcastsamplehandler) delivers broadcast samples | Promising public backend, but needs an owner-started broadcast, app/extension packaging and signing; not a drop-in daemon call |
| CARenderServer | ScreenMirror source resolves and invokes the display-render symbol | Selected experimental polled producer in SpringBoard; actual A13 privileges, pixels, pacing and rotation need verification |
| SpringBoard screenshot helpers | ScreenMirror's _UICreateScreenUIImage path | Screenshot/image path, not a proven continuous low-copy feed; intentionally not a fallback |
| BackBoardServices / IOHID | System input/event mechanisms in inspected HID implementations | Not evidence of a public video framebuffer stream |
| IOSurface | [Apple storage API](https://developer.apple.com/documentation/iosurface) and CVPixelBuffer interop | Buffer-sharing/storage primitive; allocating or looking up a surface does not grant framebuffer access |
| CoreAnimation view rendering | Per-process layer/view rendering; separate CARenderServer global path | App layer rendering alone does not capture other apps or Home Screen |
| IOMobileFramebuffer | Private headers/references occur in ScreenMirror/ZXTouch | User-client access and useful compositor contents on this A13 are unverified; no kernel changes or implementation added |
| ScreenCaptureKit | [Apple SCStream availability metadata](https://developer.apple.com/tutorials/data/documentation/screencapturekit/scstream.json) | Not available on iOS 16.1.1; current platform lists must not be mistaken for historical deployment availability |

Apple's documentation metadata fetched on 2026-11-01 lists SCStream's iOS
introduction later than 16.1.1 (27.0), and its macOS introduction as 12.3. The
low-latency VideoToolbox specification is available from iOS 14.5, while
RequireHardwareAcceleratedVideoEncoder is introduced on iOS at 17.4. This is why
the latter is not statically linked into the iOS-16 daemon/bridge. The exact
availability snapshots and source URLs are retained in api-availability.json.

Capture is driven by CADisplayLink, with a bounded IOSurface-backed pool. This
is continuous polling of a render API, **not** a verified compositor frame
subscription. No private API ABI or 60-FPS result is inferred solely from
successful linking. Entitlements on a dylib do not change host-process
privileges; SpringBoard integration is deliberate. The runtime
SBLockScreenManager selector is checked before use and missing lock detection
denies/ends sessions. [Another independently inspectable use](https://github.com/mringwal/hid-support/blob/master/hidspringboard/Tweak.xm)
corroborates the selector's existence, not target-version compatibility.

## Encoding and transport decisions

[Apple's low-latency VideoToolbox session](https://developer.apple.com/videos/play/wwdc2021/10158/)
describes the H.264 low-latency specification and real-time controls. The
prototype requests that mode and explicitly records a fallback. Hardware
availability is queried where exposed, not inferred from the A13 name. H.264
is implemented; HEVC, long-term-reference feedback and audio are deferred until
the core H.264 path is physically validated. A failed session is stopped and
recreated on reconnect, with IDR requests after receiver recovery.

| Transport | Benefit | Cost / first-prototype decision |
| --- | --- | --- |
| WebRTC | SRTP, congestion control, loss feedback, browser interoperability | Large native dependency, ICE/signaling and platform build work; best production candidate to benchmark later |
| RTP/UDP | Small framing and minimal head-of-line blocking | Must add authenticated encryption, packetization, reassembly, feedback and loss/keyframe recovery; not justified before capture validation |
| QUIC | Encrypted multiplexing; datagrams can avoid video retransmission | Native iOS-16/Windows library integration and media feedback still needed |
| ScreenMirror TCP | Working source-level reference | Mac-server direction and bespoke cryptographic protocol not retained |
| TLS/TCP + frame credit | Platform TLS, simple cross-platform prototype, bounded memory and deterministic decode | Selected now; loss retransmission and RTT-limited throughput are acknowledged limitations |

TLS identity loading follows [Apple's local-network TLS guidance](https://developer.apple.com/documentation/network/creating-an-identity-for-local-network-tls).
The protocol uses frame boundaries and complete Annex-B access units. No packet
loss measurement or sub-80-ms claim has been made. Adaptive bitrate is based on
measured per-frame acknowledgment time, with deliberately untuned thresholds.

## Background service and clients

[Theos rootless packaging](https://theos.dev/docs/rootless) supplies the /var/jb
prefix and iphoneos-arm64 package architecture. The A13 SpringBoard library also
needs arm64e; a macOS Xcode linker avoids the newer ABI limitation documented by
Theos. Linux can run portable tests but is not claimed to build this exact
SpringBoard package. Xcode's iPhoneOS SDK is used on the hosted Mac and is never
redistributed. ldid signs jailbreak binaries; no Apple developer certificate is
required for the .deb path, subject to actual Dopamine runtime validation.

launchd runs the mobile daemon with throttled crash recovery. SpringBoard
reconnects to its local socket after daemon recovery; network reconnect always
requires new local approval. No idle capture runs. Auto-lock is left enabled;
locking ends a session, and no claim of locked/asleep remote operation is made.
[Dopamine](https://github.com/opa334/Dopamine) is semi-untethered, so normal reboot
requires owner reactivation before these jailbreak components run again.

For Windows, Win32 avoids a managed UI runtime. FFmpeg offers inspected
[D3D11VA support](https://github.com/FFmpeg/FFmpeg/blob/master/libavutil/hwcontext_d3d11va.c)
and software fallback. [Media Foundation's H.264 MFT](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder)
was considered: it avoids codec distribution, but a robust hardware-transform,
D3D-device and stream-change implementation adds more initial work. FFmpeg's
dynamic LGPL build and corresponding-source artifact preserve replaceability;
see THIRD_PARTY_NOTICES.md. GDI presentation still copies pixels to the CPU.

The recommended next production step is to validate this supervised prototype
on the exact device before adding WebRTC, a D3D renderer, automatic app
orientation, broad Unicode input, audio or accessibility metadata. Evidence
from source inspection supports the implementation strategy; only the physical
tests in DEVICE_TESTING.md can establish its practical compatibility.
