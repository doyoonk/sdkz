Camera display and native HU image transfer
==========================================

STM32N6570-DK with the st_b_cams_imx_mb1854 shield uses the IMX335 RAW10 sensor.
The pinned zephyr-stm32-mw-isp west module provides exposure, white balance,
black level, demosaicing, color correction and gamma configuration. Run
``west update zephyr-stm32-mw-isp`` when setting up an existing workspace.

``camera.c`` owns capture, display and HU commands. The board transfers the
original bytes reported by the video driver, including row padding and planes.
It does not convert the snapshot or encode PNG. LCD-only conversions use a
separate RGB565 buffer. DMA cache invalidation and completion of LVGL rendering
precede returning the frame to the driver. The camera thread owns LVGL updates.

PC commands
-----------

Use the board's actual IPv4 address; HU UDP defaults to port 4241::

    python sdk/scripts/camera_capture.py --host 192.168.1.253 --list-formats
    python sdk/scripts/camera_capture.py --host 192.168.1.253 --output capture.raw
    python sdk/scripts/camera_capture.py --host 192.168.1.253 --format NV12 --width 640 --height 480 --output capture.png

The default is ``capture.raw``. Every download saves original bytes and a
``.raw.json`` sidecar containing FourCC, dimensions, pitch, size and CRC32.
Choosing ``--output capture.png`` additionally converts on the PC, preserving
``capture.raw`` first. ``--raw-output`` overrides the native-file path.
``--format``, ``--width`` and ``--height`` must be supplied together. FourCCs are
case-sensitive; aliases such as RGB565, RGB888 and ARGB8888 are also accepted.

For USB, install pyserial and use ``--serial COM20`` instead of ``--host``.
Select the board's HU USB CDC port, not the ST-Link shell port.

Board barcode recognition (ZXC)
-------------------------------

STM32N6570-DK enables ``CONFIG_ZXC=y`` and ``CONFIG_APP_CAMERA_BARCODE=y``.
The board scans the camera in a separate, lower-priority thread and shows
the symbology and a printable payload preview at the bottom of the LCD.
Default searches include ZXC's readable 1D and 2D families: linear barcodes,
QR/Micro QR/rMQR, Data Matrix, Aztec, PDF417/MicroPDF417 and MaxiCode.
OPSPro is not included in ZXC's default search.

Every two seconds a frame is sampled for live recognition. A HU snapshot
request also queues that exact source frame, ahead of a pending live scan.
Two independent grayscale buffers keep decoder input immutable while DMA
capture, LCD preview and native snapshot transfer continue. Both grayscale
buffers use ``shared_multi_heap_aligned_alloc`` with the video's memory attribute.
STM32N6570-DK enables ``CONFIG_ZXC_USE_HEAP=y`` (heap1). When
``CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP=y``, the app defaults ZXC's allocator
to CUSTOM and supplies the four hooks in ``sdk/app/src/scanner_zxc.c``. All ZXC
allocations use ``CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE`` through
``shared_multi_heap_alloc``, ``shared_multi_heap_realloc`` and
``shared_multi_heap_free``. Calloc allocates from the same heap and zeroes the
requested bytes. There is no extra allocation header or forced 32-byte
alignment; realloc uses Zephyr's implementation and retains the original block
on failure. This selection is based on the
shared-heap configuration, not the board name. Without it, ZXC retains its
default libc allocator. No native camera data is modified.

The default recognition limit is 800x480. Larger frames use integer-factor
sampling while retaining the original capture resolution and raw payload.
``APP_CAMERA_BARCODE_WIDTH/HEIGHT`` change the recognition limits;
``APP_CAMERA_BARCODE_INTERVAL_MS`` changes the live scan interval. Eight
symbols and a 16 KiB payload arena are reserved for LCD/HU results. Heap1 output
is copied within these limits before it is released; incomplete symbols are
omitted and truncation reports PARTIAL with OUTPUT capacity flags.
ZXC PARTIAL status and capacity
flags are exposed instead of treating a capacity shortage as an empty success.
Heap1 grows its internal buffers as needed, bounded by available shared memory;
it has no fixed 6 MiB workspace cap. Allocation failures report ENOMEM.
Heap0 remains supported with a default 6 MiB workspace cap.
The decoder stack defaults to 32 KiB. STM32N6570-DK configures it to 8 KiB
and the main thread stack to 1 KiB.

