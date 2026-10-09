/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT st_stm32n6_dts

#include <errno.h>
#include <stm32n6xx_hal.h>
#include <stm32_ll_rcc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(stm32n6_dts, CONFIG_SENSOR_LOG_LEVEL);

/* The HAL addresses the sensing elements of the single DTS controller directly. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "STM32N6 DTS must have a single owner");

struct stm32n6_dts_config {
	DTS_TypeDef *base;
	struct stm32_pclken clock;
	HAL_DTS_Sensor sensor;
};

struct stm32n6_dts_data {
	DTS_HandleTypeDef handle;
	struct k_mutex lock;
	struct sensor_value temperature;
	bool valid;
	bool faulted;
};

static int stm32n6_dts_error(HAL_StatusTypeDef status)
{
	return status == HAL_TIMEOUT ? -ETIMEDOUT : -EIO;
}

static int stm32n6_dts_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct stm32n6_dts_config *cfg = dev->config;
	struct stm32n6_dts_data *data = dev->data;
	HAL_StatusTypeDef status;
	float_t temperature;
	int ret;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->valid = false;
	if (data->faulted) {
		ret = -EIO;
		goto out;
	}

	status = HAL_DTS_Start(&data->handle, cfg->sensor);
	if (status != HAL_OK) {
		ret = stm32n6_dts_error(status);
		goto out;
	}

	status = HAL_DTS_PollForTemperature(&data->handle, cfg->sensor,
					  CONFIG_STM32N6_DTS_TIMEOUT_MS);
	if (status != HAL_OK) {
		ret = stm32n6_dts_error(status);
		/* A failed single conversion leaves the HAL in RUNNING state. */
		if (HAL_DTS_Stop(&data->handle, cfg->sensor) != HAL_OK) {
			data->faulted = true;
			LOG_ERR("Failed to stop DTS after conversion failure");
		}
		goto out;
	}

	/* Successful single-shot polling restores the HAL READY state. */
	status = HAL_DTS_GetTemperature(&data->handle, cfg->sensor, &temperature);
	if (status != HAL_OK) {
		ret = stm32n6_dts_error(status);
		goto out;
	}

	ret = sensor_value_from_float(&data->temperature, temperature);
	data->valid = (ret == 0);
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int stm32n6_dts_channel_get(const struct device *dev, enum sensor_channel chan,
				 struct sensor_value *value)
{
	struct stm32n6_dts_data *data = dev->data;
	int ret = 0;

	if (chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->valid) {
		*value = data->temperature;
	} else {
		ret = -ENODATA;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int stm32n6_dts_init(const struct device *dev)
{
	const struct stm32n6_dts_config *cfg = dev->config;
	struct stm32n6_dts_data *data = dev->data;
	const struct device *clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	DTS_SensorConfigTypeDef sensor_cfg = {
		.Mode = DTS_SENSOR_MODE_SINGLE,
		.Resolution = DTS_SENSOR_RESOLUTION_12BITS,
	};
	HAL_StatusTypeDef status;
	int ret;

	k_mutex_init(&data->lock);
	if (!device_is_ready(clock)) {
		return -ENODEV;
	}
	/* HAL_DTS_Init derives the 4 MHz sensor clock from the fixed HSI / 8 clock. */
	if (LL_RCC_HSI_IsReady() == 0U) {
		LOG_ERR("DTS requires HSI to be enabled");
		return -EIO;
	}

	ret = clock_control_on(clock, (clock_control_subsys_t)&cfg->clock);
	if (ret != 0) {
		return ret;
	}

	data->handle.Instance = cfg->base;
	status = HAL_DTS_Init(&data->handle);
	if (status == HAL_OK) {
		status = HAL_DTS_ConfigSensor(&data->handle, cfg->sensor, &sensor_cfg);
	}
	if (status != HAL_OK) {
		LOG_ERR("DTS initialization failed: %u (0x%x)", status, data->handle.ErrorCode);
		if (clock_control_off(clock, (clock_control_subsys_t)&cfg->clock) != 0) {
			LOG_ERR("Failed to disable DTS clock");
		}
		return stm32n6_dts_error(status);
	}

	return 0;
}

static DEVICE_API(sensor, stm32n6_dts_api) = {
	.sample_fetch = stm32n6_dts_sample_fetch,
	.channel_get = stm32n6_dts_channel_get,
};

BUILD_ASSERT(DT_INST_REG_ADDR(0) == DTS_BASE, "DTS address must match the HAL security domain");

static struct stm32n6_dts_data stm32n6_dts_data_0;
static const struct stm32n6_dts_config stm32n6_dts_config_0 = {
	.base = (DTS_TypeDef *)DT_INST_REG_ADDR(0),
	.clock = STM32_DT_INST_CLOCK_INFO(0),
	.sensor = DT_INST_PROP(0, sensor_id),
};

SENSOR_DEVICE_DT_INST_DEFINE(0, stm32n6_dts_init, NULL, &stm32n6_dts_data_0,
			     &stm32n6_dts_config_0, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
			     &stm32n6_dts_api);
