import base64
import unittest
import sys
from pathlib import Path
try:
    from mcp import Client, StdioServerParameters
    from automation.mcp_server import DeviceAdapter, create_server
except ImportError:
    Client = None

@unittest.skipIf(Client is None, "optional MCP SDK not installed")
class MCPTests(unittest.IsolatedAsyncioTestCase):
    async def test_real_stdio_startup_without_device_or_credentials(self):
        server = StdioServerParameters(command=sys.executable,
            args=["-m", "automation.mcp_server", "--config", "nonexistent-pairing-file.json"],
            cwd=Path(__file__).resolve().parents[2])
        async with Client(server) as client:
            tools = await client.list_tools()
            self.assertIn("get_screen_frame", {tool.name for tool in tools.tools})
            status = await client.call_tool("get_connection_status", {})
            self.assertFalse(status.is_error)

    async def test_discovery_readonly_gate_and_validation(self):
        calls = []
        class Fake:
            error = None
            device = {"model": "MCP_SIMULATOR"}
            def __init__(self, *args, **kwargs): pass
            def connect(self): return self
            def close(self): pass
            def request(self, op, **params): calls.append((op, params)); return {"model": "MCP_SIMULATOR"}
        adapter = DeviceAdapter("unused", factory=Fake)
        async with Client(create_server(adapter)) as client:
            tools = await client.list_tools()
            names = {t.name for t in tools.tools}
            self.assertTrue({"tap", "get_screen_frame", "get_device_info", "disconnect"} <= names)
            result = await client.call_tool("get_device_info", {})
            self.assertFalse(result.is_error)
            self.assertEqual(calls[-1][0], "get_device_info")
            count = len(calls)
            result = await client.call_tool("tap", {"x": .5, "y": .5})
            self.assertTrue(result.is_error)
            self.assertEqual(len(calls), count)
            result = await client.call_tool("tap", {"x": -1, "y": .5})
            self.assertTrue(result.is_error)
            self.assertEqual(len(calls), count)
        adapter.close()

    async def test_approved_adapter_routes_input_and_images(self):
        calls = []
        class Fake:
            error = None
            device = {}
            def __init__(self, *args, **kwargs): self.control = kwargs["control"]
            def connect(self): return self
            def close(self): pass
            def request(self, op, **params):
                calls.append((op, params))
                if op == "get_screen_frame": return {"mime": "image/jpeg", "base64": base64.b64encode(b"jpeg-fixture").decode()}
                return {"state": "completed"}
        adapter = DeviceAdapter("unused", control=True, factory=Fake)
        async with Client(create_server(adapter)) as client:
            result = await client.call_tool("swipe", {"x": .5, "y": .8, "to_x": .5, "to_y": .2, "duration_ms": 300})
            self.assertFalse(result.is_error)
            self.assertEqual(calls[-1][0], "swipe")
            result = await client.call_tool("get_screen_frame", {})
            self.assertFalse(result.is_error)
            self.assertEqual(result.content[0].type, "image")
            self.assertEqual(result.content[0].mime_type, "image/jpeg")
            text = "AI 直接输入\n你好 😀 café 👩🏽‍💻"
            result = await client.call_tool("insert_text", {"text": text})
            self.assertFalse(result.is_error)
            self.assertEqual(calls[-1], ("insert_text", {"text": text}))
            result = await client.call_tool("insert_text", {"text": "😀" * 512})
            self.assertFalse(result.is_error)
            count = len(calls)
            for invalid in ("😀" * 513, "\ud800", "\udc00"):
                result = await client.call_tool("insert_text", {"text": invalid})
                self.assertTrue(result.is_error)
                self.assertEqual(len(calls), count)
        adapter.close()

if __name__ == "__main__": unittest.main()
