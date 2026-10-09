/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stm32n6xx_hal.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static bool inject_timeout;
static bool inject_stop_failure;

HAL_StatusTypeDef __real_HAL_DTS_PollForTemperature(DTS_HandleTypeDef *handle,
						  HAL_DTS_Sensor sensor, uint32_t timeout);
HAL_StatusTypeDef __real_HAL_DTS_Stop(DTS_HandleTypeDef *handle, HAL_DTS_Sensor sensor);

HAL_StatusTypeDef __wrap_HAL_DTS_PollForTemperature(DTS_HandleTypeDef *handle,
						  HAL_DTS_Sensor sensor, uint32_t timeout)
{
	return inject_timeout ? HAL_TIMEOUT :
		__real_HAL_DTS_PollForTemperature(handle, sensor, timeout);
}

HAL_StatusTypeDef __wrap_HAL_DTS_Stop(DTS_HandleTypeDef *handle, HAL_DTS_Sensor sensor)
{
	return inject_stop_failure ? HAL_ERROR : __real_HAL_DTS_Stop(handle, sensor);
}

ZTEST(stm32n6_dts, test_measurements)
{
	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(die_temp0));
	struct sensor_value temperature;

	zassert_true(device_is_ready(dev), "DTS initialization failed");
	zassert_equal(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature), -ENODATA);
	zassert_equal(sensor_sample_fetch_chan(dev, SENSOR_CHAN_VOLTAGE), -ENOTSUP);
	zassert_equal(sensor_channel_get(dev, SENSOR_CHAN_VOLTAGE, &temperature), -ENOTSUP);

	/* Repeated conversions also check that the HAL leaves SINGLE mode ready to restart. */
	for (int i = 0; i < 10; i++) {
		int64_t micro_celsius;

		zassert_ok(sensor_sample_fetch_chan(dev, SENSOR_CHAN_DIE_TEMP));
		zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature));
		micro_celsius = sensor_value_to_micro(&temperature);
		zassert_between_inclusive(micro_celsius, -40000000LL, 125000000LL);
		printk("DTS sample %d: %lld microdeg C\n", i, micro_celsius);
		k_msleep(20);
	}

	zassert_ok(sensor_sample_fetch(dev));
	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature));

	/* Inject a timeout: stale data must be rejected, and a new fetch must recover. */
	inject_timeout = true;
	zassert_equal(sensor_sample_fetch(dev), -ETIMEDOUT);
	zassert_equal(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature), -ENODATA);
	inject_timeout = false;
	zassert_ok(sensor_sample_fetch(dev));
	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature));

	/* A failed recovery must latch an error instead of returning an old temperature. */
	inject_timeout = true;
	inject_stop_failure = true;
	zassert_equal(sensor_sample_fetch(dev), -ETIMEDOUT);
	inject_timeout = false;
	inject_stop_failure = false;
	zassert_equal(sensor_sample_fetch(dev), -EIO);
	zassert_equal(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature), -ENODATA);
}

ZTEST_SUITE(stm32n6_dts, NULL, NULL, NULL, NULL, NULL);
