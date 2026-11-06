"""Dependency-free MCP stdio entry point for Python 3.9+, including iOS hosts."""
import argparse
import json
import sys
from .mcp_tools import DeviceAdapter as Adapter, TOOLS, screen_image

VERSIONS = {"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"}
MAX_LINE = 65536


class Server:
    def __init__(self, adapter):
        self.adapter = adapter
        self.initialized = False
        self.ready = False

    @staticmethod
    def error(ident, code, message):
        return {"jsonrpc": "2.0", "id": ident, "error": {"code": code, "message": message}}

    def dispatch(self, request):
        if not isinstance(request, dict) or request.get("jsonrpc") != "2.0":
            return self.error(None, -32600, "invalid request")
        ident = request.get("id")
        method = request.get("method")
        if "id" not in request:
            if method == "notifications/initialized" and self.initialized:
                self.ready = True
            return None
        if type(ident) not in (int, str) or not isinstance(method, str):
            return self.error(None, -32600, "invalid request id or method")
        params = request.get("params", {})
        if not isinstance(params, dict):
            return self.error(ident, -32602, "params must be an object")
        if method == "initialize":
            if self.initialized or not isinstance(params.get("protocolVersion"), str):
                return self.error(ident, -32602, "invalid initialization")
            self.initialized = True
            version = params["protocolVersion"]
            result = {"protocolVersion": version if version in VERSIONS else "2025-11-25",
                      "capabilities": {"tools": {"listChanged": False}},
                      "serverInfo": {"name": "iOSRemoteBridge", "version": "1.0.0"}}
        elif method == "ping":
            result = {}
        elif not self.ready:
            return self.error(ident, -32000, "initialize this MCP session first")
        elif method == "tools/list":
            result = {"tools": TOOLS}
        elif method == "tools/call":
            try:
                name = params.get("name")
                if not isinstance(name, str):
                    raise ValueError("tool name must be a string")
                value = self.adapter.call(name, params.get("arguments", {}))
                if name == "get_screen_frame":
                    encoded = screen_image(value)
                    content = [{"type": "image", "mimeType": "image/jpeg", "data": encoded}]
                else:
                    content = [{"type": "text", "text": json.dumps(value, ensure_ascii=False, allow_nan=False)}]
                result = {"content": content, "isError": False}
            except Exception as exc:
                result = {"content": [{"type": "text", "text": str(exc)[:800]}], "isError": True}
        else:
            return self.error(ident, -32601, "method not found")
        return {"jsonrpc": "2.0", "id": ident, "result": result}

    def serve(self, input_stream, output_stream):
        try:
            while True:
                raw = input_stream.readline(MAX_LINE + 1)
                if not raw:
                    break
                if len(raw) > MAX_LINE:
                    response = self.error(None, -32600, "request exceeds line limit")
                else:
                    try:
                        response = self.dispatch(json.loads(raw, parse_constant=reject_constant))
                    except (ValueError, UnicodeError, RecursionError):
                        response = self.error(None, -32700, "invalid JSON")
                if response is not None:
                    line = json.dumps(response, ensure_ascii=False, allow_nan=False) + "\n"
                    output_stream.write(line.encode("utf8"))
                    output_stream.flush()
                if len(raw) > MAX_LINE:
                    break
        finally:
            self.adapter.close()


def reject_constant(value):
    raise ValueError("invalid JSON constant")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()
    Server(Adapter(args.config, args.control)).serve(sys.stdin.buffer, sys.stdout.buffer)


if __name__ == "__main__":
    main()
