/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(app, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <inttypes.h>

#if CONFIG_DT_HAS_ST_STM32_RTC_ENABLED
const struct device* bbram_dev = DEVICE_DT_GET(DT_ALIAS(bbram));

int init_bbram()
{
	if (!device_is_ready(bbram_dev))
	{
		LOG_ERR("Backup device not ready!");
		return -ENODEV;
	}

	uint32_t boot_counter = 0;
	size_t bbram_size = 0;
	int ret = bbram_get_size(bbram_dev, &bbram_size);
	
	ret = bbram_read(bbram_dev, 0, 1, (uint8_t*)&boot_counter);
	if (ret != 0)
	{
		LOG_ERR("Failed to read from backup RAM (err %d)", ret);
		return ret;
	}

	boot_counter++;
	LOG_INF("bbram size is %d, Writing new boot count to BBRAM: %u", bbram_size, boot_counter);
	ret = bbram_write(bbram_dev, 0, 1, (uint8_t*)&boot_counter);
	if (ret != 0)
	{
		LOG_ERR("Failed to write to backup RAM (err %d)", ret);
		return ret;
	}
	return 0;
}
#else
int init_bbram()
{
	return 0;
}
#endif
