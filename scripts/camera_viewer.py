#!/usr/bin/env python3
# Copyright (c) 2026 HU Inc.
# SPDX-License-Identifier: Apache-2.0
"""Display HU camera snapshots periodically on the PC."""

import argparse
import logging
import math
from pathlib import Path
import queue
import tempfile
import threading
import time
import tkinter as tk
from tkinter import ttk

from camera_capture import HuClient, configure, download, rgb_pixels, write_png

SYMBOL_COLORS = ("#39ff70", "#00d9ff", "#ffd54f", "#ff80df",
                 "#ff914d", "#b6a0ff", "#ffffff", "#9de34f")


def draw_symbols(canvas, recognition, scale, origin_x, origin_y):
    """Map original capture corners to the displayed image, including margins."""
    if not recognition or "error" in recognition:
        return
    for number, symbol in enumerate(recognition["symbols"], 1):
        color = SYMBOL_COLORS[(number - 1) % len(SYMBOL_COLORS)]
        points = [origin_x + v / scale if i % 2 == 0 else origin_y + v / scale
                  for i, v in enumerate(symbol["corners"])]
        # Two outlines stay visible against both dark and light symbol modules.
        canvas.create_polygon(*points, fill="", outline="black", width=5)
        canvas.create_polygon(*points, fill="", outline=color, width=2)
        left, top = min(points[::2]), min(points[1::2])
        label = canvas.create_text(left + 3, max(origin_y + 3, top - 22),
                                   text=f"{number}: {symbol['format']}", anchor="nw",
                                   fill=color, font=("Segoe UI", 11, "bold"))
        box = canvas.bbox(label)
        background = canvas.create_rectangle(box[0] - 2, box[1] - 1, box[2] + 2,
                                             box[3] + 1, fill="black", outline=color)
        canvas.tag_raise(label, background)