All 16 N6 camera output formats have grayscale input adapters. YUV uses its
native Y channel; RGB uses luminance conversion. Other formats such as encoded
JPEG or unprocessed Bayer remain transferable but board recognition returns
ENOTSUP. CPU time depends on the image and reader; two seconds is a sampling
interval, not a guaranteed worst-case recognition deadline.

The PC viewer displays results from the same snapshot ID as the image, with
colored quadrilateral outlines and numbered format labels at each symbol.
The same numbers identify payloads in the scrollable result panel. Overlays
follow image scaling and centering and are cleared with each new frame.
The LCD shows numbered outlines from the latest completed scan with its age,
and at most three payload previews. LCD coordinates include the image's
contain/letterbox scaling; outlines disappear after a format-size change or
when the result becomes stale. Since LCD video continues between scans,
moving objects can move away from a previous scan's outline until the next scan.
LCD text replaces non-ASCII bytes with dots. PC text attempts UTF-8 and escapes
control/invalid bytes; it does not interpret ECI charset changes. The complete
recognized raw bytes are retained as hexadecimal in the PC results, including
GS1 separators and binary payloads. Recognition itself does not open decoded
URLs or execute their contents.

For one capture and its results in the raw sidecar JSON::

   python sdk/scripts/camera_capture.py --host 192.168.1.253 --output capture.png --recognize

HU ``cam_decode <snapshot-id> <generation> <index> <offset>`` retrieves results:

* ``cam_decode <id> 0 0 0`` returns id, generation, symbol count, ZXC status,
  hexadecimal capacity flags, source width/height, scan width/height,
  scan duration and frame age in milliseconds. Pending decoding returns EAGAIN.
* Index 1 through count returns id, generation, index, format name, symbology
  identifier, total payload length, byte offset, up to 256 payload bytes in hex,
  four x/y corners in original image coordinates, and the has-ECI flag.
  Supply the generation from the summary. Increment offset until the full
  payload has been read. A changed generation returns ESTALE.
* ID 0 accesses the latest live result. Positive IDs select the last requested
  snapshot's result, which is retained across subsequent periodic scans.

Live PC viewer
--------------

Display a new snapshot every two seconds without changing the camera format
or dimensions::

   python sdk/scripts/camera_viewer.py --host 192.168.1.253 --interval 2

The viewer uses Tkinter (install ``python3-tk`` on Ubuntu if needed). It reuses
the capture tool's native transfer, CRC32 verification and PC color decoders.
The viewer also queries and displays board ZXC results for each captured image;
``--no-recognize`` disables this query for older firmware. No image files are
retained. Close the window to stop capture. The last image
remains visible during a connection failure, with retries shown in the status
line. Encoded formats require ffmpeg, as for the single-image capture tool.

``--serial``, ``--port``, ``--baud`` and the combined ``--format --width --height``
options work as in ``camera_capture.py``. Capture starts are spaced by
``--interval`` seconds; if transfer and decoding take longer, the next capture
starts after completion without overlapping snapshots.

Supported formats and dimensions
--------------------------------

The HU transport accepts any FourCC advertised by the camera driver; declaring
a format in Zephyr's API does not mean every camera can produce it.
``--list-formats`` queries the actual driver's width/height ranges and steps.
This N6 DCMIPP main pipe advertises RGBP, RGB3, BGR3, BA24, RA24, AB24, AR24,
GREY, YUYV, YVYU, NV12, NV21, NV16, NV61, YU12 and YV12. All have LCD previews
and PC PNG decoders. Other driver formats remain transferable even without an
LCD decoder. Preview images fit within the LCD while preserving aspect ratio.

Initial dimensions use VIDEO_WIDTH/VIDEO_HEIGHT (800x480), with RGBP/RGB565 as
APP_CAMERA_PIXEL_FORMAT. Runtime requests must fit APP_CAMERA_MAX_WIDTH/HEIGHT
(1920x1080 on N6), the driver's alignment requirements, and
APP_CAMERA_FRAME_CAPACITY (4 MiB). RGB565 1920x1080 fits; 32-bit 1920x1080 does
not. YUV420 requires even width and height. Increase limits only with a matching
memory budget; three DMA buffers and one snapshot each reserve frame capacity.

The PC decoder also handles Zephyr's other packed RGB/YUV, gray and Bayer raw
formats, including padded and MIPI-packed storage. Bayer previews use bilinear
interpolation without sensor-specific ISP tuning. High-bit-depth PNG previews
use the upper eight bits; the raw file preserves all captured bits.
JPEG/PNG/H264/AVC1 conversion requires ffmpeg on PATH. An H264 snapshot must
contain the headers and reference information needed to decode a frame; an
arbitrary inter-frame packet is not independently decodable.

