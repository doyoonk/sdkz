#!/usr/bin/env python3
# Copyright (c) 2025 HU Inc. All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0

"""Load Intel HEX firmware through HU serial commands; see fwloader.rst."""

import argparse
import base64
import binascii
import math
import sys
import time
from dataclasses import dataclass
from pathlib import Path

MAX_IMAGE_SIZE = 64 * 1024 * 1024


class HuError(RuntimeError):
    def __init__(self, command, status):
        self.status = status
        super().__init__(f"{command}: board returned {status}")


class HuSerial:
    """One request at a time. Never replay a timed-out erase/write command."""

    def __init__(self, port, timeout=5.0, device_id=None):
        self.port, self.timeout, self.device_id = port, timeout, device_id
        self.sequence = 0
        self.pending = bytearray()

    def request(self, command, *args, timeout=None):
        self.sequence += 1
        prefix = "" if self.device_id is None else f"{self.device_id}@"
        header = f"{prefix}{command}:{self.sequence}"
        body = "\x1e".join((header, *(str(x) for x in args))).encode("ascii")
        packet = b"\x05" + body + f"\x1a{binascii.crc_hqx(body, 0):04x}\x04".encode()
        if self.port.write(packet) != len(packet):
            raise OSError(f"Incomplete serial write for {command}; transfer stopped")
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            end = self.pending.find(b"\x04")
            if end < 0:
                self.pending.extend(self.port.read(max(1, self.port.in_waiting)))
                if len(self.pending) > 8192:
                    self.pending.clear()
                continue
            reply = bytes(self.pending[:end + 1])
            del self.pending[:end + 1]
            starts = [i for i in (reply.rfind(b"\x06"), reply.rfind(b"\x15")) if i >= 0]
            if not starts:
                continue
            reply = reply[max(starts):]
            try:
                data, crc = reply[1:-1].rsplit(b"\x1a", 1)
                if not 1 <= len(crc) <= 4 or any(c not in b"0123456789abcdefABCDEF" for c in crc):
                    continue
                if binascii.crc_hqx(data, 0) != int(crc, 16):
                    continue
                records = data.decode("ascii").split("\x1e")
                if records[0] != header:
                    continue
                status = int(records[1])
            except (ValueError, IndexError, UnicodeError):
                continue
            if reply[0] == 0x15 or status:
                raise HuError(command, status)
            return records[2:]
        raise TimeoutError(f"{command}: no valid response; not retried (board operation may have completed)")


def read_hex(path):
    """Validate the entire HEX file before any device mutation."""
    segments, base, eof, total = [], 0, False, 0
    with Path(path).open(encoding="ascii") as stream:
        for line_no, line in enumerate(stream, 1):
            line = line.strip()
            if not line:
                continue
            try:
                if eof or not line.startswith(":") or any(c not in "0123456789abcdefABCDEF" for c in line[1:]):
                    raise ValueError("invalid record or data after EOF")
                record = bytes.fromhex(line[1:])
                if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
                    raise ValueError("record length/checksum mismatch")
                count, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
                data = record[4:-1]
                if kind == 0:
                    absolute = base + address
                    if address + count > 0x10000 or absolute + count > 0x100000000:
                        raise ValueError("address overflow")
                    if count:
                        segments.append((absolute, data))
                        total += count
                        if total > MAX_IMAGE_SIZE:
                            raise ValueError("image exceeds 64 MiB")
                elif kind == 1 and count == 0 and address == 0:
                    eof = True
                elif kind in (2, 4) and count == 2 and address == 0:
                    base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
                elif kind in (3, 5) and count == 4 and address == 0:
                    pass  # Entry point metadata does not contain flash bytes.
                else:
                    raise ValueError("unsupported or malformed record type")
            except ValueError as error:
                raise ValueError(f"{path}:{line_no}: {error}") from error
    if not eof or not segments:
        raise ValueError("HEX must contain data and an EOF record")
    merged = []
    for address, data in sorted(segments):
        if merged and address < merged[-1][0] + len(merged[-1][1]):
            raise ValueError(f"Overlapping HEX data at 0x{address:x}")
        if merged and address == merged[-1][0] + len(merged[-1][1]):
            merged[-1][1].extend(data)
        else:
            merged.append((address, bytearray(data)))
    return [(address, bytes(data)) for address, data in merged]


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int
    size: int
    write_size: int
    erased: int
    address: int
    chunk_size: int

    @classmethod
    def query(cls, client, name):
        fields = client.request("flash_info", name)
        if len(fields) not in (7, 8):
            raise ValueError("Invalid flash_info response; updated firmware is required")
        # Older firmware appended an image type; only the user selects validation.
        p = cls(fields[0], *(int(x, 16) for x in fields[1:7]))
        if (p.name not in ("boot_partition", "slot0", "slot1") or
                not 0 < p.size <= MAX_IMAGE_SIZE or not 0 < p.write_size <= p.chunk_size <= 256 or
                p.size % p.write_size or not 0 <= p.erased <= 255 or
                not 0 <= p.offset <= 0x7fffffff or not 0 <= p.address < 0x100000000 or
                p.address + p.size > 0x100000000):
            raise ValueError("Invalid partition geometry")
        if name != "inactive" and p.name != name:
            raise ValueError("Partition response does not match requested target")
        return p


