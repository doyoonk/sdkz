/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#define FIXED_PARTITION_EXISTS(name) 1
#define PARTITION_ID(name) name##_id
#define slot0_partition_id 2
#define slot1_partition_id 3
struct flash_area { uint8_t fa_id; off_t fa_off; uint32_t fa_size; const struct device *fa_dev; };
static inline const struct device *flash_area_get_device(const struct flash_area *fa) {
    return fa->fa_dev;
}
int flash_area_open(uint8_t id,const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
int flash_area_write(const struct flash_area *fa,off_t off,const void *src,size_t len);
int flash_area_erase(const struct flash_area *fa,off_t off,size_t len);
