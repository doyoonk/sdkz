#!/usr/bin/env python3
# Copyright (c) 2026 HU Inc.
# SPDX-License-Identifier: Apache-2.0
"""Download native camera frames over HU; optionally convert to PNG on the PC."""

import argparse
import binascii
import secrets
import json
import subprocess
import shutil
import socket
import struct
import time
import zlib
from pathlib import Path
from dataclasses import dataclass


@dataclass
class CameraImage:
    width: int
    height: int
    pitch: int
    fourcc: str
    data: bytes


ALIASES = {
    "RGB565_LE": "RGBP", "RGB565": "RGBP", "RGB565_BE": "RGBR",
    "RGB888": "RGB3", "BGR888": "BGR3", "ARGB8888": "BA24",
    "ABGR8888": "RA24", "RGBA8888": "AB24", "BGRA8888": "AR24",
    "YUV420": "YU12", "YVU420": "YV12",
}


class HuError(RuntimeError):
    def __init__(self, status):
        super().__init__(f"Board returned HU error {status}")
        self.status = status


class HuClient:
    def __init__(self, host=None, port=4241, serial_port=None, baud=115200, timeout=2.0):
        self.timeout = timeout
        self.sequence = 0
        self.pending = bytearray()
        self.serial = None
        self.sock = None
        if serial_port:
            import serial  # Only required for USB CDC: pip install pyserial

            self.serial = serial.Serial(serial_port, baud, timeout=0.1, write_timeout=timeout)
            self.serial.reset_input_buffer()
        else:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.connect((host, port))

    def close(self):
        (self.serial or self.sock).close()

    def receive(self, deadline):
        while time.monotonic() < deadline:
            if self.sock:
                self.sock.settimeout(max(0.001, deadline - time.monotonic()))
                return self.sock.recv(4096)
            end = self.pending.find(b"\x04")
            if end >= 0:
                packet = bytes(self.pending[:end + 1])
                del self.pending[:end + 1]
                return packet
            self.pending.extend(self.serial.read(max(1, self.serial.in_waiting)))
            if len(self.pending) > 8192:
                self.pending.clear()
        raise TimeoutError("HU response timed out")

    def request(self, command, *args):
        self.sequence += 1
        header = f"{command}:{self.sequence}".encode("ascii")
        payload = b"\x1e".join([header, *(str(arg).encode("ascii") for arg in args)])
        checksum = f"{binascii.crc_hqx(payload, 0):x}".encode("ascii")
        packet = b"\x05" + payload + b"\x1a" + checksum + b"\x04"
        # The same sequence and snapshot ID make retries safe after a lost response.
        for _ in range(3):
            if self.sock:
                self.sock.send(packet)
            else:
                self.serial.write(packet)
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                try:
                    reply = self.receive(deadline)
                except (TimeoutError, socket.timeout):
                    break
                starts = [pos for pos in (reply.find(b"\x06"), reply.find(b"\x15")) if pos >= 0]
                if not starts:
                    continue
                reply = reply[min(starts):]
                if not reply.endswith(b"\x04") or b"\x1a" not in reply:
                    continue
                body, check = reply[1:-1].rsplit(b"\x1a", 1)
                try:
                    if binascii.crc_hqx(body, 0) != int(check, 16):
                        continue
                    records = body.decode("ascii").split("\x1e")
                    if records[0].encode("ascii") != header:
                        continue
                    status = int(records[1])
                except (ValueError, IndexError, UnicodeError):
                    continue
                if reply[0] == 0x15 or status != 0:
                    raise HuError(status)
                return records[2:]
        raise TimeoutError(f"No valid response to {command} after three attempts")


def download(client, progress=None):
    snapshot_id = secrets.randbelow(0x7FFFFFFF) + 1
    # A lost initial response can still have reserved a snapshot on the board.
    owned = True
    try:
        deadline = time.monotonic() + 10
        while True:
            try:
                metadata = client.request("cam_capture", snapshot_id)
                owned = True
                break
            except HuError as exc:
                if exc.status != -11:  # -EAGAIN: waiting for the next captured frame
                    raise
                owned = True
                if time.monotonic() >= deadline:
                    raise TimeoutError("Camera did not produce a frame") from exc
                time.sleep(0.05)
        if len(metadata) != 8:
            raise ValueError("Invalid camera metadata")
        sid, width, height, pitch, size = map(int, metadata[:5])
        pixel_format, checksum, chunk_size = metadata[5:]
        chunk_size = int(chunk_size)
        if (sid != snapshot_id or len(pixel_format) != 4 or
                not 0 < width <= 8192 or not 0 < height <= 8192 or
                not 0 <= pitch <= 1024 * 1024 or not 0 < size <= 32 * 1024 * 1024 or
                not 0 < chunk_size <= 512):
            raise ValueError("Unsupported camera format or dimensions")
        image = bytearray()
        while len(image) < size:
            offset = len(image)
            count = min(chunk_size, size - offset)
            records = client.request("cam_read", snapshot_id, offset, count)
            if len(records) != 3 or int(records[0]) != snapshot_id or int(records[1]) != offset:
                raise ValueError("Snapshot ID or chunk offset mismatch")
            chunk = bytes.fromhex(records[2])
            if len(chunk) != count:
                raise ValueError("Camera chunk length mismatch")
            image.extend(chunk)
            if progress:
                progress(len(image), size)
        if zlib.crc32(image) != int(checksum, 16):
            raise ValueError("Snapshot CRC32 mismatch")
        return CameraImage(width, height, pitch, pixel_format, bytes(image))
    finally:
        if owned:
            try:
                client.request("cam_release", snapshot_id)
            except (OSError, RuntimeError):
                pass  # The board also releases abandoned snapshots after 30 s idle.


