/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdint.h>

int settings_get_bootloader_active_slot(uint8_t* slot);
int settings_get_bootloader_max_application_size(int *max_size);

#endif
