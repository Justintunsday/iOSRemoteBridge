# Performance evidence and measurement

**Short physical samples are available in the [2026-11-01 device report](DEVICE_VALIDATION_2026-11-01.md).**
One native 30-FPS Python session averaged 28.26 encoded FPS over approximately
13.7 seconds. A live Windows title sample reported 19.9 decoded FPS with D3D11VA.
These are separate paths and short observations, not sustained benchmarks.
Later matched-path Python samples on the input-fix package reached 23.73 encoded
FPS at 30 requested and 25.04 at 60 requested, over 7.376/8.186 seconds respectively.
Those measurements used the original one-frame pipeline. The native 60-FPS
objective was not achieved in that version; see the report's method and ranges.
The current pipeline negotiates up to three frame slots and overlaps two encoder
submissions. New target-device measurements are required before claiming a gain.
CI separately validates compilation, protocol behavior and synthetic H.264 decode.
Hardware encoder use, glass-to-glass latency, input latency, sustained thermal
stability and battery consumption remain unmeasured.

Targets: native 750x1334 portrait / 1334x750 landscape, 30 FPS power-saving mode,
60 FPS interactive mode, H.264 hardware encoding and ideally <80 ms local
glass-to-glass latency. These remain objectives. Native resolution
means the pixel dimensions must be inspected on the device, not inferred from
an encoder configuration or a successful build.

Static arithmetic, **not benchmark results**:

| Item | Calculated amount |
| --- | --- |
| One 750x1334 BGRA image, excluding row alignment | 4,002,000 bytes |
| Four such images, excluding framework overhead | 16,008,000 bytes |
| Raw image payload at 60 FPS, before compression | 240,120,000 bytes/s |
| Frame interval at 60 / 30 FPS | 16.67 / 33.33 ms |
| Default compressed target bitrate | 8,000,000 bits/s |

The capture pool is limited to four allocations. Native capture directly
passes an IOSurface-backed pixel buffer to VideoToolbox, which may perform its
own conversion. It is a low-copy API path, not a measured zero-copy end-to-end
pipeline. Lower-resolution mode adds a Core Image GPU scaling request. The
Windows renderer explicitly copies GPU-decoded frames back to CPU BGRA and
uses GDI; optimizing this to D3D presentation is outstanding.

Bounded frame credit prevents runaway latency under backpressure. The current
maximum is three frames, with one/two-frame modes available through the Python
API for controlled comparisons. A single slot limits throughput to an entire
capture/encode/transfer/decode/ack cycle. The Windows client now flushes queued
credits and commands before waiting on socket input. TCP retransmission stalls and
the conservative bitrate controller must be measured against RTP/WebRTC before
selecting a production transport. Merely raising a timer to 60 does not prove
60 independent captured and displayed frames per second.

Run a supervised device session using:

```powershell
python -m tools.benchmark --config credentials/pairing/client-config.json --fps 30 --seconds 120 --output out/metrics-30.jsonl
python -m tools.benchmark --config credentials/pairing/client-config.json --fps 60 --seconds 120 --output out/metrics-60.jsonl
```

Each run requires local approval. Repeat at the same brightness, Wi-Fi band,
distance, room temperature and content. Warm up first, then test static UI,
scrolling, app switching and an animation with frame-identifying numbers. Sample
capture/encode times, encoded FPS, skip counts, thermal state and whole-process
resident memory/CPU counters. These are last-frame/periodic samples, not a
complete per-frame trace. The daemon's capture-to-client-ack EWMA includes capture,
encoding, local IPC, transfer and client consumption/decoding, but excludes
monitor scanout and physical-device display timing. Python consumes encoded
frames without decoding; benchmark that path separately from Windows playback.

For glass-to-glass latency film the iPhone and client monitor together at 240 FPS
or faster. Use a visible time-coded animation, collect at least 100 matched
transitions, and report median/p95/p99 with the camera's temporal quantization.
For input latency include the physical mouse/button action in the camera view
and count until the iPhone responds. Network RTT and cross-host timestamp
subtraction are not substitutes for that measurement.

For power and reliability, compare an idle daemon with no session against
30/60-FPS sessions for at least 30 minutes each. Record battery percentage,
temperature/thermal state and screen brightness. Keep other phone use stable.
Run an overnight reconnect test separately. Stop on thermal pressure, repeated
capture errors, memory growth or SpringBoard instability. No synthetic result
should be entered as a device measurement.
