# Copyright (c) 2025 HU Inc. All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0

import base64
import binascii
import tempfile
import unittest
from unittest.mock import patch, MagicMock
from pathlib import Path
from fwloader import HuError, HuSerial, Partition, load, prepare_image, read_hex, verify, main


def record(kind, address=0, data=b""):
    raw = bytes([len(data)]) + address.to_bytes(2, "big") + bytes([kind]) + data
    return ":" + (raw + bytes([-sum(raw) & 255])).hex() + "\n"


class Board:
    def __init__(self, protection=True, failure=None):
        self.calls = []
        self.memory = bytearray(512)
        self.protection = protection
        self.unsupported = -134
        self.failure = failure

    def request(self, command, *args, **kwargs):
        self.calls.append((command, *args))
        if command == self.failure:
            raise TimeoutError(command)
        if command == "wrprt":
            if not self.protection:
                raise HuError(command, self.unsupported)
            return list(args)
        if command == "erase":
            self.memory[:] = b"\xff" * 512
            return list(args)
        if command == "flash":
            part, offset, size, data = args
            offset, size = int(offset, 16), int(size, 16)
            raw = base64.a85decode(data)
            assert len(raw) == size and offset % 8 == 0 and size % 8 == 0
            self.memory[offset:offset + size] = raw
            return list(args[:3])
        if command == "flash_read":
            part, offset, size = args
            begin, length = int(offset, 16), int(size, 16)
            return [part, offset, size, self.memory[begin:begin + length].hex()]
        if command == "flash_info":
            return ["slot1", "200000", "200", "8", "ff", "70200000", "100"]
        raise AssertionError(command)


class SerialPort:
    def __init__(self, mode="ok"):
        self.data, self.writes, self.mode = bytearray(), [], mode

    @property
    def in_waiting(self):
        return len(self.data)

    def write(self, packet):
        self.writes.append(packet)
        body, crc = packet[1:-1].split(b"\x1a")
        assert binascii.crc_hqx(body, 0) == int(crc, 16)
        header = body.split(b"\x1e")[0]
        body = header + b"\x1e0\x1eslot0"
        frame = b"\x06" + body + f"\x1a{binascii.crc_hqx(body, 0):x}\x04".encode()
        if self.mode == "ok":
            self.data.extend(b"console\r\n" + frame.replace(b"\x1a", b"\x1aFFFF", 1) + frame)
        return len(packet)

    def read(self, size):
        data = bytes(self.data[:3])  # Deliberately fragmented response.
        del self.data[:3]
        return data


