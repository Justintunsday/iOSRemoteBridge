"""Exercise the compiled Windows client over real loopback TLS with H.264."""
import json
from collections import deque
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from automation import protocol as p
from tools.provision import provision

def run(executable, fixture, wrong_pin, lockscreen=False):
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp) / "pairing"
        provision("127.0.0.1", folder)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(folder / "server.crt", folder / "server.key")
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(20)
        config = json.loads((folder / "client-config.json").read_text())
        config["port"] = listener.getsockname()[1]
        if wrong_pin: config["certificate_sha256"] = "0" * 64
        config_path = folder / "client-config.json"
        config_path.write_text(json.dumps(config))
        auth_seen = threading.Event()
        ack_seen = threading.Event()
        actions_seen = []
        errors = []
        def serve():
            try:
                raw, _ = listener.accept()
                with context.wrap_socket(raw, server_side=True) as sock:
                    sock.settimeout(20)
                    h = p.unpack(p.recv_exact(sock, 24))
                    auth = json.loads(p.recv_exact(sock, h.length)); auth_seen.set()
                    if h.kind != p.AUTH or auth["token"] != config["token"]: raise ValueError("incorrect authentication")
                    if auth.get("video_window") != 3 or auth.get("fps") != 60 or auth.get("lockscreen_view") is not True: raise ValueError("interactive pipeline negotiation missing")
                    sequence = 0
                    pending_video = deque()
                    final_ready = False
                    lock_sent = False
                    def send(kind, data, pts=0):
                        nonlocal sequence
                        sequence += 1
                        body = json.dumps(data).encode() if isinstance(data, dict) else data
                        header = p.Header(kind, 1 if kind == p.VIDEO else 0, len(body), sequence, pts).pack()
                        packet = header + body
                        for start in range(0, len(packet), 97): sock.sendall(packet[start:start+97])
                    device = {"model": "TLS_SIMULATOR"}
                    if lockscreen: device.update(lockscreen_view=True, locked=False, display_on=True,
                        input_allowed=True, input_epoch=1, min_visible_pts=0, wake_supported=True)
                    send(p.RESPONSE, {"event": "authorized", "device": device})
                    pending_video.append(123456)
                    send(p.VIDEO, Path(fixture).read_bytes(), 123456)
                    pending_video.append(123457)
                    send(p.VIDEO, Path(fixture).read_bytes(), 123457)
                    pending_video.append(123458)
                    send(p.VIDEO, Path(fixture).read_bytes(), 123458)
                    received_sequence = h.sequence
                    while True:
                        h = p.unpack(p.recv_exact(sock, 24)); payload = p.recv_exact(sock, h.length)
                        received_sequence += 1
                        if h.sequence != received_sequence: raise ValueError("client sequence mismatch")
                        if h.kind == p.CREDIT:
                            if h.length or not pending_video or pending_video.popleft() != h.pts_us:
                                raise ValueError("client frame credit out of order")
                            if h.pts_us == 123459: ack_seen.set()
                            if lockscreen and h.pts_us == 123459:
                                lock_sent = True
                                device.update(locked=True, display_on=False, input_allowed=False, input_epoch=2, min_visible_pts=123461)
                                send(p.RESPONSE, {"event": "device_state", "device": device})
                                pending_video.append(123460);send(p.VIDEO, Path(fixture).read_bytes(), 123460)
                        elif h.kind == p.PING: send(p.PONG, payload, h.pts_us)
                        elif h.kind == p.CONTROL:
                            request = json.loads(payload)
                            actions_seen.append(request["op"])
                            if lockscreen and request["op"] not in ("get_device_info", "wake_screen") and request.get("input_epoch") != device["input_epoch"]:
                                raise ValueError("missing current input epoch")
                            if request["op"] == "tap":
                                send(p.RESPONSE, {"id": request["id"], "ok": 0, "error": "invalid coordinates"})
                            else:
                                # Match the actual iOS C-int boxing bug for input
                                # replies; get_device_info exercises standard booleans.
                                send(p.RESPONSE, {"id": request["id"], "ok": True if request["op"] == "get_device_info" else 1,
                                                  "result": {"model": "TLS_SIMULATOR"}})
                            if len(actions_seen) == 7:
                                final_ready = True
                            if lockscreen and request['op'] == 'wake_screen':
                                if not lock_sent: raise ValueError('wake preceded lock')
                                device.update(locked=False, display_on=True, input_allowed=True, input_epoch=3, min_visible_pts=123461)
                                send(p.RESPONSE, {"event": "device_state", "device": device})
                                pending_video.append(123461);send(p.VIDEO, Path(fixture).read_bytes(), 123461)
                        if final_ready and len(pending_video) < 3:
                            pending_video.append(123459)
                            send(p.VIDEO, Path(fixture).read_bytes(), 123459)
                            final_ready = False
            except (EOFError, ConnectionError): pass
            except Exception as exc: errors.append(str(exc))
        thread = threading.Thread(target=serve, daemon=True); thread.start()
        command = [str(Path(executable).resolve()), str(config_path)]
        if wrong_pin: command.append("--expect-pin-failure")
        elif lockscreen: command.append("--lockscreen")
        try:
            subprocess.run(command, check=True, timeout=25)
            if wrong_pin:
                if auth_seen.is_set(): raise AssertionError("credentials sent to incorrect certificate")
            elif not auth_seen.is_set() or not ack_seen.wait(2) or actions_seen != ["get_device_info", "press_home", "touch", "touch", "touch", "tap", "get_device_info"]+(["wake_screen", "press_home"] if lockscreen else []):
                raise AssertionError("missing actions or decoded-frame acknowledgement after input")
            if errors: raise AssertionError(errors)
        finally:
            listener.close(); thread.join(timeout=3)

if __name__ == "__main__":
    run(sys.argv[1], sys.argv[2], False)
    run(sys.argv[1], sys.argv[2], False, True)
    run(sys.argv[1], sys.argv[2], True)
    print("Native TLS/H.264 integration and wrong-certificate credential protection passed.")
