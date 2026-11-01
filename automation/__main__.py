"""CLI / persistent JSON-lines adapter. Credentials are never CLI arguments."""
import argparse
import base64
import json
import sys
from pathlib import Path
from .client import BridgeClient

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--control", action="store_true", help="request input on the unlocked paired phone")
    parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
    parser.add_argument("operation", help="operation name, snapshot, or serve")
    parser.add_argument("--params", default="{}", help="JSON operation parameters")
    parser.add_argument("--output", help="snapshot JPEG file")
    args = parser.parse_args()
    with BridgeClient(args.config, control=args.control, fps=args.fps) as client:
        if args.operation == "serve":
            while True:
                line = sys.stdin.buffer.readline(65537)
                if not line: break
                if len(line) > 65536: raise ValueError("request line too long")
                try:
                    request = json.loads(line)
                    op = request.pop("op")
                    result = {"ok": True, "result": client.request(op, **request)}
                except (ValueError, KeyError, TypeError, RuntimeError) as exc:
                    result = {"ok": False, "error": str(exc)}
                print(json.dumps(result), flush=True)
        elif args.operation == "snapshot":
            if not args.output: raise ValueError("snapshot requires --output")
            result = client.request("get_screen_frame")
            Path(args.output).write_bytes(base64.b64decode(result["base64"], validate=True))
            print(json.dumps({"saved": args.output, "width": result["width"], "height": result["height"]}))
        else:
            print(json.dumps(client.request(args.operation, **json.loads(args.params))))

if __name__ == "__main__":
    try: main()
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"iOSRemoteBridge: {exc}", file=sys.stderr)
        raise SystemExit(1)
