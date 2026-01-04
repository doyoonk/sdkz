/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#if CONFIG_BOOTLOADER_MCUBOOT && CONFIG_RETENTION_BOOTLOADER_INFO

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bootloader, CONFIG_LOG_DEFAULT_LEVEL);

#include <app/bootloader.h>
#include <zephyr/kernel.h>

#include <zephyr/retention/blinfo.h>
#include <bootutil/boot_status.h>
#include <bootutil/image.h>
#include <bootutil/security_cnt.h>
#include <bootutil/boot_record.h>

int bootloader_active_slot(uint8_t* slot)
{
	int rc;

	rc = blinfo_lookup(BLINFO_RUNNING_SLOT, slot, sizeof(*slot));
	if (rc < 0) {
		LOG_ERR("Failed to fetch active slot: %d", rc);
		return rc;
	}

	return 0;
}

int bootloader_max_appsize(int *max_size)
{
	int rc;

	rc = blinfo_lookup(BLINFO_MAX_APPLICATION_SIZE, (char*)max_size, sizeof(*max_size));
	if (rc < 0) {
		LOG_ERR("Failed to lookup max application size: %d", rc);
		return rc;
	} else if (rc == 0) {
		LOG_ERR("No data read for max application size");
		return -ENOENT;
	}

	return 0;
}

#else

int bootloader_active_slot(uint8_t* slot) { return -ENOTSUP; }
int bootloader_max_appsize(int *max_size) { return -ENOTSUP; }

#endif
