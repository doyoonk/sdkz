/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>

struct device { int tag; };
extern const struct device test_internal_flash, test_external_flash;
#define DT_INST(inst, compat) inst
#define DEVICE_DT_GET(node) (&test_internal_flash)
#define FLASH_EX_OP_VENDOR_BASE 0x8000
struct flash_pages_info { off_t start_offset; size_t size; uint32_t index; };
int flash_get_page_info_by_offs(const struct device *dev, off_t offset,
                              struct flash_pages_info *info);
int flash_ex_op(const struct device *dev, uint16_t code, uintptr_t in, void *out);
