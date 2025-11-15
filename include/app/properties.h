/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PROPERTIES_H__
#define __PROPERTIES_H__

#include <stdint.h>

struct Property {
    const char* const key;
    uint32_t offset;
    uint32_t length;
    void* value_default;
};

#endif
