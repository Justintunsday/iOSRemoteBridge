"""Pinned TLS client. A physical approval is required for every new session."""
import hashlib
import hmac
import json
import queue
import re
import socket
import ssl
import threading
import time
from pathlib import Path
from . import protocol as wire

class BridgeError(RuntimeError):
    pass

class BridgeClient:
    def __init__(self, config, *, control=False, fps=30, timeout=60):
        if isinstance(config, (str, Path)):
            config = json.loads(Path(config).read_text(encoding="utf-8"))
        self.config = dict(config)
        for field in ("token", "certificate_sha256"):
            if not re.fullmatch(r"[0-9a-fA-F]{64}", str(config.get(field, ""))):
                raise ValueError(f"{field} must be 64 hexadecimal characters")
        if not isinstance(config.get("host"), str) or not config["host"].strip():
            raise ValueError("host is required")
        port = config.get("port", 7443)
        if type(port) is not int or not 1024 <= port <= 65535:
            raise ValueError("port must be 1024..65535")
        if fps not in (30, 60): raise ValueError("fps must be 30 or 60")
        self.control, self.fps, self.timeout = control, fps, timeout
        self.sock = None
        self._sequence = self._rx = self._request = 0
        self._send_lock = threading.Lock()
        self._pending_lock = threading.Lock()
        self._pending = {}
        self._events = queue.Queue(maxsize=8)
        self._stop = threading.Event()
        self._reader = self._heartbeat = None
        self.device = None
        self.last_video = None
        self.last_frame_pts = None
        self.error = None
        self.rtt_ms = None

    def connect(self):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        # Trust is the mandatory exact certificate pin, checked before AUTH.
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        raw = socket.create_connection((self.config["host"], self.config.get("port", 7443)), timeout=10)
        raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            self.sock = context.wrap_socket(raw, server_hostname=self.config["host"])
            pin = hashlib.sha256(self.sock.getpeercert(binary_form=True)).hexdigest()
            if not hmac.compare_digest(pin, self.config["certificate_sha256"].lower()):
                raise BridgeError("server certificate pin mismatch; no credentials sent")
            self.sock.settimeout(self.timeout + 10)
            self._send(wire.AUTH, {"token": self.config["token"], "control": self.control, "fps": self.fps})
            self._reader = threading.Thread(target=self._read, daemon=True)
            self._reader.start()
            self._heartbeat = threading.Thread(target=self._ping, daemon=True)
            self._heartbeat.start()
            deadline = time.monotonic() + self.timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0: raise BridgeError("device-owner approval timed out")
                try: event = self._events.get(timeout=remaining)
                except queue.Empty as exc: raise BridgeError("device-owner approval timed out") from exc
                if isinstance(event, Exception): raise event
                if event.get("event") == "authorized":
                    self.device = event["device"]
                    return self
                if event.get("event") == "error": raise BridgeError(event.get("error", "device error"))
        except BaseException:
            self.close()
            raw.close()
            raise

    def _send(self, kind, obj=None, *, pts=0):
        payload = b"" if obj is None else json.dumps(obj, separators=(",", ":"), allow_nan=False).encode()
        with self._send_lock:
            if not self.sock or self._stop.is_set(): raise BridgeError("not connected")
            self._sequence += 1
            header = wire.Header(kind, 0, len(payload), self._sequence, pts).pack()
            self.sock.sendall(header + payload)

    def _ping(self):
        while not self._stop.wait(2):
            try: self._send(wire.PING, pts=time.monotonic_ns() // 1000)
            except (OSError, ValueError, BridgeError) as exc:
                self._failed(BridgeError(str(exc)))
                return

    def _failed(self, error):
        self.error = error
        self._stop.set()
        try: self._events.put_nowait(error)
        except queue.Full: pass
        with self._pending_lock:
            for pending in self._pending.values():
                try: pending.put_nowait(error)
                except queue.Full: pass

    def _read(self):
        sock = self.sock
        try:
            while not self._stop.is_set():
                header = wire.unpack(wire.recv_exact(sock, wire.HEADER.size))
                if header.sequence != self._rx + 1: raise BridgeError("server sequence mismatch")
                self._rx += 1
                if header.kind not in (wire.VIDEO, wire.RESPONSE, wire.PONG):
                    raise BridgeError("unexpected server frame type")
                payload = wire.recv_exact(sock, header.length)
                if header.kind == wire.VIDEO:
                    self.last_video, self.last_frame_pts = payload, header.pts_us
                    # Automation consumes rather than decodes video. JPEG snapshots
                    # are an explicit RPC; acknowledging video prevents idle stalls.
                    self._send(wire.CREDIT, pts=header.pts_us)
                elif header.kind == wire.PONG:
                    self.rtt_ms = (time.monotonic_ns() // 1000 - header.pts_us) / 1000
                else:
                    obj = json.loads(payload)
                    if not isinstance(obj, dict): raise BridgeError("invalid server JSON")
                    if "id" in obj:
                        with self._pending_lock:
                            pending = self._pending.get(obj["id"])
                            if pending: pending.put_nowait(obj)
                    else:
                        self._events.put_nowait(obj)
        except (EOFError, OSError, ValueError, KeyError, TypeError, queue.Full, BridgeError) as exc:
            self._failed(BridgeError(str(exc)))

    def request(self, op, **parameters):
        if self._stop.is_set(): raise self.error or BridgeError("connection closed")
        if "id" in parameters or "op" in parameters: raise ValueError("reserved request fields")
        result = queue.Queue(maxsize=1)
        with self._pending_lock:
            self._request += 1
            ident = self._request
            self._pending[ident] = result
        try:
            self._send(wire.CONTROL, {"op": op, "id": ident, **parameters})
            try: response = result.get(timeout=self.timeout)
            except queue.Empty as exc: raise BridgeError("operation timed out; inspect device state before retrying input") from exc
            if isinstance(response, Exception): raise response
            if not response.get("ok"): raise BridgeError(response.get("error", "operation failed"))
            return response.get("result")
        finally:
            with self._pending_lock: self._pending.pop(ident, None)

    def close(self):
        self._stop.set()
        if self.sock:
            try: self.sock.shutdown(socket.SHUT_RDWR)
            except OSError: pass
            self.sock.close()
            self.sock = None

    def __enter__(self): return self.connect()
    def __exit__(self, *args): self.close()
