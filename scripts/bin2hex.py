#!/usr/bin/env python3
# Copyright (c) 2025 HU Inc. All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0

"""Convert a BIN file to Intel HEX without changing its bytes."""

import argparse
import os
from pathlib import Path
import sys
import tempfile


def hex_record(kind, address=0, data=b""):
    record = bytes((len(data), address >> 8, address & 255, kind)) + data
    return ":" + (record + bytes((-sum(record) & 255,))).hex().upper() + "\n"


def convert_bin_to_hex(source, destination, address=0):
    source, destination = Path(source), Path(destination)
    if not 0 <= address <= 0xffffffff:
        raise ValueError("HEX address must be between 0 and 0xffffffff")
    if (source.resolve() == destination.resolve() or
            (destination.exists() and source.samefile(destination))):
        raise ValueError("Input and output must be different files")

    temporary = None
    try:
        with source.open("rb") as binary:
            size = os.fstat(binary.fileno()).st_size
            if not size:
                raise ValueError("Input BIN is empty")
            if address + size > 0x100000000:
                raise ValueError("BIN data exceeds the 32-bit Intel HEX address range")
            with tempfile.NamedTemporaryFile(mode="w", encoding="ascii", newline="\n",
                                             dir=destination.parent, prefix=".bin2hex-",
                                             suffix=".tmp", delete=False) as output:
                temporary = Path(output.name)
                upper, written = None, 0
                while data := binary.read(min(16, 0x10000 - (address & 65535))):
                    if written + len(data) > size:
                        raise ValueError("Input BIN changed during conversion")
                    if upper != address >> 16:
                        upper = address >> 16
                        output.write(hex_record(4, data=upper.to_bytes(2, "big")))
                    output.write(hex_record(0, address & 65535, data))
                    address += len(data)
                    written += len(data)
                if written != size:
                    raise ValueError("Input BIN changed during conversion")
                output.write(hex_record(1))
        os.replace(temporary, destination)
        temporary = None
        return size
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="input BIN file")
    parser.add_argument("output", type=Path, help="output Intel HEX file")
    parser.add_argument("--address", type=lambda value: int(value, 0), default=0,
                        help="first HEX data address (default: 0; hexadecimal or decimal)")
    args = parser.parse_args(argv)
    try:
        size = convert_bin_to_hex(args.input, args.output, args.address)
    except (OSError, ValueError) as error:
        print(f"bin2hex: {error}", file=sys.stderr)
        return 1
    print(f"{args.input} -> {args.output}: {size} bytes, HEX address=0x{args.address:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