def configure(client, pixel_format, width, height):
    token = secrets.randbelow(0x7FFFFFFF) + 1
    fourcc = ALIASES.get(pixel_format, pixel_format)
    deadline = time.monotonic() + 10
    while True:
        try:
            result = client.request("cam_config", token, fourcc, width, height)
            if len(result) != 6 or int(result[0]) != token:
                raise ValueError("Invalid camera configuration reply")
            print(f"Camera: {result[1]} {result[2]}x{result[3]}, pitch={result[4]}")
            return result
        except HuError as exc:
            if exc.status != -11 or time.monotonic() >= deadline:
                raise
            time.sleep(0.05)


def list_formats(client):
    index = 0
    while True:
        fields = client.request("cam_caps", index)
        if len(fields) != 12 or int(fields[0]) != index:
            raise ValueError("Invalid camera capabilities reply")
        print(f"{fields[2]!r}: width {fields[3]}..{fields[4]} step {fields[5]}, "
              f"height {fields[6]}..{fields[7]} step {fields[8]}")
        index += 1
        if index >= int(fields[1]):
            print(f"Application limit: {fields[9]}x{fields[10]}, "
                  f"{fields[11]} bytes/frame (format-dependent)")
            return


def rgb_pixels(image):
    """Decode Zephyr packed RGB, gray, and YUV; YUV is BT.601 full range.

    N6 planar layouts use width-byte Y rows and contiguous chroma planes;
    the driver's pitch is the aggregate bytes per image row, not Y stride.
    """
    w, h, pitch, fmt, data = image.width, image.height, image.pitch, image.fourcc, image.data
    packed = {
        "RGB3": (3, 0, 1, 2), "BGR3": (3, 2, 1, 0),
        "BA24": (4, 1, 2, 3), "RA24": (4, 3, 2, 1),
        "AB24": (4, 0, 1, 2), "AR24": (4, 2, 1, 0),
        "BX24": (4, 1, 2, 3), "RX24": (4, 3, 2, 1),
        "XB24": (4, 0, 1, 2), "XR24": (4, 2, 1, 0),
    }
    planar = fmt in ("NV12", "NV21", "NV16", "NV61", "NV24", "NV42", "YU12", "YV12")
    full = fmt in ("YU12", "YV12")
    sub_y = 2 if fmt in ("NV12", "NV21", "YU12", "YV12") else 1
    sub_x = 1 if fmt in ("NV24", "NV42") else 2
    if planar:
        if w % sub_x or h % sub_y:
            raise ValueError("Invalid planar chroma dimensions")
        required = w * h + (w // sub_x) * (h // sub_y) * 2
    else:
        bpp = packed[fmt][0] if fmt in packed else {
            "RGBP": 2, "RGBR": 2, "GREY": 1, "YUYV": 2, "YVYU": 2,
            "UYVY": 2, "VYUY": 2, "YUV3": 3, "XYUV": 4,
        }.get(fmt)
        if bpp is None:
            return decode_other_raw(image)
        if pitch < w * bpp:
            raise ValueError("Invalid pixel stride")
        if fmt in ("YUYV", "YVYU", "UYVY", "VYUY") and w % 2:
            raise ValueError("Packed 4:2:2 requires even width")
        required = pitch * h
    if len(data) < required:
        raise ValueError("Truncated image")
    rgb = bytearray(w * h * 3)
    for y in range(h):
        row = y * pitch
        for x in range(w):
            if fmt in packed:
                bpp, ro, go, bo = packed[fmt]
                i = row + x * bpp
                r, g, b = data[i + ro], data[i + go], data[i + bo]
            elif fmt in ("RGBP", "RGBR"):
                i = row + x * 2
                value = int.from_bytes(data[i:i + 2], "little" if fmt == "RGBP" else "big")
                r, g, b = value >> 11, (value >> 5) & 63, value & 31
                r, g, b = (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)
            elif fmt == "GREY":
                r = g = b = data[row + x]
            else:
                if planar:
                    ys = w * h
                    cy, cx = y // sub_y, x // sub_x
                    if full:
                        ci = cy * (w // 2) + cx
                        u, v = data[ys + ci], data[ys + ys // 4 + ci]
                    else:
                        ci = (cy * (w // sub_x) + cx) * 2
                        u, v = data[ys + ci], data[ys + ci + 1]
                    if fmt in ("NV21", "NV61", "NV42", "YV12"):
                        u, v = v, u
                    luma = data[y * w + x]
                elif fmt in ("YUV3", "XYUV"):
                    i = row + x * (3 if fmt == "YUV3" else 4)
                    if fmt == "XYUV":
                        i += 1
                    luma, u, v = data[i:i + 3]
                else:
                    pair = data[row + (x // 2) * 4:row + (x // 2) * 4 + 4]
                    if fmt in ("YUYV", "YVYU"):
                        luma, u, v = pair[(x % 2) * 2], pair[1], pair[3]
                        if fmt == "YVYU": u, v = v, u
                    else:
                        luma, u, v = pair[(x % 2) * 2 + 1], pair[0], pair[2]
                        if fmt == "VYUY": u, v = v, u
                u, v = u - 128, v - 128
                r = max(0, min(255, luma + ((359 * v + 128) >> 8)))
                g = max(0, min(255, luma - ((88 * u + 183 * v + 128) >> 8)))
                b = max(0, min(255, luma + ((454 * u + 128) >> 8)))
            i = (y * w + x) * 3
            rgb[i:i + 3] = bytes((r, g, b))
    return bytes(rgb)


def decode_other_raw(image):
    """Decode Zephyr gray/Bayer storage, with bilinear Bayer interpolation.

    Preview scales sensor codes to eight bits; it does not apply sensor-specific
    white balance, black-level correction, or gamma. Native data remains intact.
    """
    fmt = image.fourcc
    pattern = None
    storage = "word"
    depth = 8
    if fmt in ("Y10 ", "Y12 ", "Y14 ", "Y16 "):
        depth = int(fmt[1:3])
    elif fmt in ("Y10P", "Y12P", "Y14P"):
        depth, storage = int(fmt[1:3]), "mipi"
    elif fmt == "Y8P2":
        storage = "nibbles"
    elif fmt in ("BA81", "GBRG", "GRBG", "RGGB"):
        pattern = {"BA81": "BGGR", "GBRG": "GBRG", "GRBG": "GRBG", "RGGB": "RGGB"}[fmt]
        storage = "byte"
    elif fmt.startswith("p") and fmt[1:2] in ("B", "G", "g", "R") and fmt[2:] in ("82", "AA", "CC", "EE"):
        pattern = {"B": "BGGR", "G": "GBRG", "g": "GRBG", "R": "RGGB"}[fmt[1]]
        depth = {"82": 8, "AA": 10, "CC": 12, "EE": 14}[fmt[2:]]
        storage = "nibbles" if depth == 8 else "mipi"
    elif fmt == "BYR2":
        pattern, depth = "BGGR", 16
    elif fmt[:2] in ("BG", "GB", "BA", "GR", "RG") and fmt[2:] in ("10", "12", "14", "16"):
        pattern = {"BG": "BGGR", "GB": "GBRG", "BA": "GRBG", "GR": "GRBG", "RG": "RGGB"}[fmt[:2]]
        depth = int(fmt[2:])
    else:
        raise ValueError(f"No built-in PNG decoder for {fmt!r}; native raw capture is preserved")

    w, h, pitch, data = image.width, image.height, image.pitch, image.data
    group = 2 if depth == 12 else 4
    minimum = (w * depth + 7) // 8 if storage == "mipi" else w * (1 if storage == "byte" else 2)
    if pitch < minimum or len(data) < pitch * h:
        raise ValueError("Invalid or truncated raw pixel rows")
    if storage == "mipi" and w % group:
        raise ValueError("MIPI packed rows require complete pixel groups")
    samples = bytearray(w * h)
    for y in range(h):
        row = data[y * pitch:(y + 1) * pitch]
        for x in range(w):
            if storage == "mipi":
                # Each group's first bytes contain the upper eight bits.
                value = row[(x // group) * (group * depth // 8) + x % group]
            elif storage == "byte":
                value = row[x]
            elif storage == "nibbles":
                value = (row[x * 2] & 15) | ((row[x * 2 + 1] & 15) << 4)
            else:
                value = (int.from_bytes(row[x * 2:x * 2 + 2], "little") & ((1 << depth) - 1)) >> (depth - 8)
            samples[y * w + x] = value
    if pattern is None:
        return bytes(value for sample in samples for value in (sample, sample, sample))
    rgb = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            native = pattern[(y % 2) * 2 + x % 2]
            for channel, color in enumerate("RGB"):
                if color == native:
                    value = samples[y * w + x]
                else:
                    adjacent = [samples[ny * w + nx]
                                for ny in range(max(0, y - 1), min(h, y + 2))
                                for nx in range(max(0, x - 1), min(w, x + 2))
                                if pattern[(ny % 2) * 2 + nx % 2] == color]
                    value = sum(adjacent) // len(adjacent) if adjacent else samples[y * w + x]
                rgb[(y * w + x) * 3 + channel] = value
    return bytes(rgb)


def write_png(path, width, height, data, fourcc="RGBP", pitch=None):
    """Convert only on the PC; captured data is never modified on the board."""
    if pitch is None:
        pitch = width * 2
    if fourcc in ("JPEG", "PNG ", "H264", "AVC1"):
        ffmpeg = shutil.which("ffmpeg")
        if not ffmpeg:
            raise ValueError("Encoded image conversion requires ffmpeg on PATH; raw capture is preserved")
        if fourcc == "AVC1":
            annexb = bytearray()
            pos = 0
            while pos < len(data):
                if pos + 4 > len(data): raise ValueError("Truncated AVC1 length")
                size = int.from_bytes(data[pos:pos + 4], "big")
                pos += 4
                if not size or pos + size > len(data): raise ValueError("Invalid AVC1 NAL size")
                annexb.extend(b"\0\0\0\1" + data[pos:pos + size])
                pos += size
            data = bytes(annexb)
        input_format = {"JPEG": "mjpeg", "PNG ": "png_pipe", "H264": "h264", "AVC1": "h264"}[fourcc]
        result = subprocess.run([ffmpeg, "-v", "error", "-f", input_format, "-i", "pipe:0",
                                 "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "pipe:1"],
                                input=data, capture_output=True, timeout=30)
        if result.returncode or not result.stdout.startswith(b"\x89PNG"):
            raise ValueError("Encoded frame needs a decodable keyframe and codec headers: " +
                             result.stderr.decode(errors="replace"))
        Path(path).write_bytes(result.stdout)
        return
    rgb = rgb_pixels(CameraImage(width, height, pitch, fourcc, data))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload)))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows))
    png += chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    link = parser.add_mutually_exclusive_group(required=True)
    link.add_argument("--host", help="Board IPv4 address (HU UDP)")
    link.add_argument("--serial", help="Board HU USB CDC port, not the ST-Link shell port")
    parser.add_argument("--port", type=int, default=4241)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path, default=Path("capture.raw"))
    parser.add_argument("--raw-output", type=Path, help="Also save native bytes when --output is PNG")
    parser.add_argument("--format", help="FourCC or alias from --list-formats")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--list-formats", action="store_true", help="List formats reported by the camera driver")
    args = parser.parse_args()
    client = HuClient(args.host, args.port, args.serial, args.baud)
    last_percent = -1

    def progress(done, total):
        nonlocal last_percent
        percent = done * 100 // total
        if percent != last_percent:
            print(f"\rDownloading: {percent:3d}%", end="", flush=True)
            last_percent = percent

    try:
        if args.list_formats:
            list_formats(client)
            return
        if any(v is not None for v in (args.format, args.width, args.height)):
            if any(v is None for v in (args.format, args.width, args.height)):
                parser.error("--format, --width, and --height must be specified together")
            configure(client, args.format, args.width, args.height)
        image = download(client, progress)
        raw_path = args.raw_output or (args.output.with_suffix(".raw") if args.output.suffix.lower() == ".png" else args.output)
        raw_path.write_bytes(image.data)
        raw_path.with_suffix(raw_path.suffix + ".json").write_text(json.dumps({
            "fourcc": image.fourcc, "width": image.width, "height": image.height,
            "pitch": image.pitch, "size": len(image.data),
            "crc32": f"{zlib.crc32(image.data):08x}", "yuv_matrix": "BT601_FULL",
            "planar_layout": "STM32_DCMIPP_CONTIGUOUS",
        }, indent=2) + "\n")
        if args.output.suffix.lower() == ".png":
            write_png(args.output, image.width, image.height, image.data, image.fourcc, image.pitch)
        print(f"\nSaved {image.fourcc!r} {image.width}x{image.height}: {args.output} (CRC32 verified)")
    finally:
        client.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error)) from error
