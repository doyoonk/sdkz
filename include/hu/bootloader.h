/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef __BOOTLOADER_H__
#define __BOOTLOADER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

int bootloader_active_slot(uint8_t* slot);

#ifdef __cplusplus
}
#endif

#endif /* __BOOTLOADER_H__ */
