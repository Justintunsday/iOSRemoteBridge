"""Record periodic iPhone telemetry; does not measure glass-to-glass latency."""
import argparse
import json
from pathlib import Path
import time
from automation import BridgeClient

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--config", required=True)
    p.add_argument("--fps", type=int, choices=(30, 60), default=30)
    p.add_argument("--seconds", type=int, default=120)
    p.add_argument("--output", required=True)
    args = p.parse_args()
    if not 1 <= args.seconds <= 600: p.error("seconds must be 1..600")
    output = Path(args.output); output.parent.mkdir(parents=True, exist_ok=True)
    with BridgeClient(args.config, fps=args.fps) as client, output.open("x", encoding="utf-8") as stream:
        start = time.monotonic()
        stream.write(json.dumps({"device": client.device, "requested_fps": args.fps,
                                 "client_path": "Python encoded-frame consumer; no video decode/display"}) + "\n")
        while time.monotonic() - start < args.seconds:
            stats = client.request("get_performance_stats")
            stats.update(elapsed_s=time.monotonic()-start, network_rtt_ms=client.rtt_ms)
            stream.write(json.dumps(stats, allow_nan=False) + "\n"); stream.flush()
            time.sleep(1)
    print(f"Recorded periodic telemetry to {output}; no display-latency claim is implied.")

if __name__ == "__main__": main()
