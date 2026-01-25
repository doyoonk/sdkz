/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#if CONFIG_RETENTION_BOOTLOADER_INFO

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bootloader, CONFIG_LOG_DEFAULT_LEVEL);

#include <app/bootloader.h>
#include <zephyr/kernel.h>

#include <zephyr/retention/blinfo.h>
#include <bootutil/boot_status.h>
#include <bootutil/image.h>
#include <bootutil/security_cnt.h>
#include <bootutil/boot_record.h>
#if  CONFIG_RETENTION_BOOTLOADER_INFO_OUTPUT_SETTINGS
#include <bootutil/boot_status.h>
#include <bootutil/image.h>
#include <zephyr/mcuboot_version.h>
#include <zephyr/settings/settings.h>
#endif

int bootloader_active_slot(uint8_t* slot)
{
	int rc;

	*slot = 0xff;
#if CONFIG_RETENTION_BOOTLOADER_INFO_OUTPUT_SETTINGS
 	rc = settings_runtime_get("blinfo/running_slot", slot, sizeof(*slot));
	if (rc != sizeof(*slot)) {
		LOG_ERR("Failed to fetch active slot from settings: %d", rc);
		return rc < 0 ? rc : -EIO;
	}
#else
	rc = blinfo_lookup(BLINFO_RUNNING_SLOT, slot, sizeof(*slot));
	if (rc < 0) {
		LOG_ERR("Failed to fetch active slot: %d", rc);
		return rc;
	}
#endif

	return 0;
}

#else

int bootloader_active_slot(uint8_t* slot) { return -ENOTSUP; }

#endif
