"""Real loopback TLS test. Simulates the protocol, not iOS private APIs."""
import hashlib
import json
import socket
import ssl
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor
import unittest
from pathlib import Path
from automation import BridgeClient, BridgeError
from automation import protocol as p
from tools.provision import provision

class TLSTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.folder = Path(cls.temp.name) / "pairing"
        cls.pin = provision("127.0.0.1", cls.folder)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def server(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(5)
        self.addCleanup(listener.close)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(self.folder / "server.crt", self.folder / "server.key")
        config = json.loads((self.folder / "client-config.json").read_text())
        config["port"] = listener.getsockname()[1]
        observed = {"auth": False, "error": None, "credit": threading.Event(),
                    "ping": threading.Event(), "deferred": threading.Event(), "credits": []}
        def handle():
            try:
                raw, _ = listener.accept()
                with context.wrap_socket(raw, server_side=True) as sock:
                    sock.settimeout(5)
                    h = p.unpack(p.recv_exact(sock, 24)); auth = json.loads(p.recv_exact(sock, h.length))
                    observed["auth"] = True
                    observed["video_window"] = auth.get("video_window", 1)
                    if h.kind != p.AUTH or auth["token"] != config["token"]: return
                    def send(kind, seq, payload, pts=0):
                        body = json.dumps(payload).encode() if isinstance(payload, dict) else payload
                        packet = p.Header(kind, 0, len(body), seq, pts).pack() + body
                        # Split both the header and payload across writes.
                        for i in range(0, len(packet), 7): sock.sendall(packet[i:i+7])
                    send(p.RESPONSE, 1, {"event": "approval_required"})
                    send(p.RESPONSE, 2, {"event": "authorized", "device": {"model": "SIMULATOR"}})
                    send(p.VIDEO, 3, b"\0\0\0\1fake-frame", 12345)
                    seq = 3
                    for offset in range(1, observed["video_window"]):
                        seq += 1; send(p.VIDEO, seq, b"\0\0\0\1next-frame", 12345+offset)
                    while True:
                        h = p.unpack(p.recv_exact(sock, 24)); data = p.recv_exact(sock, h.length)
                        if h.kind == p.CREDIT:
                            observed["credits"].append(h.pts_us)
                            if len(observed["credits"]) == observed["video_window"]: observed["credit"].set()
                        elif h.kind == p.PING:
                            observed["ping"].set()
                            seq += 1; send(p.PONG, seq, data, h.pts_us)
                        elif h.kind == p.CONTROL:
                            request = json.loads(data)
                            if request["op"] == "defer": observed["deferred"].set(); continue
                            seq += 1
                            if request["op"] == "bad_sequence":
                                send(p.RESPONSE, seq+1, {"id": request["id"], "ok": True, "result": {}})
                            elif request["op"] == "reject":
                                send(p.RESPONSE, seq, {"id": request["id"], "ok": False, "error": "owner declined"})
                            else:
                                send(p.RESPONSE, seq, {"id": request["id"], "ok": True, "result": {"echo": request["op"]}})
            except (OSError, EOFError, ValueError) as exc: observed["error"] = type(exc).__name__
        thread = threading.Thread(target=handle, daemon=True); thread.start()
        self.addCleanup(lambda: thread.join(timeout=3))
        return config, observed

    def test_authenticated_fragmented_video_rpc_and_rejection(self):
        config, observed = self.server()
        with BridgeClient(config, timeout=5) as client:
            self.assertEqual(client.device["model"], "SIMULATOR")
            self.assertEqual(client.request("get_device_info"), {"echo": "get_device_info"})
            self.assertTrue(observed["auth"])
            self.assertTrue(observed["credit"].wait(2))
            self.assertEqual(observed["video_window"], 3)
            self.assertEqual(observed["credits"], [12345, 12346, 12347])
            with self.assertRaisesRegex(BridgeError, "owner declined"): client.request("reject")

    def test_legacy_single_frame_window(self):
        config, observed = self.server()
        with BridgeClient(config, timeout=5, video_window=1) as client:
            self.assertEqual(client.request("get_device_info"), {"echo": "get_device_info"})
            self.assertTrue(observed["credit"].wait(2))
            self.assertEqual(observed["credits"], [12345])

    def test_invalid_frame_window_rejected_before_network(self):
        config = json.loads((self.folder / "client-config.json").read_text())
        for value in (0, 4, True, 1.5, "2"):
            with self.assertRaisesRegex(ValueError, "video_window"):
                BridgeClient(config, video_window=value)

    def test_wrong_pin_sends_no_token(self):
        config, observed = self.server(); config["certificate_sha256"] = "0"*64
        with self.assertRaisesRegex(BridgeError, "pin mismatch"):
            BridgeClient(config, timeout=3).connect()
        self.assertFalse(observed["auth"])

    def test_out_of_order_response_fails_pending_rpc(self):
        config, _ = self.server()
        with BridgeClient(config, timeout=5) as client:
            with self.assertRaisesRegex(BridgeError, "sequence mismatch"): client.request("bad_sequence")

    def test_missing_pin_is_rejected_before_network(self):
        with self.assertRaises(ValueError): BridgeClient({"host": "127.0.0.1", "token": "1"*64})

    def test_concurrent_rpcs_and_heartbeat_share_one_tls_io_thread(self):
        config, observed = self.server()
        with BridgeClient(config, timeout=5) as client, ThreadPoolExecutor(max_workers=4) as workers:
            results = list(workers.map(lambda _: client.request("get_device_info"), range(100)))
            self.assertTrue(all(result == {"echo": "get_device_info"} for result in results))
            self.assertTrue(observed["credit"].wait(2))
            self.assertTrue(observed["ping"].wait(3))

    def test_close_cancels_a_pending_operation_without_replay(self):
        config, observed = self.server()
        with BridgeClient(config, timeout=5) as client, ThreadPoolExecutor(max_workers=1) as workers:
            pending = workers.submit(client.request, "defer")
            self.assertTrue(observed["deferred"].wait(2))
            client.close()
            with self.assertRaisesRegex(BridgeError, "connection closed"): pending.result(timeout=2)

if __name__ == "__main__": unittest.main()
