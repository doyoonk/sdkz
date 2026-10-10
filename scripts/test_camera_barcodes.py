# SPDX-License-Identifier: Apache-2.0
import unittest
from camera_capture import HuError, read_barcodes


class BarcodeProtocolTests(unittest.TestCase):
    def test_pending_and_binary_payload_chunks(self):
        payload = bytes(range(256)) + b"\x00\x1d\x1e\x04GS1"
        calls = []

        class Board:
            pending = True

            def request(self, command, sid, generation, index, offset):
                calls.append((command, sid, generation, index, offset))
                if index == 0:
                    if self.pending:
                        self.pending = False
                        raise HuError(-11)
                    return list(map(str, [sid, 9, 1, 0, "0", 800, 480, 800, 480, 37, 80]))
                return list(map(str, [sid, 9, 1, "CODE128", "]C1", len(payload), offset,
                                      payload[offset:offset + 256].hex(),
                                      10, 10, 90, 10, 90, 50, 10, 50, 0]))

        scan = read_barcodes(Board(), 123)
        self.assertEqual(bytes.fromhex(scan["symbols"][0]["payload_hex"]), payload)
        self.assertEqual([call[-1] for call in calls[-2:]], [0, 256])

    def test_wrong_snapshot_and_generation_are_rejected(self):
        class WrongSnapshot:
            def request(self, *args):
                return list(map(str, [999, 9, 0, 0, "0", 800, 480, 800, 480, 1, 1]))
        with self.assertRaises(ValueError):
            read_barcodes(WrongSnapshot(), 123)

        class WrongGeneration:
            def request(self, command, sid, generation, index, offset):
                if index == 0:
                    return list(map(str, [sid, 9, 1, 0, "0", 800, 480, 800, 480, 1, 1]))
                return list(map(str, [sid, 10, 1, "QR", "]Q1", 1, 0, "61",
                                      0, 0, 10, 0, 10, 10, 0, 10, 0]))
        with self.assertRaises(ValueError):
            read_barcodes(WrongGeneration(), 123)

    def test_partial_empty_scan_and_pending_timeout(self):
        class Partial:
            def request(self, *args):
                return list(map(str, [123, 9, 0, 1, "10", 800, 480, 800, 480, 1, 1]))
        result = read_barcodes(Partial(), 123)
        self.assertEqual(result["capacity_flags"], 16)
        self.assertEqual(result["symbols"], [])

        class Pending:
            def request(self, *args):
                raise HuError(-11)
        with self.assertRaises(HuError):
            read_barcodes(Pending(), 123, timeout=0)


if __name__ == "__main__":
    unittest.main()
