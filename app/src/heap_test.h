/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_HEAP_TEST_H
#define APP_HEAP_TEST_H

#include <stdint.h>

#if CONFIG_HU_PALLOC
int palloc_self_test(uintptr_t start, uintptr_t end, const char *name);
int palloc_added_region_self_test(uintptr_t start, uintptr_t end, const char *name);
int palloc_multi_region_self_test(uintptr_t itcm_start, uintptr_t itcm_end,
    uintptr_t dtcm_start, uintptr_t dtcm_end);
#elif defined(CONFIG_SHARED_MULTI_HEAP)
int shared_heap_self_test(uintptr_t start, uintptr_t end, const char *name,
    unsigned int attr);
#endif

#endif
