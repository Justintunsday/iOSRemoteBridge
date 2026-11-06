"""Shared, dependency-free MCP catalog and device adapter (Python 3.9+)."""
import base64
import math
import threading

COORD = {"type": "number", "minimum": 0, "maximum": 1}
DURATION = {"type": "integer", "minimum": 40, "maximum": 5000, "default": 400}


def tool(name, description, properties=None, required=None, read=True):
    return {"name": name, "description": description,
            "inputSchema": {"type": "object", "properties": properties or {},
                            "required": required or [], "additionalProperties": False},
            "annotations": {"readOnlyHint": read,
                            "destructiveHint": not read and name != "disconnect",
                            "idempotentHint": read or name == "disconnect",
                            "openWorldHint": not read and name != "disconnect"}}


TOOLS = [
    tool("get_device_info", "Read actual model, iOS, lock state and input readiness."),
    tool("get_screen_frame", "View a current JPEG screen image before selecting an action."),
    tool("get_orientation", "Read natural-portrait capture dimensions and orientation."),
    tool("get_connection_status", "Read this adapter's status without connecting to the phone."),
    tool("get_performance_stats", "Read capture/encode/ack/thermal telemetry; not glass-to-glass latency."),
    tool("tap", "Tap a point observed in the latest image; inspect a fresh image afterwards.",
         {"x": COORD, "y": COORD}, ["x", "y"], False),
    tool("double_tap", "Double tap an observed point, then inspect a fresh image.",
         {"x": COORD, "y": COORD}, ["x", "y"], False),
    tool("long_press", "Hold an observed point, then release and inspect a fresh image.",
         {"x": COORD, "y": COORD, "duration_ms": dict(DURATION, default=600)}, ["x", "y"], False),
    tool("swipe", "Swipe in normalized natural-portrait coordinates; then inspect a fresh image.",
         {"x": COORD, "y": COORD, "to_x": COORD, "to_y": COORD, "duration_ms": DURATION},
         ["x", "y", "to_x", "to_y"], False),
    tool("insert_text", "Insert literal Unicode into the observed focused field, bypassing IME composition. Prefer for ordinary text, Chinese and emoji. At most 1024 UTF-16 units. Does not press Return or require keyboard mode. Never replay an ambiguous result.",
         {"text": {"type": "string", "maxLength": 1024}}, ["text"], False),
    tool("type_text", "Legacy US-layout ASCII key simulation, affected by the active IME. Prefer insert_text for literal text.",
         {"text": {"type": "string", "maxLength": 1024}}, ["text"], False),
    tool("press_home", "Press Home, then inspect the resulting screen.", read=False),
    tool("launch_app", "Request launching a confirmed installed bundle ID. Experimental: inspect the screen even after an error before retrying.",
         {"bundle_id": {"type": "string", "minLength": 1, "maxLength": 255}}, ["bundle_id"], False),
    tool("disconnect", "Release this adapter's device session.", read=False),
]
CATALOG = {entry["name"]: entry for entry in TOOLS}


def screen_image(value):
    if not isinstance(value, dict) or value.get("mime") != "image/jpeg":
        raise ValueError("unexpected image response")
    encoded = value.get("base64")
    if not isinstance(encoded, str) or len(encoded) > 2000000:
        raise ValueError("screen image exceeds response limit")
    base64.b64decode(encoded, validate=True)
    return encoded


def validate(name, arguments):
    if not isinstance(name, str) or name not in CATALOG:
        raise ValueError("unknown tool")
    schema = CATALOG[name]["inputSchema"]
    if not isinstance(arguments, dict):
        raise ValueError("arguments must be an object")
    if set(arguments) - set(schema["properties"]):
        raise ValueError("unknown tool argument")
    if set(schema["required"]) - set(arguments):
        raise ValueError("missing tool argument")
    result = dict(arguments)
    for key, spec in schema["properties"].items():
        if key not in result:
            if "default" in spec:
                result[key] = spec["default"]
            continue
        value = result[key]
        kind = spec["type"]
        if kind in ("number", "integer"):
            if type(value) not in (int, float) or (kind == "integer" and type(value) is not int):
                raise ValueError(key + " must be a " + kind)
            if not math.isfinite(value) or not spec["minimum"] <= value <= spec["maximum"]:
                raise ValueError(key + " is outside its allowed range")
        elif kind == "string":
            if not isinstance(value, str) or not spec.get("minLength", 0) <= len(value) <= spec["maxLength"]:
                raise ValueError(key + " must be a bounded string")
            if name == "type_text" and not value.isascii():
                raise ValueError("type_text supports US-layout ASCII only")
            try:
                units = len(value.encode("utf-16-le")) // 2
            except UnicodeError as exc:
                raise ValueError("text must contain valid Unicode") from exc
            if name == "insert_text" and units > 1024:
                raise ValueError("text exceeds 1024 UTF-16 code units")
    return result


class DeviceAdapter:
    def __init__(self, config, control=False, factory=None):
        self.config, self.control, self.factory = config, control, factory
        self.client = None
        self.lock = threading.RLock()

    def close(self):
        with self.lock:
            client, self.client = self.client, None
            if client is not None:
                client.close()

    def request(self, name, **arguments):
        return self.call(name, arguments)

    def call(self, name, arguments):
        arguments = validate(name, arguments)
        with self.lock:
            if name == "disconnect":
                self.close()
                return {"connected": False}
            if name == "get_connection_status":
                return {"connected": self.client is not None and not getattr(self.client, "error", None),
                        "device": getattr(self.client, "device", None), "input_requested": self.control}
            if not CATALOG[name]["annotations"]["readOnlyHint"] and not self.control:
                raise ValueError("input disabled; this adapter requires --control")
            if self.client is None:
                factory = self.factory
                if factory is None:
                    from .client import BridgeClient
                    factory = BridgeClient
                self.client = factory(self.config, control=self.control, timeout=20)
                try:
                    self.client.connect()
                except BaseException:
                    self.close()
                    raise
            try:
                return self.client.request(name, **arguments)
            except Exception:
                if getattr(self.client, "error", None):
                    self.close()
                raise  # Never replay a failed or ambiguous input operation.
