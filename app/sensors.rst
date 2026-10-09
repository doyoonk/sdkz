Application sensor cache and HU commands
========================================

The main thread samples die temperature and VBAT once per second, independently
of LED availability. Each sensor has a cached value, the status of its latest
attempt (zero on success), and the uptime in milliseconds of its last successful
measurement. A failed measurement retains the previous value and timestamp but
sets a nonzero status. HU handlers take a consistent snapshot under a spinlock;
they do not start a new conversion.

The ``app_data`` fields are:

* Temperature: ``die_temp_mc`` (mC), ``die_temp_status``,
  ``die_temp_updated_ms``.
* VBAT: ``vbat_mv`` (mV), ``vbat_status``, ``vbat_updated_ms``.

The ``die_temp`` and ``vbat`` commands take no arguments. Successful responses
contain the standard zero status record, cached value, and sample age in ms
(capped at INT_MAX). Before the first measurement, the response is NAK with
-EAGAIN. Measurement errors return NAK with that error; extra arguments return
NAK with -EINVAL. Both commands use the existing USB CDC and UDP HU registry.

VBAT example without an ID, sequence, or CRC::

    Request:  0x05 "vbat" 0x04
    Success:  0x06 "vbat" 0x1e "0" 0x1e "3287" 0x1e "237" 0x04

This means 3.287 V, measured 237 ms ago.

STM32N6570-DK VBAT configuration
------------------------------

The application alias ``vbat0`` references the board's enabled ``vbat`` node.
Zephyr's existing CONFIG_STM32_VBAT driver uses ADC2 channel 16, a 12-bit
conversion, maximum acquisition time, and the internal VBAT/4 path. The SoC
DTS supplies ratio=4; the board supplies vref-mv=1800. The driver restores the
division ratio when converting the ADC reading, so the application must not
multiply by four again. No SDK-specific VBAT driver is required.

This reports the VBAT supply voltage; it is not a battery state-of-charge
estimate. Accuracy depends on the actual ADC reference matching the configured
1.8 V reference.
