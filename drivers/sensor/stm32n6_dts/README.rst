STM32N6 die temperature sensor
=============================

This driver exposes one STM32N6 DTS sensing element using
``SENSOR_CHAN_DIE_TEMP``. It uses the STM32N6 HAL with 12-bit single
conversions and bounded polling. It does not require an ADC or an IRQ.

Include ``<st/n6/stm32n6-dts.dtsi>`` in the application overlay, enable
``&die_temp``, and add ``die-temp0 = &die_temp`` to ``/aliases``. The
STM32N6570-DK application already includes this configuration. Enable
``CONFIG_SENSOR`` and ``CONFIG_STM32N6_DTS``. The driver selects the HAL
module automatically. HSI must remain enabled; HAL derives the 4 MHz
sensor clock from HSI / 8.

The DTS node owns the entire controller. ``sensor-id`` selects sensing
element 0 (default) or 1. Do not instantiate two nodes for the controller.
Simultaneous exposure of both elements, sensor triggers, and device power
management are not implemented.

Application API::

    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(die_temp0));
    struct sensor_value temperature;
    int ret;

    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    ret = sensor_sample_fetch(dev);
    if (ret != 0) {
        return ret;
    }
    ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature);

The result is the die temperature in degrees Celsius. Before the first
successful fetch, and after a failed fetch, ``channel_get`` returns
``-ENODATA``. Unsupported channels return ``-ENOTSUP``. Conversion timeout
returns ``-ETIMEDOUT``; the driver stops the conversion so it can be retried.
If stopping fails, subsequent fetches return ``-EIO`` until reinitialization.
Calls must run in thread context. Individual calls are serialized; clients
needing an atomic fetch/get pair must also serialize that pair externally.

With ``CONFIG_SENSOR_SHELL=y``, read the sensor from the Zephyr shell::

    sensor get temperature-sensor@5600a000 die_temp

Application cache and HU command
--------------------------------

The application's main thread samples the sensor once per second, independently
of LED blinking. ``app.die_temp_mc`` stores milli-degrees Celsius,
``app.die_temp_status`` stores the latest fetch result (zero on success), and
``app.die_temp_updated_ms`` is the uptime of the last successful sample.
The cache writer and HU reader use a spinlock to obtain consistent snapshots.
A failed update retains the last successful value and timestamp but marks the
cache invalid through its nonzero status.

The HU command ``die_temp`` takes no arguments and reads the cache without
starting a conversion. Its ACK contains the standard zero status record,
temperature in milli-degrees Celsius, and age of the sample in milliseconds.
For example, temperature ``34636`` means 34.636 degrees Celsius. Age is capped
at ``INT_MAX``. Before the first update it returns NAK with ``-EAGAIN``;
after an unsuccessful update it returns NAK with that error. Extra arguments
return NAK with ``-EINVAL``.

Without an ID, sequence, or CRC, the wire format is::

    Request:  0x05 "die_temp" 0x04
    Success:  0x06 "die_temp" 0x1e "0" 0x1e "34636" 0x1e "237" 0x04

``CONFIG_STM32N6_DTS_TIMEOUT_MS`` defaults to 100 ms. The HAL also has
bounded internal waits for the serial configuration interface.

The application explicitly places sensor-shell RTIO buffers in normal
AXISRAM using ``CONFIG_RTIO_BLOCK_POOL_PLACEMENT_DEFAULT`` and
``CONFIG_RTIO_SQE_PLACEMENT_DEFAULT``. This avoids the implicit DTCM
placement selected when ``zephyr,dtcm`` is present in the application.

Hardware test
-------------

Build the standalone hardware test from the west workspace::

    west build -b stm32n6570_dk sdk/tests/drivers/sensor/stm32n6_dts -d builds/stm32n6570_dk/die-temp-test

The test checks readiness, pre-fetch and unsupported-channel errors, and
repeated real conversions. Linker wrappers additionally inject a conversion
timeout and a stop failure to check retry behavior, stale-data rejection,
and the persistent error state. The final injected stop failure deliberately
prints an error; the ztest PASS/FAIL result determines success. Reset the
board after running the test.
