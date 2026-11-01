"""Optional standard MCP stdio adapter. No network listener is opened locally."""
import argparse
import asyncio
import base64
import threading
from typing import Annotated
from mcp.server import MCPServer
from mcp.server.mcpserver import Image
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations
from pydantic import Field
from .client import BridgeClient, BridgeError

Coordinate = Annotated[float, Field(ge=0, le=1, allow_inf_nan=False)]
Duration = Annotated[int, Field(ge=40, le=5000)]
READ = ToolAnnotations(readOnlyHint=True, destructiveHint=False, idempotentHint=True, openWorldHint=False)
INPUT = ToolAnnotations(readOnlyHint=False, destructiveHint=True, idempotentHint=False, openWorldHint=True)
READ_OPS = {"get_device_info", "get_screen_frame", "get_orientation", "get_connection_status", "get_performance_stats"}

class DeviceAdapter:
    def __init__(self, config, control=False, factory=BridgeClient):
        self.config, self.control, self.factory = config, control, factory
        self.client = None
        self.lock = threading.RLock()

    def request(self, op, **parameters):
        if op not in READ_OPS and not self.control:
            raise BridgeError("input disabled; start this adapter with --control on the unlocked paired iPhone")
        with self.lock:
            if self.client is None:
                self.client = self.factory(self.config, control=self.control, timeout=60)
                try: self.client.connect()
                except BaseException:
                    self.close()
                    raise
            try: return self.client.request(op, **parameters)
            except BridgeError:
                if getattr(self.client, "error", None): self.close()
                raise  # Never replay a failed/ambiguous action.

    def close(self):
        client, self.client = self.client, None
        if client: client.close()

def create_server(adapter):
    server = MCPServer("iOSRemoteBridge")

    async def request(op, **parameters):
        try: return await asyncio.to_thread(adapter.request, op, **parameters)
        except (BridgeError, OSError, ValueError) as exc: raise ToolError(str(exc)) from exc

    @server.tool(annotations=READ)
    async def get_device_info() -> dict:
        """Inspect device model, iOS, capture dimensions and runtime capability status. First call connects to the unlocked paired phone."""
        return await request("get_device_info")

    @server.tool(annotations=READ)
    async def get_screen_frame() -> Image:
        """Inspect a current JPEG screen frame during an paired session. Protected output is not bypassed."""
        result = await request("get_screen_frame")
        if result["mime"] != "image/jpeg": raise BridgeError("unexpected screen image type")
        return Image(data=base64.b64decode(result["base64"], validate=True), format="jpeg")

    @server.tool(annotations=READ)
    async def get_orientation() -> dict:
        """Read capture-plane orientation metadata; coordinates are natural portrait normalized values."""
        return await request("get_orientation")

    @server.tool(annotations=READ)
    async def get_connection_status() -> dict:
        """Inspect this adapter's local session status without connecting."""
        client = adapter.client
        return {"connected": client is not None and not getattr(client, "error", None),
                "device": client.device if client else None, "input_requested": adapter.control}

    @server.tool(annotations=READ)
    async def get_performance_stats() -> dict:
        """Read periodic capture/encode/ack/thermal telemetry. This does not measure glass-to-glass latency."""
        return await request("get_performance_stats")

    @server.tool(annotations=INPUT)
    async def tap(x: Coordinate, y: Coordinate) -> dict:
        """Tap a normalized natural-portrait point. Requires a paired control session."""
        return await request("tap", x=x, y=y)

    @server.tool(annotations=INPUT)
    async def double_tap(x: Coordinate, y: Coordinate) -> dict:
        """Double tap a point in the paired control session."""
        return await request("double_tap", x=x, y=y)

    @server.tool(annotations=INPUT)
    async def long_press(x: Coordinate, y: Coordinate, duration_ms: Duration = 600) -> dict:
        """Hold one point, then release. Completion reports scheduling, not semantic UI success."""
        return await request("long_press", x=x, y=y, duration_ms=duration_ms)

    @server.tool(annotations=INPUT)
    async def swipe(x: Coordinate, y: Coordinate, to_x: Coordinate, to_y: Coordinate, duration_ms: Duration = 400) -> dict:
        """Swipe in the natural portrait coordinate plane. Never automatically retry after an ambiguous failure."""
        return await request("swipe", x=x, y=y, to_x=to_x, to_y=to_y, duration_ms=duration_ms)

    @server.tool(annotations=INPUT)
    async def type_text(text: Annotated[str, Field(max_length=1024)]) -> dict:
        """Type supported US-layout ASCII text. Executes directly in the paired control session."""
        return await request("type_text", text=text)

    @server.tool(annotations=INPUT)
    async def press_home() -> dict:
        """Submit a Home button press in the paired control session."""
        return await request("press_home")

    @server.tool(annotations=INPUT)
    async def launch_app(bundle_id: Annotated[str, Field(min_length=1, max_length=255)]) -> dict:
        """Request launching an installed app. Executes directly; private API is experimental."""
        return await request("launch_app", bundle_id=bundle_id)

    @server.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
    async def disconnect() -> dict:
        """Close this adapter's session and cancel further screen/input access."""
        await asyncio.to_thread(adapter.close)
        return {"connected": False}

    return server

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, help="absolute local pairing JSON path")
    parser.add_argument("--control", action="store_true", help="explicitly request input; phone must be unlocked")
    args = parser.parse_args()
    adapter = DeviceAdapter(args.config, args.control)
    try: create_server(adapter).run(transport="stdio")
    finally: adapter.close()

if __name__ == "__main__": main()