def prepare_image(segments, part, base=None, image_type="raw"):
    if image_type not in ("raw", "mcuboot", "stm32-v2.3"):
        raise ValueError(f"Unknown image type: {image_type}")
    origin = part.address if base is None else base
    if not 0 <= origin < 0x100000000 or origin + part.size > 0x100000000:
        raise ValueError("Invalid HEX base")
    image = bytearray([part.erased]) * part.size
    for address, data in segments:
        offset = address - origin
        if offset < 0 or offset + len(data) > part.size:
            raise ValueError(f"HEX range 0x{address:x}..0x{address+len(data):x} is outside "
                             f"{part.name} (HEX base 0x{origin:x}, size 0x{part.size:x})")
        image[offset:offset + len(data)] = data
    covered = len(segments[0][1]) if segments[0][0] == origin else 0
    def u16(offset):
        return int.from_bytes(image[offset:offset + 2], "little")
    def u32(offset):
        return int.from_bytes(image[offset:offset + 4], "little")
    if image_type == "stm32-v2.3":
        # STM32 Signing Tool header v2.3: fixed header (160 bytes), extensions,
        # then the payload. This is an integrity check, not signature validation.
        if covered < 160 or image[:4] != b"STM2" or u32(0x68) != 0x00020300:
            raise ValueError("Selected stm32-v2.3 format requires an STM32 v2.3 header")
        header, length = 160 + u32(0x88), u32(0x6c)
        if not length or header + length > covered:
            raise ValueError("Truncated STM32 header or payload")
        if sum(image[header:header + length]) & 0xffffffff != u32(0x64):
            raise ValueError("STM32 payload checksum mismatch")
    if image_type == "mcuboot":
        if covered < 32 or image[:4] != bytes.fromhex("3db8f396"):
            raise ValueError("Selected mcuboot format requires an MCUboot image header")
        header, protected, length = u16(8), u16(10), u32(12)
        end = header + length
        if header < 32 or not length or end + protected + 4 > covered:
            raise ValueError("Truncated MCUboot image")
        if protected and (u16(end) != 0x6908 or u16(end + 2) != protected):
            raise ValueError("Invalid protected MCUboot TLV")
        end += protected
        if u16(end) != 0x6907 or u16(end + 2) < 4 or end + u16(end + 2) > covered:
            raise ValueError("Missing or truncated MCUboot signature TLV")
    return bytes(image)


def verify(client, part, image, progress=None):
    for offset in range(0, part.size, part.chunk_size):
        data = image[offset:offset + part.chunk_size]
        reply = client.request("flash_read", part.name, f"{offset:x}", f"{len(data):x}")
        if (len(reply) != 4 or reply[0] != part.name or int(reply[1], 16) != offset or
                int(reply[2], 16) != len(data) or bytes.fromhex(reply[3]) != data):
            raise ValueError(f"Readback mismatch at {part.name}+0x{offset:x}")
        if progress:
            progress("Verify", offset + len(data), part.size)


