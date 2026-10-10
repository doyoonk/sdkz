/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_PALLOC_TEST_H
#define APP_PALLOC_TEST_H
#include <stdint.h>
int palloc_self_test(uintptr_t start, uintptr_t end, const char *name);
int palloc_multi_region_self_test(uintptr_t itcm_start, uintptr_t itcm_end,
    uintptr_t dtcm_start, uintptr_t dtcm_end);
#endif
