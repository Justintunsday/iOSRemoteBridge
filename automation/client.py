"""Pinned TLS client. A physical approval is required for every new session."""
import hashlib
import hmac
import json
import queue
import re
import select
import socket
import ssl
import threading
import time
from pathlib import Path
from . import protocol as wire

class BridgeError(RuntimeError):
    pass

class BridgeClient:
    def __init__(self, config, *, control=False, fps=30, timeout=60, video_window=2):
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
        if type(video_window) is not int or video_window not in (1, 2): raise ValueError("video_window must be 1 or 2")
        self.video_window = video_window
        self.control, self.fps, self.timeout = control, fps, timeout
        self.sock = None
        self._sequence = self._rx = self._request = 0
        self._send_lock = threading.Lock()
        self._outgoing = queue.Queue(maxsize=128)
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
            self.sock.setblocking(False)
            self._send(wire.AUTH, {"token": self.config["token"], "control": self.control, "fps": self.fps, "video_window": self.video_window})
            self._reader = threading.Thread(target=self._read, daemon=True)
            self._reader.start()
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
            try: self._outgoing.put_nowait(header + payload)
            except queue.Full as exc: raise BridgeError("outgoing queue full") from exc

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
        received = bytearray()
        pending = None
        offset = 0
        last_receive = last_ping = time.monotonic()
        try:
            while not self._stop.is_set():
                now = time.monotonic()
                if now - last_receive > self.timeout + 10: raise BridgeError("TLS peer timed out")
                if now - last_ping > 2:
                    self._send(wire.PING, pts=time.monotonic_ns() // 1000)
                    last_ping = now
                if pending is None:
                    try: pending = self._outgoing.get_nowait(); offset = 0
                    except queue.Empty: pass
                if pending is not None:
                    try:
                        sent = sock.send(memoryview(pending)[offset:])
                        if not sent: raise EOFError("TLS connection closed")
                        offset += sent
                        if offset == len(pending): pending = None
                    except (ssl.SSLWantReadError, ssl.SSLWantWriteError): pass
                readable = sock.pending() or select.select([sock], [], [], .01)[0]
                if readable:
                    try:
                        block = sock.recv(65536)
                        if not block: raise EOFError("TLS connection closed")
                        received.extend(block); last_receive = time.monotonic()
                    except (ssl.SSLWantReadError, ssl.SSLWantWriteError): pass
                while len(received) >= wire.HEADER.size:
                    header = wire.unpack(received[:wire.HEADER.size])
                    if header.kind not in (wire.VIDEO, wire.RESPONSE, wire.PONG):
                        raise BridgeError("unexpected server frame type")
                    total = wire.HEADER.size + header.length
                    if len(received) < total: break
                    if header.sequence != self._rx + 1: raise BridgeError("server sequence mismatch")
                    self._rx += 1
                    payload = bytes(received[wire.HEADER.size:total]); del received[:total]
                    if header.kind == wire.VIDEO:
                        self.last_video, self.last_frame_pts = payload, header.pts_us
                        self._send(wire.CREDIT, pts=header.pts_us)
                    elif header.kind == wire.PONG:
                        self.rtt_ms = (time.monotonic_ns() // 1000 - header.pts_us) / 1000
                    else:
                        obj = json.loads(payload)
                        if not isinstance(obj, dict): raise BridgeError("invalid server JSON")
                        if "id" in obj:
                            with self._pending_lock:
                                response_queue = self._pending.get(obj["id"])
                                if response_queue: response_queue.put_nowait(obj)
                        else: self._events.put_nowait(obj)
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
        self._failed(BridgeError("connection closed"))
        if self.sock:
            try: self.sock.shutdown(socket.SHUT_RDWR)
            except OSError: pass
            self.sock.close()
            self.sock = None

    def __enter__(self): return self.connect()
    def __exit__(self, *args): self.close()
