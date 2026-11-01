"""IRB1 framing. Keep limits in sync with shared/irb_protocol.h."""
from dataclasses import dataclass
import math
import struct

HEADER = struct.Struct(">4sBBBBIIQ")
AUTH, CONTROL, RESPONSE, VIDEO, PING, PONG = range(1, 7)
START, STOP, CREDIT = 128, 129, 130
MAX_PAYLOAD = 8 * 1024 * 1024
MAX_CONTROL = 65536
MAX_RESPONSE = 2 * 1024 * 1024
TYPES = {AUTH, CONTROL, RESPONSE, VIDEO, PING, PONG, START, STOP, CREDIT}

@dataclass(frozen=True)
class Header:
    kind: int
    flags: int
    length: int
    sequence: int
    pts_us: int = 0

    def pack(self):
        raw = HEADER.pack(b"IRB1", 1, self.kind, self.flags, 0,
                          self.length, self.sequence, self.pts_us)
        unpack(raw)
        return raw

def unpack(raw):
    if len(raw) != HEADER.size:
        raise ValueError("truncated frame header")
    magic, version, kind, flags, reserved, size, seq, pts = HEADER.unpack(raw)
    if magic != b"IRB1" or version != 1 or reserved or kind not in TYPES:
        raise ValueError("invalid frame header")
    if flags & ~1 or (flags and kind != VIDEO):
        raise ValueError("invalid frame flags")
    limit = MAX_PAYLOAD if kind == VIDEO else MAX_RESPONSE if kind == RESPONSE else MAX_CONTROL
    if size > limit:
        raise ValueError("frame exceeds payload limit")
    return Header(kind, flags, size, seq, pts)

def recv_exact(sock, size):
    result = bytearray()
    while len(result) < size:
        block = sock.recv(size - len(result))
        if not block:
            raise EOFError("connection closed")
        result.extend(block)
    return bytes(result)

def unrotate(x, y, rotation=0):
    if not all(isinstance(n, (float, int)) and math.isfinite(n) and 0 <= n <= 1 for n in (x, y)):
        raise ValueError("coordinates must be finite normalized numbers")
    if rotation == 0: return x, y
    if rotation == 90: return y, 1-x
    if rotation == 180: return 1-x, 1-y
    if rotation == 270: return 1-y, x
    raise ValueError("rotation must be 0, 90, 180 or 270")
