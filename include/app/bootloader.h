/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BOOTLOADER_H__
#define __BOOTLOADER_H__

#include <stdint.h>

int bootloader_active_slot(uint8_t* slot);
int bootloader_max_appsize(int *max_size);

#endif
