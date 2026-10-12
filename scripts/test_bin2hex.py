# Copyright (c) 2025 HU Inc. All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0

import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from bin2hex import convert_bin_to_hex, main
from fwloader import read_hex


class BinToHexTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.source = Path(self.folder.name) / "firmware.bin"
        self.output = Path(self.folder.name) / "firmware.hex"

    def test_known_records(self):
        self.source.write_bytes(bytes((1, 2, 3, 4)))
        self.assertEqual(convert_bin_to_hex(self.source, self.output), 4)
        self.assertEqual(self.output.read_bytes(),
                         b":020000040000FA\n:0400000001020304F2\n:00000001FF\n")

    def test_roundtrip_across_extended_address_boundary(self):
        data = bytes(range(256)) * 257 + b"tail"
        self.source.write_bytes(data)
        for address in (0, 0xfff8, 0x70000000):
            with self.subTest(address=address):
                convert_bin_to_hex(self.source, self.output, address)
                self.assertEqual(read_hex(self.output), [(address, data)])

    def test_last_32bit_address(self):
        self.source.write_bytes(b"x")
        convert_bin_to_hex(self.source, self.output, 0xffffffff)
        self.assertEqual(read_hex(self.output), [(0xffffffff, b"x")])

    def test_invalid_range_preserves_existing_output(self):
        self.source.write_bytes(b"ab")
        self.output.write_bytes(b"existing output")
        for address in (-1, 0x100000000, 0xffffffff):
            with self.subTest(address=address), self.assertRaises(ValueError):
                convert_bin_to_hex(self.source, self.output, address)
            self.assertEqual(self.output.read_bytes(), b"existing output")
        self.assertFalse(list(self.output.parent.glob(".bin2hex-*.tmp")))

    def test_empty_and_missing_input(self):
        with self.assertRaises(OSError):
            convert_bin_to_hex(self.source, self.output)
        self.source.write_bytes(b"")
        with self.assertRaises(ValueError):
            convert_bin_to_hex(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_same_file_and_hardlink_preserve_input(self):
        self.source.write_bytes(b"original")
        with self.assertRaises(ValueError):
            convert_bin_to_hex(self.source, self.source)
        self.output.hardlink_to(self.source)
        with self.assertRaises(ValueError):
            convert_bin_to_hex(self.source, self.output)
        self.assertEqual(self.source.read_bytes(), b"original")

    def test_cli_and_explicit_address(self):
        self.source.write_bytes(b"unmodified payload")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(self.source), str(self.output),
                                   "--address", "0x70000000"]), 0)
        self.assertEqual(read_hex(self.output), [(0x70000000, b"unmodified payload")])
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main([str(self.source), str(self.output),
                                   "--address", "-1"]), 1)


if __name__ == "__main__":
    unittest.main()
