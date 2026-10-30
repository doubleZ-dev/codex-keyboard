"""Exercise actual compiled C debounce with bounced/multi-key/time-boundary traces."""
import ctypes
import sys
import unittest
from pathlib import Path

lib = ctypes.CDLL(str(Path(sys.argv.pop(1)).resolve()))
lib.test_sample.argtypes = [ctypes.c_uint16, ctypes.c_uint64]
lib.test_sample.restype = ctypes.c_uint32
lib.test_reset.argtypes = []
lib.test_reset.restype = None
lib.test_board.argtypes = []
lib.test_board.restype = ctypes.c_int


class FoundationTests(unittest.TestCase):
    def setUp(self):
        lib.test_reset()

    def test_board(self):
        self.assertEqual(lib.test_board(), 1)

    def test_bounced_press_and_release(self):
        for raw, time in [(0, 0), (1, 100), (0, 200), (1, 300), (1, 5299)]:
            self.assertEqual(lib.test_sample(raw, time), 0)
        self.assertEqual(lib.test_sample(1, 5300), 1)
        self.assertEqual(lib.test_sample(1, 6000), 0)
        self.assertEqual(lib.test_sample(0, 7000), 0)
        self.assertEqual(lib.test_sample(1, 7100), 0)
        self.assertEqual(lib.test_sample(0, 7200), 0)
        self.assertEqual(lib.test_sample(0, 12200), 1 << 16)

    def test_all_keys_and_holes(self):
        self.assertEqual(lib.test_sample(0xFFFF, 0), 0)
        self.assertEqual(lib.test_sample(0xFFFF, 5000), 0xEFF3)
        self.assertEqual(lib.test_sample(0, 6000), 0)
        self.assertEqual(lib.test_sample(0, 11000), 0xEFF3 << 16)
        self.assertEqual(lib.test_sample(0x100C, 12000), 0)
        self.assertEqual(lib.test_sample(0x100C, 20000), 0)

    def test_independent_keys(self):
        self.assertEqual(lib.test_sample(1, 0), 0)
        self.assertEqual(lib.test_sample(3, 2000), 0)
        self.assertEqual(lib.test_sample(3, 5000), 1)
        self.assertEqual(lib.test_sample(3, 7000), 2)

    def test_long_uptime_and_backward_timestamp(self):
        start = (1 << 32) - 2000
        self.assertEqual(lib.test_sample(1, start), 0)
        self.assertEqual(lib.test_sample(1, start - 1), 0xFFFFFFFF)
        self.assertEqual(lib.test_sample(1, start + 5000), 1)

    def test_short_pulse_and_late_sample(self):
        self.assertEqual(lib.test_sample(1, 0), 0)
        self.assertEqual(lib.test_sample(0, 4999), 0)
        self.assertEqual(lib.test_sample(0, 10000), 0)
        self.assertEqual(lib.test_sample(1, 11000), 0)
        self.assertEqual(lib.test_sample(1, 50000), 1)
        self.assertEqual(lib.test_sample(1, 50000), 0)


if __name__ == '__main__':
    unittest.main()