YUV conversion uses DCMIPP's BT.601 full-range matrix. N6 planar Y rows have
``width`` bytes followed by contiguous chroma planes; its reported ``pitch``
is the aggregate row size, not the Y-plane stride. This PC planar decoder is
specific to that layout. A different driver with different plane strides needs
a corresponding PC decoder. Native transfer itself does not interpret planes.

STM32N6570-DK memory
-------------------

SHARED_MULTI_HEAP and VIDEO_BUFFER_USE_SHARED_MULTI_HEAP are enabled, with
VIDEO_BUFFER_SMH_ATTRIBUTE=2 (SMH_REG_ATTR_EXTERNAL). Zephyr's
``video_buffer_aligned_alloc`` therefore calls ``shared_multi_heap_aligned_alloc``.
The snapshot and LCD conversion buffer call it directly. Allocations are
32-byte aligned. No static video pool overlaps the shared heap.

The board overlay splits external PSRAM:

* 0x90000000..0x91DFFFFF: 30 MiB PSRAM shared heap, registered by the PSRAM driver.
* 0x91E00000..0x91FFFFDF: PSRAM_DISPLAY for static LVGL/LTDC buffers.
  RGB565 800x480 double buffers use 1,536,000 bytes.
* The last 32 bytes remain excluded for STM32N6 erratum ES0620, section 2.4.5.

The configured camera allocation is approximately 20 MiB plus heap metadata:
three 4 MiB DMA buffers, one 4 MiB snapshot, and a 1920x1080 RGB565 conversion
buffer. The application still executes from internal SRAM at 0x24000000.

HU protocol
-----------

Numeric arguments are decimal. IDs are client-generated positive integers up
to INT_MAX, reused on retries. Command records are separated by 0x1e. Packets
start with ENQ (0x05), end with 0x04, and include the standard HU CRC16.
Responses start with ACK (0x06) or NAK (0x15), followed by the echoed header and
status. ID/sequence headers are limited to 16 characters each.

* ``cam_caps <index>`` returns ``index, count, FourCC, width_min, width_max,
  width_step, height_min, height_max, height_step, app_max_width,
  app_max_height, frame_capacity``. Query indices 0 through count-1.
* ``cam_config <id> <FourCC> <width> <height>`` returns -EAGAIN while the camera
  thread stops, drains, reconfigures and restarts capture. Poll the same request.
  ACK fields are ``id, FourCC, width, height, pitch, size``. Failed layout
  requests restore the previous layout when possible.
* ``cam_capture <id>`` requests the next frame and initially returns -EAGAIN.
  Poll the same ID. ACK fields are ``id, width, height, pitch, byte_count,
  FourCC, crc32_hex, max_chunk``. No frame within three seconds gives -ETIMEDOUT.
* ``cam_read <id> <offset> <count>`` returns ``id, offset, hex_data``.
  Counts refer to native bytes, up to the advertised limit (currently 512).
  Repeating a read is safe. Invalid ranges return -EINVAL; stale IDs -ESTALE.
* ``cam_release <id>`` returns the ID and releases a pending/completed snapshot.
  Repeating release of the same ID is safe.

Only one snapshot can be active; other IDs receive -EBUSY. It remains immutable
while preview continues, including across configuration changes, and expires
after 30 seconds without valid capture/read activity. The PC retries lost
responses, checks sequence/CRC16, and verifies whole-image CRC32 before saving.

Validation
----------

Build with ``west build -b stm32n6570_dk --shield st_b_cams_imx_mb1854 ./sdk/app
-d ./builds/stm32n6570_dk/app``. Host protocol/color tests run with
``python -m unittest discover -s sdk/scripts -p test_camera_capture.py -v``.

Barcode protocol tests are included with ``-p 'test_camera*.py'``. The host C
integration test checks all N6 grayscale adapters, padded/downsampled RGB565
QR recognition, Code128 recognition, empty images and input bounds::

   cmake -S sdk/tests/app/camera_barcode -B /tmp/sdkz-camera-barcode -DCMAKE_BUILD_TYPE=Release
   cmake --build /tmp/sdkz-camera-barcode --parallel
   ctest --test-dir /tmp/sdkz-camera-barcode --output-on-failure
