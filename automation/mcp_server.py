"""Optional standard MCP stdio adapter. No network listener is opened locally."""
import argparse
import asyncio
import base64
from typing import Annotated
from mcp.server import MCPServer
from mcp.server.mcpserver import Image
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import Tool as MCPTool, ToolAnnotations
from pydantic import Field
from .client import BridgeError
from .mcp_tools import CATALOG, TOOLS, DeviceAdapter, screen_image, validate

Coordinate = Annotated[float, Field(ge=0, le=1, allow_inf_nan=False)]
Duration = Annotated[int, Field(ge=40, le=5000)]
class BridgeMCPServer(MCPServer):
    async def list_tools(self):
        return [MCPTool(**entry) for entry in TOOLS]

    async def call_tool(self, name, arguments, context=None):
        try:
            arguments = validate(name, arguments)
        except ValueError as exc:
            raise ToolError(str(exc)) from exc
        return await super().call_tool(name, arguments, context)


def create_server(adapter):
    server = BridgeMCPServer("iOSRemoteBridge")

    def register(function):
        spec = CATALOG[function.__name__]
        return server.tool(description=spec["description"],
                           annotations=ToolAnnotations(**spec["annotations"]))(function)

    async def request(op, **parameters):
        try: return await asyncio.to_thread(adapter.request, op, **parameters)
        except (BridgeError, OSError, ValueError) as exc: raise ToolError(str(exc)) from exc

    @register
    async def get_device_info() -> dict:
        """Inspect device model, iOS, capture dimensions and runtime capability status. First call connects to the unlocked paired phone."""
        return await request("get_device_info")

    @register
    async def get_screen_frame() -> Image:
        """Inspect a current JPEG screen frame during an paired session. Protected output is not bypassed."""
        result = await request("get_screen_frame")
        return Image(data=base64.b64decode(screen_image(result), validate=True), format="jpeg")

    @register
    async def get_orientation() -> dict:
        """Read capture-plane orientation metadata; coordinates are natural portrait normalized values."""
        return await request("get_orientation")

    @register
    async def get_connection_status() -> dict:
        """Inspect this adapter's local session status without connecting."""
        return await request("get_connection_status")

    @register
    async def get_performance_stats() -> dict:
        """Read periodic capture/encode/ack/thermal telemetry. This does not measure glass-to-glass latency."""
        return await request("get_performance_stats")

    @register
    async def tap(x: Coordinate, y: Coordinate) -> dict:
        """Tap a normalized natural-portrait point. Requires a paired control session."""
        return await request("tap", x=x, y=y)

    @register
    async def double_tap(x: Coordinate, y: Coordinate) -> dict:
        """Double tap a point in the paired control session."""
        return await request("double_tap", x=x, y=y)

    @register
    async def long_press(x: Coordinate, y: Coordinate, duration_ms: Duration = 600) -> dict:
        """Hold one point, then release. Completion reports scheduling, not semantic UI success."""
        return await request("long_press", x=x, y=y, duration_ms=duration_ms)

    @register
    async def swipe(x: Coordinate, y: Coordinate, to_x: Coordinate, to_y: Coordinate, duration_ms: Duration = 400) -> dict:
        """Swipe in the natural portrait coordinate plane. Never automatically retry after an ambiguous failure."""
        return await request("swipe", x=x, y=y, to_x=to_x, to_y=to_y, duration_ms=duration_ms)

    @register
    async def insert_text(text: Annotated[str, Field(max_length=1024)]) -> dict:
        """Insert literal Unicode into the observed focused field, bypassing IME composition. Prefer this for Chinese, emoji and ordinary text. Maximum 1024 UTF-16 units. Does not press Return or enable keyboard mode; never retry an ambiguous result."""
        return await request("insert_text", text=text)

    @register
    async def type_text(text: Annotated[str, Field(max_length=1024)]) -> dict:
        """Legacy US-layout ASCII key simulation; affected by the active IME. Prefer insert_text for literal text. Executes in the paired control session."""
        return await request("type_text", text=text)

    @register
    async def press_home() -> dict:
        """Submit a Home button press in the paired control session."""
        return await request("press_home")

    @register
    async def launch_app(bundle_id: Annotated[str, Field(min_length=1, max_length=255)]) -> dict:
        """Request launching an installed app. Executes directly; private API is experimental."""
        return await request("launch_app", bundle_id=bundle_id)

    @register
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