class LoaderTests(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "fw.hex"
            p.write_text(text)
            return read_hex(p)

    def part(self):
        return Partition("slot0", 0x80000, 512, 8, 255, 0x70080000, 256)

    def test_hex_extended_sparse_and_start(self):
        parts = self.parse(record(4, data=b"\x70\x08") + record(0, 3, b"abc") +
                           record(0, 7, b"def") + record(5, data=b"\x24\0\0\0") + record(1))
        image = prepare_image(parts, self.part())
        self.assertEqual(image[:12], b"\xff\xff\xffabc\xffdef\xff\xff")

    def test_segment_address(self):
        self.assertEqual(self.parse(record(2, data=b"\x12\x34") + record(0, 5, b"a") + record(1)),
                         [(0x12345, b"a")])

    def test_reject_corrupt_truncated_overlapping_hex(self):
        valid = record(0, 0, b"abcd")
        bad = [valid, valid[:-3] + "00\n" + record(1), valid + valid + record(1),
               record(1) + valid, record(4, data=b"\0") + record(1),
               record(0, 65535, b"ab") + record(1), record(6) + record(1), record(1)]
        for text in bad:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.parse(text)

    def test_bounds_and_explicit_relocation(self):
        with self.assertRaises(ValueError):
            prepare_image([(0x70200000, b"a")], self.part())
        with self.assertRaises(ValueError):
            prepare_image([(0x700801ff, b"ab")], self.part())
        self.assertEqual(prepare_image([(0x70200000, b"a")], self.part(), 0x70200000)[0], 97)

    def test_raw_default_for_all_partitions(self):
        for name in ("boot_partition", "slot0", "slot1"):
            part = Partition(name, 0, 512, 8, 255, 0x70080000, 256)
            image = prepare_image([(part.address, b"arbitrary image")], part)
            self.assertEqual(image[:15], b"arbitrary image")

    def test_legacy_board_type_does_not_select_validation(self):
        class Legacy(Board):
            def request(self, command, *args, **kwargs):
                return super().request(command, *args, **kwargs) + ["stm32n6"]
        part = Partition.query(Legacy(), "slot1")
        self.assertEqual(prepare_image([(part.address, b"raw")], part)[:3], b"raw")
        with self.assertRaises(ValueError):
            prepare_image([(part.address, b"raw")], part, image_type="stm32-v2.3")

    def test_cli_default_and_explicit_header_validation(self):
        board = Board()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "arbitrary.hex"
            path.write_text(record(4, data=b"\x70\x20") + record(0, 0, b"arbitrary") + record(1))
            args = [str(path), "--port", "test", "--partition", "slot1"]
            with patch("serial.Serial", return_value=MagicMock()), patch("fwloader.HuSerial", return_value=board):
                self.assertEqual(main(args), 0)
                self.assertIn(("erase", "slot1"), board.calls)
                for kind in ("mcuboot", "stm32-v2.3"):
                    board.calls.clear()
                    self.assertEqual(main(args + ["--image-type", kind]), 1)
                    self.assertEqual(board.calls, [("flash_info", "slot1")])

    def test_image_header(self):
        for kind in ("mcuboot", "stm32-v2.3"):
            with self.assertRaises(ValueError):
                prepare_image([(0x70080000, b"wrong")], self.part(), image_type=kind)
        app = bytearray(40)
        app[:4] = bytes.fromhex("3db8f396")
        app[8:10] = (32).to_bytes(2, "little")
        app[12:16] = (4).to_bytes(4, "little")
        app[36:40] = bytes.fromhex("07690400")
        prepare_image([(0x70080000, app)], self.part(), image_type="mcuboot")
        with self.assertRaises(ValueError):
            prepare_image([(0x70080000, app[:-1])], self.part(), image_type="mcuboot")
        boot = bytearray(164)
        boot[:4] = b"STM2"
        boot[0x68:0x6c] = (0x20300).to_bytes(4, "little")
        boot[0x6c:0x70] = (4).to_bytes(4, "little")
        prepare_image([(0x70080000, boot)], self.part(), image_type="stm32-v2.3")
        boot[-1] = 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            prepare_image([(0x70080000, boot)], self.part(), image_type="stm32-v2.3")

    def test_sequence_write_verify_protect(self):
        board, part = Board(), self.part()
        image = prepare_image([(part.address, bytes(range(255)))], part)
        load(board, part, image)
        self.assertEqual(board.memory, image)
        self.assertEqual([x[0] for x in board.calls],
                         ["wrprt", "erase", "flash", "flash_read", "flash_read", "wrprt"])
        self.assertEqual(board.calls[0][-1], "0")
        self.assertEqual(board.calls[-1][-1], "1")

    def test_unsupported_protection(self):
        for code in (-134, -95):
            board, part = Board(protection=False), self.part()
            board.unsupported = code
            load(board, part, bytes([255]) * part.size)
            self.assertEqual([x[0] for x in board.calls].count("wrprt"), 1)
            self.assertEqual(board.memory, bytes([255]) * part.size)

    def test_errors_restore_and_never_retry(self):
        for command in ("erase", "flash", "flash_read"):
            board, part = Board(failure=command), self.part()
            with self.subTest(command=command), self.assertRaises(TimeoutError):
                load(board, part, bytes(part.size))
            self.assertEqual(board.calls[-1], ("wrprt", "slot0", "1"))
            self.assertEqual([x[0] for x in board.calls].count(command), 1)

    def test_protection_failure_no_erase(self):
        class Denied(Board):
            def request(self, command, *args, **kwargs):
                self.calls.append((command, *args))
                raise HuError(command, -13)
        b = Denied()
        with self.assertRaisesRegex(RuntimeError, "restoration failed"):
            load(b, self.part(), bytes(512))
        self.assertTrue(all(c[0] == "wrprt" for c in b.calls))

    def test_unprotect_lost_ack_restores_without_erase(self):
        class Lost(Board):
            def request(self, command, *args, **kwargs):
                if command == "wrprt" and args[-1] == "0":
                    self.calls.append((command, *args))
                    raise TimeoutError("lost unprotect ACK")
                return super().request(command, *args, **kwargs)
        b = Lost()
        with self.assertRaises(TimeoutError):
            load(b, self.part(), bytes(512))
        self.assertEqual(b.calls, [("wrprt", "slot0", "0"), ("wrprt", "slot0", "1")])

    def test_reprotect_error_is_not_success(self):
        class ReprotectError(Board):
            def request(self, command, *args, **kwargs):
                if command == "wrprt" and args[-1] == "1":
                    raise HuError(command, -5)
                return super().request(command, *args, **kwargs)
        with self.assertRaisesRegex(RuntimeError, "restoration failed"):
            load(ReprotectError(), self.part(), bytes(512))

    def test_verify_mismatch(self):
        with self.assertRaisesRegex(ValueError, "mismatch"):
            verify(Board(), self.part(), bytes([255]) * 512)

    def test_inactive_pinned(self):
        p = Partition.query(Board(), "inactive")
        self.assertEqual(p.name, "slot1")
        with self.assertRaises(ValueError):
            Partition.query(Board(), "slot0")

    def test_serial_crc_fragment_and_sequence(self):
        port = SerialPort()
        self.assertEqual(HuSerial(port, device_id=19).request("erase", "slot0"), ["slot0"])
        self.assertIn(b"19@erase:1", port.writes[0])

    def test_serial_timeout_no_replay(self):
        port = SerialPort("drop")
        with self.assertRaises(TimeoutError):
            HuSerial(port, timeout=0.001).request("erase", "slot0")
        self.assertEqual(len(port.writes), 1)


if __name__ == "__main__":
    unittest.main()
