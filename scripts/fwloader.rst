HU firmware loader
==================

Run from the workspace root. Python 3.10+ and ``pyserial`` are required::

  python -m pip install pyserial
  python sdk/scripts/fwloader.py --port COM3 --baud 115200 --partition slot0 builds/stm32n6570_dk/app/zephyr/zephyr.signed.confirmed.hex
  python sdk/scripts/fwloader.py --port COM3 --partition boot_partition builds/stm32n6570_dk/mcuboot/zephyr/zephyr.signed.hex --reboot

Use the HU protocol port: on the current board COM3 is HU USB CDC, COM5 is
MicroPython and COM15 is the Zephyr shell/ST-Link console. Linux serial port
names such as ``/dev/ttyACM0`` also work. The board runs the app with BOOT1 OFF/L
during HU updates. Install firmware containing ``flash_info`` and ``flash_read``
once using ST-Link before using this loader; older firmware is rejected before
erase. Close other applications using the selected port.

Serial parameters: ``--baud``, ``--data-bits``, ``--parity``, ``--stop-bits``,
``--rtscts``, ``--dsrdtr``, ``--xonxoff``, ``--device-id``, ``--timeout`` and
``--erase-timeout``. Defaults are 115200, 8N1, no flow control, 5 seconds per
request and 120 seconds for a full partition erase.

Image format selection
----------------------

The user chooses the file and target partition. By default, ``--image-type raw``
performs no image header or board compatibility check, for every partition.
Here ``raw`` still means an Intel HEX input file; it does not select binary input.
The loader does not infer the image format from the SoC or partition name.

Optional checks can be selected explicitly with ``--image-type mcuboot``
(MCUboot header and TLV bounds) or ``--image-type stm32-v2.3`` (STM32 v2.3 header,
payload length and checksum). These checks do not validate cryptographic
signatures or establish board compatibility. The user is responsible for
selecting an image that boots on the target.

Transfer behavior
-----------------

* Validate Intel HEX checksums, record types, overlapping data, EOF and addresses.
* Query partition geometry and validate all data before modifying flash.
* Execute ``wrprt <partition> 0``, ``erase``, ``flash`` (up to 256 bytes of
  ASCII85 per request), full partition readback, then ``wrprt <partition> 1``.
* Only unsupported-operation errors (Zephyr ``-ENOTSUP`` = -134, or -95) skip write protection. Other errors stop the transfer.
  After attempted unprotect, protection restoration is attempted even on errors
  or interruption, unless the driver explicitly reported unsupported protection.
* Erase the whole selected partition. Omitted HEX ranges remain erased and are
  included in readback verification. Writes respect the device's write unit.
* No automatic retries of erase/write commands. A missing ACK can mean the
  operation completed; resolve the connection problem and restart the full update.
* No implicit reset. ``--reboot`` runs only after verification and protection
  restoration succeed. ``--dry-run`` only queries and validates; ``--verify-only``
  compares without erase, write or protection changes.

``slot0``, ``slot1``, ``inactive`` and ``boot_partition`` are supported.
``boot`` and ``mcuboot`` are aliases for ``boot_partition``. ``inactive`` is
resolved once into its concrete slot. The HEX must be linked/rebased for the
selected partition address. To explicitly relocate a slot0 RAM-load image to
an inactive slot, use ``--base 0x70080000``; that address is interpreted as
partition offset zero. This does not relocate machine code or make XIP firmware
suitable for another slot. Image activation/confirmation policies are unchanged;
the loader does not set MCUboot swap requests.

STM32N6570-DK MCUboot HEX
------------------------

The SDK's MCUboot build generates both ``zephyr.signed.bin`` and
``zephyr.signed.hex`` after STM32 signing. The HEX has the address from
``boot_partition`` (currently 0x70000000), including the STM32 v2.3 header.
The ordinary ``zephyr.hex`` has RAM execution addresses and must not be used
for this update. App images use MCUboot signed HEX, preferably the existing
``zephyr.signed.confirmed.hex`` when installing directly into the running slot.

The STM32 signing tool must be in PATH, as required by the Zephyr SoC build::

  export PATH=/opt/st/stm32cubeclt_1.21.0/STM32CubeProgrammer/bin:$PATH
  west build -d builds/stm32n6570_dk/mcuboot

MCUboot is overwritten directly and has no fallback bootloader slot here; an
interrupted update may require BOOT1 ON/H and ST-Link recovery. The current
STM32 XSPI driver does not support ``wrprt`` and returns ``-ENOTSUP``.

Protocol additions
------------------

``flash_info <partition>`` returns these records after HU status::

  canonical_name, flash_offset, partition_size, write_unit, erase_value,
  mapped_address, max_chunk

Numbers are hexadecimal. The loader also accepts the older eight-field response
and ignores its trailing image type; header checks are selected only by the user.
``flash_read <partition> <offset_hex> <size_hex>`` returns::

  partition, offset_hex, size_hex, data_hex

The read limit is 256 bytes. Existing ``wrprt``, ``erase`` and ``flash`` wire
formats are unchanged. Multiple updaters must not target the board concurrently.

Tests::

  python -m unittest discover -s sdk/scripts -p test_fwloader.py -v
  cmake -S sdk/tests/lib/hu_lifecycle -B tmp/fwloader-hu-tests
  cmake --build tmp/fwloader-hu-tests
  ctest --test-dir tmp/fwloader-hu-tests --output-on-failure
