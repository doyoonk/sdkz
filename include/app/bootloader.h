/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdint.h>

int bootloader_get_active_slot(uint8_t* slot);
int bootloader_get_max_appsize(int *max_size);

#endif
