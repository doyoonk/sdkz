/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PALLOC_TEST_CACHE_H
#define PALLOC_TEST_CACHE_H

#include <errno.h>
#include <stddef.h>

/* Host tests exercise allocation/data integrity, not STM32 cache hardware. */
static inline int sys_cache_data_flush_and_invd_range(void *addr, size_t size)
{
    (void)addr;
    (void)size;
    return -ENOTSUP;
}

#endif
