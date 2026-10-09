# Copyright (c) 2026 HU Inc.
# SPDX-License-Identifier: Apache-2.0
"""Host protocol and PNG regression tests: python -m unittest discover -s sdk/scripts."""

import binascii
import socket
import struct
import tempfile
import threading
import unittest
import zlib
from pathlib import Path

from camera_capture import CameraImage, HuClient, download, rgb_pixels, write_png


class CameraCaptureTests(unittest.TestCase):
    def test_png_rgb565_primary_colors(self):
        # Red, green, blue and white in little-endian RGB565.
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "colors.png"
            write_png(path, 4, 1, bytes.fromhex("00f8e0071f00ffff"))
            png = path.read_bytes()
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        compressed = bytearray()
        while offset < len(png):
            size = struct.unpack_from(">I", png, offset)[0]
            kind = png[offset + 4:offset + 8]
            data = png[offset + 8:offset + 8 + size]
            checksum = struct.unpack_from(">I", png, offset + 8 + size)[0]
            self.assertEqual(checksum, zlib.crc32(kind + data))
            if kind == b"IDAT":
                compressed.extend(data)
            offset += 12 + size
        self.assertEqual(zlib.decompress(compressed),
                         b"\0\xff\0\0\0\xff\0\0\0\xff\xff\xff\xff")

    def test_udp_download_retries_crc_and_sequence(self):
        pixels = bytes.fromhex("00f8e0071f00ffff") * 256
        stop = threading.Event()
        released = threading.Event()
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(0.1)
        errors = []

        def serve():
            pending = True
            lost_reply = True
            try:
                while not stop.is_set():
                    try:
                        packet, peer = server.recvfrom(4096)
                    except socket.timeout:
                        continue
                    body, checksum = packet[1:-1].rsplit(b"\x1a", 1)
                    self.assertEqual(binascii.crc_hqx(body, 0), int(checksum, 16))
                    records = body.decode().split("\x1e")
                    header, sid = records[:2]
                    command = header.split(":")[0]
                    status = 0
                    if command == "cam_capture":
                        if pending:
                            pending = False
                            status, values = -11, []
                        else:
                            values = [sid, 32, 32, 64, len(pixels), "RGBP",
                                      f"{zlib.crc32(pixels):x}", 512]
                    elif command == "cam_read":
                        if lost_reply:
                            lost_reply = False
                            continue
                        offset, count = map(int, records[2:])
                        values = [sid, offset, pixels[offset:offset + count].hex()]
                    else:
                        self.assertEqual(command, "cam_release")
                        released.set()
                        values = [sid]
                    payload = "\x1e".join(map(str, [header, status, *values])).encode()
                    prefix = b"\x15" if status else b"\x06"
                    # Ignore both a corrupted reply and a valid reply for an old sequence.
                    server.sendto(prefix + payload + b"\x1aFFFF\x04", peer)
                    old = b"old:0\x1e0"
                    server.sendto(b"\x06" + old + b"\x1a" +
                                  f"{binascii.crc_hqx(old, 0):x}".encode() + b"\x04", peer)
                    server.sendto(prefix + payload + b"\x1a" +
                                  f"{binascii.crc_hqx(payload, 0):x}".encode() + b"\x04", peer)
            except Exception as exc:
                errors.append(exc)

        thread = threading.Thread(target=serve)
        thread.start()
        client = HuClient("127.0.0.1", server.getsockname()[1], timeout=0.1)
        try:
            result = download(client)
            self.assertEqual(result, CameraImage(32, 32, 64, "RGBP", pixels))
            self.assertTrue(released.is_set())
        finally:
            client.close()
            stop.set()
            thread.join(2)
            server.close()
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])

    def test_corrupt_snapshot_is_rejected_and_released(self):
        class CorruptCamera:
            released = False

            def request(self, command, sid, *args):
                if command == "cam_capture":
                    return [str(sid), "1", "1", "2", "2", "RGBP", "0", "512"]
                if command == "cam_read":
                    return [str(sid), "0", "ffff"]
                self.released = True
                return [str(sid)]

        camera = CorruptCamera()
        with self.assertRaisesRegex(ValueError, "CRC32"):
            download(camera)
        self.assertTrue(camera.released)

    def test_packed_rgb_channel_order_and_padding(self):
        formats = {"RGB3": "ff0000", "BGR3": "0000ff", "BA24": "7fff0000",
                   "RA24": "7f0000ff", "AB24": "ff00007f", "AR24": "0000ff7f",
                   "BX24": "7fff0000", "RX24": "7f0000ff", "XB24": "ff00007f",
                   "XR24": "0000ff7f", "RGBP": "00f8", "RGBR": "f800"}
        for fmt, value in formats.items():
            with self.subTest(fmt=fmt):
                row = bytes.fromhex(value) + b"\xaa\xbb"
                image = CameraImage(1, 2, len(row), fmt, row * 2)
                self.assertEqual(rgb_pixels(image), b"\xff\0\0" * 2)

    def test_yuv_formats_chroma_order(self):
        # BT.601 full range: Y=76, U=85, V=255 decodes red.
        rows = {"YUYV": [76, 85, 76, 255], "YVYU": [76, 255, 76, 85],
                "UYVY": [85, 76, 255, 76], "VYUY": [255, 76, 85, 76],
                "YUV3": [76, 85, 255] * 2, "XYUV": [0, 76, 85, 255] * 2}
        for fmt, row in rows.items():
            with self.subTest(fmt=fmt):
                self.assertEqual(rgb_pixels(CameraImage(2, 2, len(row), fmt, bytes(row) * 2)),
                                 bytes([254, 0, 0]) * 4)
        for fmt in ("NV12", "NV21", "NV16", "NV61", "NV24", "NV42", "YU12", "YV12"):
            with self.subTest(fmt=fmt):
                pairs = 4 if fmt in ("NV24", "NV42") else 2 if fmt in ("NV16", "NV61") else 1
                uv = [255, 85] if fmt in ("NV21", "NV61", "NV42", "YV12") else [85, 255]
                data = bytes([76] * 4 + uv * pairs) + b"\xaa" * 16
                self.assertEqual(rgb_pixels(CameraImage(2, 2, 16, fmt, data)), bytes([254, 0, 0]) * 4)

    def test_gray_and_bayer_storage(self):
        self.assertEqual(rgb_pixels(CameraImage(2, 1, 2, "GREY", b"\0\xff")), b"\0" * 3 + b"\xff" * 3)
        for depth in (10, 12, 14, 16):
            data = (0xAB << (depth - 8)).to_bytes(2, "little")
            self.assertEqual(rgb_pixels(CameraImage(1, 1, 2, f"Y{depth} ", data)), b"\xab" * 3)
        self.assertEqual(rgb_pixels(CameraImage(1, 1, 2, "Y8P2", b"\x0b\x0a")), b"\xab" * 3)
        for depth, data in ((10, b"\x11\x22\x33\x44\xff"),
                            (12, b"\x11\x22\xff"),
                            (14, b"\x11\x22\x33\x44\xff\xff\xff")):
            width = 2 if depth == 12 else 4
            self.assertEqual(rgb_pixels(CameraImage(width, 1, len(data), f"Y{depth}P", data)),
                             bytes(v for v in data[:width] for _ in range(3)))
        for fmt, pattern in {"BA81": "BGGR", "GBRG": "GBRG", "GRBG": "GRBG", "RGGB": "RGGB"}.items():
            data = bytes({"R": 240, "G": 120, "B": 30}[c] for c in pattern)
            self.assertEqual(rgb_pixels(CameraImage(2, 2, 2, fmt, data)), bytes([240, 120, 30]) * 4)

    def test_invalid_layout_is_rejected(self):
        for image in (CameraImage(2, 2, 4, "RGBP", b"\0"),
                      CameraImage(3, 2, 6, "NV12", b"\0" * 32),
                      CameraImage(3, 1, 6, "YUYV", b"\0" * 6)):
            with self.assertRaises(ValueError):
                rgb_pixels(image)


if __name__ == "__main__":
    unittest.main()
