import math
import struct
import unittest
from automation import protocol as p

class ProtocolTests(unittest.TestCase):
    def test_cross_language_vector(self):
        h = p.Header(p.VIDEO, 1, 8000000, 0xfefdfcfb, 0x0102030405060708)
        self.assertEqual(h.pack().hex(), "4952423101040100007a1200fefdfcfb0102030405060708")
        self.assertEqual(p.unpack(h.pack()), h)

    def test_reject_lengths_before_allocating(self):
        for kind, limit in ((p.VIDEO, p.MAX_PAYLOAD), (p.CONTROL, p.MAX_CONTROL), (p.RESPONSE, p.MAX_RESPONSE)):
            with self.assertRaises(ValueError): p.Header(kind, 0, limit+1, 1).pack()
        for size in range(24):
            with self.assertRaises(ValueError): p.unpack(b"\0"*size)

    def test_header_corruption(self):
        valid = p.Header(p.CONTROL, 0, 0, 1).pack()
        for index, value in ((0, 0), (4, 2), (5, 99), (6, 128), (7, 1), (6, 1)):
            bad = bytearray(valid); bad[index] = value
            with self.assertRaises(ValueError): p.unpack(bad)

    def test_coordinate_rotation_and_validation(self):
        for rotation in (0, 90, 180, 270):
            for point in ((0, 0), (1, 1), (.2, .3)):
                x, y = p.unrotate(*point, rotation)
                a, b = p.unrotate(x, y, (360-rotation) % 360)
                self.assertAlmostEqual(a, point[0]); self.assertAlmostEqual(b, point[1])
        for value in (math.nan, math.inf, -.01, 1.01):
            with self.assertRaises(ValueError): p.unrotate(value, .5)
        with self.assertRaises(ValueError): p.unrotate(.5, .5, 45)

    def test_fragmented_reads_and_eof(self):
        class Fragmented:
            def __init__(self): self.blocks = iter((b"a", b"bc", b"d", b""))
            def recv(self, count): return next(self.blocks)
        self.assertEqual(p.recv_exact(Fragmented(), 4), b"abcd")
        with self.assertRaises(EOFError): p.recv_exact(Fragmented(), 5)

if __name__ == "__main__": unittest.main()