def load(client, part, image, erase_timeout=120, progress=None):
    restore = True
    try:
        try:
            if client.request("wrprt", part.name, "0") != [part.name, "0"]:
                raise ValueError("Invalid wrprt response")
        except HuError as error:
            if error.status not in (-134, -95):  # Zephyr ENOTSUP / host EOPNOTSUPP.
                raise
            restore = False
            print("Hardware write protection is not supported by this flash driver.")
        if client.request("erase", part.name, timeout=erase_timeout) != [part.name]:
            raise ValueError("Invalid erase response")
        chunk = part.chunk_size // part.write_size * part.write_size
        for offset in range(0, part.size, chunk):
            data = image[offset:offset + chunk]
            if data != bytes([part.erased]) * len(data):
                args = (part.name, f"{offset:x}", f"{len(data):x}")
                reply = client.request("flash", *args, base64.a85encode(data).decode("ascii"))
                if reply != list(args):
                    raise ValueError(f"Invalid flash response at 0x{offset:x}")
            if progress:
                progress("Write", offset + len(data), part.size)
        verify(client, part, image, progress)
    finally:
        if restore:
            original = sys.exc_info()[1]
            try:
                if client.request("wrprt", part.name, "1") != [part.name, "1"]:
                    raise ValueError("Invalid wrprt response")
            except Exception as error:
                raise RuntimeError(f"{str(original) + '; ' if original else ''}"
                                   f"hardware protection restoration failed: {error}") from error


def integer(value):
    return int(value, 0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, help="Intel HEX file, normally zephyr.signed[.confirmed].hex")
    parser.add_argument("--port", "-p", required=True, help="HU serial port (e.g. COM3 or /dev/ttyACM0)")
    parser.add_argument("--partition", "-t", required=True,
                        choices=("slot0", "slot1", "inactive", "boot_partition", "boot", "mcuboot"))
    parser.add_argument("--baud", "-b", type=int, default=115200)
    parser.add_argument("--data-bits", type=int, choices=(5, 6, 7, 8), default=8)
    parser.add_argument("--parity", choices=("N", "E", "O", "M", "S"), default="N")
    parser.add_argument("--stop-bits", type=float, choices=(1, 1.5, 2), default=1)
    for option in ("rtscts", "dsrdtr", "xonxoff"):
        parser.add_argument("--" + option, action="store_true")
    parser.add_argument("--device-id", type=int)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--erase-timeout", type=float, default=120)
    parser.add_argument("--base", type=integer, help="HEX address corresponding to partition offset zero; explicit relocation")
    parser.add_argument("--image-type", choices=("raw", "mcuboot", "stm32-v2.3"), default="raw",
                        help="optional image header validation; raw (default) does not check image headers")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="validate HEX and query geometry without modifying flash")
    mode.add_argument("--verify-only", action="store_true", help="only compare existing flash against HEX, including erased gaps")
    parser.add_argument("--reboot", action="store_true", help="reboot only after successful download and protection restoration")
    args = parser.parse_args(argv)
    if (args.baud <= 0 or any(not math.isfinite(x) or x <= 0 for x in (args.timeout, args.erase_timeout)) or
            (args.device_id is not None and not 0 <= args.device_id <= 65535) or
            (args.reboot and (args.dry_run or args.verify_only))):
        parser.error("Invalid serial timing/device ID or incompatible --reboot option")
    try:
        segments = read_hex(args.file)
        import serial
        name = "boot_partition" if args.partition in ("boot", "mcuboot") else args.partition
        last = [0.0]

        def progress(phase, done, total):
            now = time.monotonic()
            if now - last[0] > 0.25 or done == total:
                print(f"\r{phase}: {done}/{total} bytes ({done/total:.1%})", end="\n" if done == total else "", flush=True)
                last[0] = now

        with serial.Serial(args.port, args.baud, bytesize=args.data_bits, parity=args.parity,
                           stopbits=args.stop_bits, rtscts=args.rtscts, dsrdtr=args.dsrdtr,
                           xonxoff=args.xonxoff, timeout=0.1, write_timeout=args.timeout) as port:
            port.reset_input_buffer()
            client = HuSerial(port, args.timeout, args.device_id)
            part = Partition.query(client, name)
            image = prepare_image(segments, part, args.base, args.image_type)
            print(f"{part.name}: flash address=0x{part.address:08x}, size=0x{part.size:x}, "
                  f"write unit={part.write_size}; HEX data={sum(len(d) for _, d in segments)} bytes")
            if args.dry_run:
                print("Validation passed; flash unchanged.")
                return 0
            if args.verify_only:
                verify(client, part, image, progress)
            else:
                load(client, part, image, args.erase_timeout, progress)
            print("Full partition readback verified.")
            if args.reboot:
                client.request("reboot")
                print("Reboot requested.")
        return 0
    except (OSError, ValueError, RuntimeError, ImportError) as error:
        print(f"fwloader: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("fwloader: interrupted; firmware may be incomplete", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
