/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdint.h>

struct Property {
    const char* const key;
    uint32_t offset;
    uint32_t length;
    void* default;
};

#endif
