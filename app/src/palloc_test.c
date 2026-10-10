/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "palloc_test.h"
#include <hu/palloc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

LOG_MODULE_REGISTER(palloc_test, CONFIG_LOG_DEFAULT_LEVEL);

static bool in_pool(const void *p, size_t size, uintptr_t start, uintptr_t end)
{
    uintptr_t address = (uintptr_t)p;
    return p != NULL && address >= start && address < end &&
        size <= end - address && address % _Alignof(max_align_t) == 0;
}

static bool bytes_equal(const uint8_t *p, size_t size, uint8_t value)
{
    for (size_t i = 0; i < size; i++) {
        if (p[i] != value) return false;
    }
    return true;
}

/* Called before HU users start; every successful test leaves the pool intact. */
int palloc_self_test(uintptr_t start, uintptr_t end, const char *name)
{
    uint8_t *p = NULL, *guard = NULL, *q;
    size_t largest, smallest, links;
    size_t initial = palloc_stats(&largest, &smallest, &links);
    size_t initial_largest = largest;
    unsigned checks = 0;
    int rc = -EFAULT;
#define CHECK(condition) do { \
    if (!(condition)) { LOG_ERR("check failed at line %d", __LINE__); goto out; } \
    checks++; \
} while (0)

    CHECK(initial > 8192 && largest > 8192 && links == 1);
    CHECK(palloc(0) == NULL && prealloc(NULL, 0) == NULL);
    CHECK(palloc(SIZE_MAX) == NULL && pcalloc(SIZE_MAX, 2) == NULL);
    p = pcalloc(17, 3);
    CHECK(in_pool(p, 51, start, end) && bytes_equal(p, 51, 0));
    pfree(p); p = NULL;
    CHECK(palloc_stats(NULL, NULL, NULL) == initial);

    p = prealloc(NULL, 16);
    CHECK(in_pool(p, 16, start, end));
    memset(p, 0x31, 16);
    q = prealloc(p, 17);
    CHECK(q != NULL);
    bool same = q == p;
    p = q;
    CHECK(same && in_pool(p, 17, start, end) && bytes_equal(p, 16, 0x31));
    memset(p, 0x42, 17);
    q = prealloc(p, 1024);
    CHECK(q != NULL);
    same = q == p; p = q;
    CHECK(same && in_pool(p, 1024, start, end) && bytes_equal(p, 17, 0x42));
    memset(p, 0x53, 1024);
    size_t before = palloc_stats(NULL, NULL, NULL);
    q = prealloc(p, 32);
    CHECK(q != NULL);
    same = q == p; p = q;
    CHECK(same && bytes_equal(p, 32, 0x53) && palloc_stats(NULL, NULL, NULL) > before);

    guard = palloc(64);
    CHECK(in_pool(guard, 64, start, end));
    memset(guard, 0x64, 64);
    q = prealloc(p, 4096);
    CHECK(q != NULL);
    same = q == p; p = q;
    CHECK(!same && in_pool(p, 4096, start, end) && bytes_equal(p, 32, 0x53));
    CHECK(bytes_equal(guard, 64, 0x64));
    CHECK(prealloc(p, SIZE_MAX) == NULL && bytes_equal(p, 32, 0x53));
    CHECK(prealloc(p, 0) == NULL);
    p = NULL;
    pfree(guard); guard = NULL;
    CHECK(palloc_stats(&largest, &smallest, &links) == initial && largest == initial_largest);

    /* Allocate and check the whole payload to exercise both ends of the region. */
    p = palloc(largest);
    CHECK(in_pool(p, largest, start, end));
    memset(p, 0x75, largest);
    CHECK(bytes_equal(p, largest, 0x75));
    CHECK(palloc(1) == NULL);
    CHECK(prealloc(p, largest + 1) == NULL && bytes_equal(p, largest, 0x75));
    CHECK(palloc_stats(&largest, &smallest, &links) == 0 &&
          largest == 0 && smallest == 0 && links == 0);
    pfree(p); p = NULL;
    CHECK(palloc_stats(&largest, &smallest, &links) == initial &&
          largest == initial_largest && largest == smallest && links == 1);
    LOG_INF("PASS %u checks: %s [%p, %p), align %zu, free %zu, largest %zu",
        checks, name, (void*)start, (void*)end, (size_t)_Alignof(max_align_t), initial, largest);
    rc = 0;
out:
    pfree(guard);
    pfree(p);
    return rc;
#undef CHECK
}

/* The board's ITCM is below DTCM and smaller; exercise both free-list regions. */
int palloc_multi_region_self_test(uintptr_t itcm_start, uintptr_t itcm_end,
    uintptr_t dtcm_start, uintptr_t dtcm_end)
{
    uint8_t *p = NULL, *guard = NULL, *q;
    size_t largest, smallest, links;
    size_t initial = palloc_stats(&largest, &smallest, &links);
    size_t initial_largest = largest, initial_smallest = smallest;
    unsigned checks = 0;
    int rc = -EFAULT;
#define CHECK(condition) do { \
    if (!(condition)) { LOG_ERR("multi-region check failed at line %d", __LINE__); goto out; } \
    checks++; \
} while (0)

    CHECK(itcm_end <= dtcm_start && links == 2 && smallest > 64 && largest > smallest);
    p = pcalloc(17, 3);
    CHECK(in_pool(p, 51, itcm_start, itcm_end) && bytes_equal(p, 51, 0));
    pfree(p); p = NULL;
    CHECK(palloc_stats(NULL, NULL, NULL) == initial);

    /* Fill ITCM, then move the allocation to DTCM while preserving every byte. */
    p = palloc(initial_smallest);
    CHECK(in_pool(p, initial_smallest, itcm_start, itcm_end));
    memset(p, 0x86, initial_smallest);
    CHECK(bytes_equal(p, initial_smallest, 0x86));
    q = prealloc(p, initial_largest);
    CHECK(q != NULL);
    p = q;
    CHECK(in_pool(p, initial_largest, dtcm_start, dtcm_end));
    CHECK(bytes_equal(p, initial_smallest, 0x86));
    memset(p, 0x97, initial_largest);
    CHECK(bytes_equal(p, initial_largest, 0x97));

    guard = palloc(initial_smallest);
    CHECK(in_pool(guard, initial_smallest, itcm_start, itcm_end));
    memset(guard, 0xa8, initial_smallest);
    CHECK(bytes_equal(guard, initial_smallest, 0xa8));
    CHECK(palloc(1) == NULL);
    CHECK(prealloc(guard, initial_smallest + 1) == NULL);
    CHECK(bytes_equal(guard, initial_smallest, 0xa8));
    CHECK(prealloc(p, initial_largest + 1) == NULL);
    CHECK(bytes_equal(p, initial_largest, 0x97));
    CHECK(palloc_stats(&largest, &smallest, &links) == 0 &&
        largest == 0 && smallest == 0 && links == 0);

    pfree(p); p = NULL;
    pfree(guard); guard = NULL;
    CHECK(palloc_stats(&largest, &smallest, &links) == initial &&
        largest == initial_largest && smallest == initial_smallest && links == 2);
    /* Combined free space cannot satisfy an allocation spanning the gap. */
    CHECK(palloc(initial_largest + 1) == NULL);
    LOG_INF("PASS %u checks: ITCM/DTCM, free %zu, largest %zu, regions %zu",
        checks, initial, largest, links);
    rc = 0;
out:
    pfree(guard);
    pfree(p);
    return rc;
#undef CHECK
}
