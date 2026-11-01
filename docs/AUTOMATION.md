# Automation interface

The stdlib Python client uses the same pinned TLS connection as the GUI. It
holds one approved session, pumps/acknowledges video in a background reader and
matches JSON RPC results by request ID. It does not automatically retry an
input operation after a timeout or connection loss. Inspect device state before
issuing another action whose previous outcome is ambiguous.

```python
from automation import BridgeClient

with BridgeClient("credentials/pairing/client-config.json", control=True) as phone:
    print(phone.request("get_device_info"))
    phone.request("tap", x=0.5, y=0.5)
    phone.request("swipe", x=0.5, y=0.8, to_x=0.5, to_y=0.2, duration_ms=400)
```

Coordinates use the natural portrait plane, each in [0,1]. The GUI transforms
its rotated view back to this plane. Public request IDs are positive integers.
The authenticated daemon validates all fields, then the bridge validates again.

`BridgeClient(..., video_window=1/2/3)` selects the maximum number of frames in
flight for benchmarking; the default is 3. An older server ignores this optional
AUTH field and retains its one-frame behavior. Credits remain ordered per frame.

| Operation | Parameters / result |
| --- | --- |
| get_device_info | model, OS, dimensions, requested FPS, input/hardware status |
| get_screen_frame | JPEG MIME, base64 data, dimensions, natural orientation |
| get_orientation | current natural capture-plane metadata; automatic foreground-app orientation is not implemented |
| get_connection_status | current approved-session/device info |
| get_performance_stats | capture/encode timing, frames, skips, memory/CPU/thermal telemetry and frame-ack EWMA |
| tap / double_tap | x, y |
| long_press | x, y, duration_ms (40..5000) |
| swipe | x, y, to_x, to_y, duration_ms (40..5000) |
| touch | finger (0..9), phase (0 down / 1 move / 2 up), x, y |
| type_text | text, maximum 1024 US-layout ASCII characters; separate local approval |
| key | usage (USB keyboard page 0x07, 4..231); one press/release; separate local approval |
| press_home / app_switcher | no parameters; guarded SpringBoard navigation handlers |
| volume_up / volume_down | no parameters; device delivery needs verification |
| launch_app | bundle_id; dynamic workspace API; separate local approval |
| configure | bitrate (500000..20000000); optional fps (30/60) and scale (.25..1) together |
| request_keyframe | no parameters; recovery IDR |

The default Python connection requests viewing only; pass control=True or
`--control` to request input. The phone still requires physical approval. The
service intentionally permits one session at a time, so the GUI and a separate
automation process cannot control it concurrently. Use one persistent adapter
for a sequence of agent actions rather than reconnecting for each tool call.

CLI examples:

```powershell
python -m automation --config credentials/pairing/client-config.json get_device_info
python -m automation --config credentials/pairing/client-config.json snapshot --output out/screen.jpg
python -m automation --config credentials/pairing/client-config.json --control serve
```

`serve` reads one JSON object per line, such as
`{"op":"tap","x":0.5,"y":0.5}`, and writes one result per line. This is a
simple process adapter; it is not itself the MCP wire protocol. Do not put the
token on a command line or in an agent's prompt. The credential file path is
the only pairing argument.

## Standard MCP adapter

Install the optional pinned official Python SDK and launch the stdio server:

```powershell
python -m pip install -r automation/requirements-mcp.txt
python -m automation.mcp_server --config ABSOLUTE_PATH_TO_CLIENT_CONFIG_JSON
```

Use that command and argument array in an MCP-capable host such as Codex or
OpenCode; host-specific setup is separate. Pass `--control` only when input
tools should be enabled. MCP initializes and lists tools before any device
connection; the first screen/device tool call requests local owner approval.
Allow at least 60 seconds for that first call. The server has no local HTTP
listener and writes protocol traffic only to stdout. Tokens stay in the local
pairing file. Screen inspection returns an MCP image content block.

Read-only and potentially destructive tool annotations distinguish inspection
from input. These hints do not replace the on-device consent checks. The SDK
also validates coordinate and duration schemas before dispatch. CI verifies MCP
tool discovery, routing, image content, invalid-coordinate rejection and the
read-only gate with a simulated device adapter. No Codex/OpenCode-to-iPhone
physical integration has been claimed. SDK API usage follows the
[official v2 SDK](https://github.com/modelcontextprotocol/python-sdk/tree/v2.3.0).

All screen/input operations remain experimental on the target until physical
testing is recorded. Owner consent, the active indicator and STOP control apply
equally to human clients and AI agents. A tool's `submitted`/`completed` state
describes injection scheduling, not proof that a UI action had the desired effect.
