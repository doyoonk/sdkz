/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(settings, CONFIG_LOG_DEFAULT_LEVEL);

#include <app/settings.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/hash_function.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include <zephyr/fs/zms.h>

#include <stdlib.h>
#include <errno.h>

#if CONFIG_ZMS && DT_HAS_FIXED_PARTITION_LABEL(storage_partition)

#define SETTINGS_PAGE_SIZE			256

static struct zms_fs _fs = {0};

#define SETTINGS_PARTITION_DEVICE		FIXED_PARTITION_DEVICE(storage_partition)
#define SETTINGS_PARTITION_OFFSET		FIXED_PARTITION_OFFSET(storage_partition)
#define SETTINGS_PARTITION_SIZE			FIXED_PARTITION_SIZE(storage_partition)

// TODO: read/write setting

static int _settings_init(void) {
	int free_space;
	int ret;
	struct flash_pages_info info = {0};

	_fs.flash_device = SETTINGS_PARTITION_DEVICE;
	_fs.offset = SETTINGS_PARTITION_OFFSET;
	if (!device_is_ready(_fs.flash_device)) {
		return -ENXIO;
	}

	ret = flash_get_page_info_by_offs(_fs.flash_device, _fs.offset, &info);
	if (ret) {
		return ret;
	}

	if (info.size == 0)
		info.size = SETTINGS_PAGE_SIZE;
	_fs.sector_size = info.size;
	_fs.sector_count = SETTINGS_PARTITION_SIZE / info.size;

	ret = zms_mount(&_fs);
	if (ret) {
		return ret;
	}
	free_space = zms_calc_free_space(&_fs);
	LOG_INF("Sector count %d, size %d, free space %d", _fs.sector_count, _fs.sector_size, free_space);
	if (free_space < 0) {
		return -ENOMEM;
	}
	return 0;
}

SYS_INIT(_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_ZMS */