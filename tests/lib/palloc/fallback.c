/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/palloc.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
int main(void)
{
    size_t largest = 111, smallest = 222, links = 333;
    palloc_init(NULL, NULL);
    assert(palloc_stats(&largest, &smallest, &links) == 0);
    assert(largest == 0 && smallest == 0 && links == 0);
    assert(palloc_stats(NULL, NULL, NULL) == 0);
    uint8_t *p = pcalloc(4, 4); assert(p);
    for (int i = 0; i < 16; i++) { assert(p[i] == 0); p[i] = (uint8_t)i; }
    uint8_t *q = prealloc(p, 64); assert(q);
    for (int i = 0; i < 16; i++) assert(q[i] == (uint8_t)i);
    assert(prealloc(q, 0) == NULL);
    assert(prealloc(NULL, 0) == NULL);
    p = prealloc(NULL, 8); assert(p); pfree(p); pfree(NULL);
    return 0;
}
