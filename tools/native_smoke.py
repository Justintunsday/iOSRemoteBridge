"""Exercise the compiled Windows client over real loopback TLS with H.264."""
import json
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

def run(executable, fixture, wrong_pin):
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
        errors = []
        def serve():
            try:
                raw, _ = listener.accept()
                with context.wrap_socket(raw, server_side=True) as sock:
                    sock.settimeout(20)
                    h = p.unpack(p.recv_exact(sock, 24))
                    auth = json.loads(p.recv_exact(sock, h.length)); auth_seen.set()
                    if h.kind != p.AUTH or auth["token"] != config["token"]: raise ValueError("incorrect authentication")
                    sequence = 0
                    def send(kind, data, pts=0):
                        nonlocal sequence
                        sequence += 1
                        body = json.dumps(data).encode() if isinstance(data, dict) else data
                        header = p.Header(kind, 1 if kind == p.VIDEO else 0, len(body), sequence, pts).pack()
                        packet = header + body
                        for start in range(0, len(packet), 97): sock.sendall(packet[start:start+97])
                    send(p.RESPONSE, {"event": "authorized", "device": {"model": "TLS_SIMULATOR"}})
                    send(p.VIDEO, Path(fixture).read_bytes(), 123456)
                    while True:
                        h = p.unpack(p.recv_exact(sock, 24)); payload = p.recv_exact(sock, h.length)
                        if h.kind == p.CREDIT and h.pts_us == 123456: ack_seen.set()
                        elif h.kind == p.PING: send(p.PONG, payload, h.pts_us)
                        elif h.kind == p.CONTROL:
                            request = json.loads(payload)
                            send(p.RESPONSE, {"id": request["id"], "ok": True, "result": {"model": "TLS_SIMULATOR"}})
            except (EOFError, ConnectionError): pass
            except Exception as exc: errors.append(str(exc))
        thread = threading.Thread(target=serve, daemon=True); thread.start()
        command = [str(Path(executable).resolve()), str(config_path)]
        if wrong_pin: command.append("--expect-pin-failure")
        try:
            subprocess.run(command, check=True, timeout=25)
            if wrong_pin:
                if auth_seen.is_set(): raise AssertionError("credentials sent to incorrect certificate")
            elif not auth_seen.is_set() or not ack_seen.wait(2):
                raise AssertionError("missing authentication or decoded-frame acknowledgement")
            if errors: raise AssertionError(errors)
        finally:
            listener.close(); thread.join(timeout=3)

if __name__ == "__main__":
    run(sys.argv[1], sys.argv[2], False)
    run(sys.argv[1], sys.argv[2], True)
    print("Native TLS/H.264 integration and wrong-certificate credential protection passed.")