def capture_loop(args, events, stop):
    client = None

    def progress(done, total):
        if stop.is_set():
            raise RuntimeError("Viewer closed")

    def publish(event):
        # Keep the newest frame if the UI is temporarily busy.
        while True:
            try:
                events.put_nowait(event)
                return
            except queue.Full:
                try:
                    events.get_nowait()
                except queue.Empty:
                    pass

    try:
        client = HuClient(args.host, args.port, args.serial, args.baud)
        if args.format is not None:
            configure(client, args.format, args.width, args.height)
        number = 0
        while not stop.is_set():
            started = time.monotonic()
            try:
                frame = download(client, progress, recognize=not args.no_recognize)
                if frame.fourcc in ("JPEG", "PNG ", "H264", "AVC1"):
                    with tempfile.TemporaryDirectory(prefix="hu-camera-") as folder:
                        path = Path(folder) / "frame.png"
                        write_png(path, frame.width, frame.height, frame.data,
                                  frame.fourcc, frame.pitch)
                        pixels, encoding = path.read_bytes(), "png"
                else:
                    pixels = (f"P6\n{frame.width} {frame.height}\n255\n".encode("ascii")
                              + rgb_pixels(frame))
                    encoding = "PPM"
                if stop.is_set():
                    break
                number += 1
                elapsed = time.monotonic() - started
                status = (f"#{number}  {frame.fourcc!r} {frame.width}x{frame.height}"
                          f"  |  {time.strftime('%H:%M:%S')}  |  capture + decode {elapsed:.2f}s"
                          f"  |  interval {args.interval:g}s  |  CRC32 OK")
                publish((pixels, encoding, status, frame.recognition))
                logging.info(status)
            except (OSError, RuntimeError, ValueError) as error:
                if stop.is_set():
                    break
                publish((None, None, f"Capture failed: {error}. Retrying...", None))
                logging.warning("Capture failed: %s", error)
            # The interval is measured between capture starts. Never overlap requests.
            stop.wait(max(0, args.interval - (time.monotonic() - started)))
    except (OSError, RuntimeError, ValueError) as error:
        publish((None, None, str(error), None))
        logging.error("Camera setup failed: %s", error)
    finally:
        if client is not None:
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    link = parser.add_mutually_exclusive_group(required=True)
    link.add_argument("--host", help="Board IPv4 address (HU UDP)")
    link.add_argument("--serial", help="Board HU USB CDC port, not the ST-Link shell")
    parser.add_argument("--port", type=int, default=4241)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interval", type=float, default=2.0, help="Capture interval in seconds")
    parser.add_argument("--format", help="Optional FourCC or alias, with --width and --height")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--no-recognize", action="store_true", help="Only show camera images")
    args = parser.parse_args()
    if not math.isfinite(args.interval) or args.interval <= 0:
        parser.error("--interval must be a positive finite number")
    dimensions = (args.format, args.width, args.height)
    if any(value is not None for value in dimensions) and any(value is None for value in dimensions):
        parser.error("--format, --width, and --height must be specified together")

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    root = tk.Tk()
    root.title(f"HU Camera - {args.host or args.serial} - {args.interval:g}s")
    root.configure(background="#202020")
    picture = tk.Canvas(root, background="#202020", width=800, height=480,
                        highlightthickness=0)
    picture.image = None
    picture.recognition = None
    picture.scale = 1
    picture.pack(fill="both", expand=True)
    status = tk.StringVar(value="Connecting...")
    ttk.Label(root, textvariable=status, padding=8).pack(fill="x")
    results_panel = ttk.Frame(root)
    results_panel.pack(fill="x")
    decoded = tk.Text(results_panel, height=7, wrap="word", state="disabled")
    results_scroll = ttk.Scrollbar(results_panel, orient="vertical", command=decoded.yview)
    decoded.configure(yscrollcommand=results_scroll.set)
    results_scroll.pack(side="right", fill="y")
    decoded.pack(side="left", fill="both", expand=True)
    events = queue.Queue(maxsize=2)
    stop = threading.Event()
    worker = threading.Thread(target=capture_loop, args=(args, events, stop), daemon=True)

    def redraw(event=None):
        picture.delete("all")
        if picture.image is None:
            picture.create_text(20, 20, text="Waiting for camera...", fill="white", anchor="nw")
            return
        x = (picture.winfo_width() - picture.image.width()) / 2
        y = (picture.winfo_height() - picture.image.height()) / 2
        picture.create_image(x, y, image=picture.image, anchor="nw")
        draw_symbols(picture, picture.recognition, picture.scale, x, y)

    picture.bind("<Configure>", redraw)

    def refresh():
        try:
            while True:
                pixels, encoding, text, recognition = events.get_nowait()
                status.set(text)
                if pixels is not None:
                    image = tk.PhotoImage(data=pixels, format=encoding)
                    scale = max(1, math.ceil(image.width() / max(1, root.winfo_screenwidth() - 100)),
                                math.ceil(image.height() / max(1, root.winfo_screenheight() - 280)))
                    if scale > 1:
                        image = image.subsample(scale)
                    picture.configure(width=image.width(), height=image.height())
                    picture.image = image
                    picture.recognition = recognition
                    picture.scale = scale
                    redraw()
                if pixels is not None:
                    lines = []
                    if recognition is None:
                        lines.append("Board recognition disabled")
                    elif "error" in recognition:
                        lines.append("Board recognition unavailable: " + recognition["error"])
                    else:
                        lines.append(f"ZXC: {len(recognition['symbols'])} symbol(s), "
                                     f"{recognition['elapsed_ms']} ms, same captured frame")
                        if recognition["status"] == 1:
                            lines.append(f"Partial scan (capacity flags 0x{recognition['capacity_flags']:x})")
                        for number, symbol in enumerate(recognition["symbols"], 1):
                            value = bytes.fromhex(symbol["payload_hex"]).decode("utf-8", "backslashreplace")
                            value = "".join(ch if ch.isprintable() else f"\\x{ord(ch):02x}" for ch in value)
                            lines.append(f"{number}: {symbol['format']} [{symbol['identifier']}]: {value}")
                        if not recognition["symbols"]:
                            lines.append("No symbol detected")
                    decoded.configure(state="normal")
                    decoded.delete("1.0", "end")
                    decoded.insert("end", "\n".join(lines))
                    decoded.configure(state="disabled")
        except queue.Empty:
            pass
        root.after(50, refresh)

    def close():
        stop.set()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    worker.start()
    root.after(50, refresh)
    try:
        root.mainloop()
    finally:
        stop.set()
        worker.join(timeout=8)


if __name__ == "__main__":
    main()
