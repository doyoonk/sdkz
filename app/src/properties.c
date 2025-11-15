/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(properties, CONFIG_LOG_DEFAULT_LEVEL);

#include <app/properties.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/hash_function.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include <zephyr/fs/zms.h>

#include <stdlib.h>
#include <errno.h>

#define PROPERTIES_PAGE_SIZE			256

static struct zms_fs _fs = {0};

#define PROPERTIES_PARTITION_DEVICE		FIXED_PARTITION_DEVICE(storage_partition)
#define PROPERTIES_PARTITION_OFFSET		FIXED_PARTITION_OFFSET(storage_partition)
#define PROPERTIES_PARTITION_SIZE		FIXED_PARTITION_SIZE(storage_partition)

// TODO: read/write property

static int _properties_init(void) {
	int free_space;
	int rc;
	struct flash_pages_info info = {0};

	_fs.flash_device = PROPERTIES_PARTITION_DEVICE;
	_fs.offset = PROPERTIES_PARTITION_OFFSET;
	if (!device_is_ready(_fs.flash_device)) {
		return -ENXIO;
	}

	rc = flash_get_page_info_by_offs(_fs.flash_device, _fs.offset, &info);
	if (rc) {
		return rc;
	}

	if (info.size == 0)
		info.size = PROPERTIES_PAGE_SIZE;
	_fs.sector_size = info.size;
	_fs.sector_count = PROPERTIES_PARTITION_SIZE / info.size;

	rc = zms_mount(&_fs);
	if (rc) {
		return rc;
	}
	free_space = zms_calc_free_space(&_fs);
	if (free_space < 0) {
		return -ENOMEM;
	}
	LOG_INF("Free space in storage is %u bytes", free_space);
	return 0;
}

SYS_INIT(_properties_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
