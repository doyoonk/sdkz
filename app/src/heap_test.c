/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "heap_test.h"
#include <hu/palloc.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

LOG_MODULE_REGISTER(heap_test, CONFIG_LOG_DEFAULT_LEVEL);

static bool bytes_equal(const uint8_t *p, size_t size, uint8_t value)
{
    for (size_t i = 0; i < size; i++) {
        if (p[i] != value) return false;
    }
    return true;
}

#if CONFIG_HU_PALLOC
static bool in_pool(const void *p, size_t size, uintptr_t start, uintptr_t end)
{
    uintptr_t address = (uintptr_t)p;
    return p != NULL && address >= start && address < end &&
        size <= end - address && address % _Alignof(max_align_t) == 0;
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

/* Hold larger/earlier pools until an allocation reaches the newly added bank. */
int palloc_added_region_self_test(uintptr_t start, uintptr_t end, const char *name)
{
    void *held[8] = {0};
    size_t initial_largest, initial_smallest, initial_links;
    size_t initial = palloc_stats(&initial_largest, &initial_smallest, &initial_links);
    int rc = -EFAULT;
    bool tested = false;

    for (size_t i = 0; i < ARRAY_SIZE(held); i++) {
        size_t largest;
        palloc_stats(&largest, NULL, NULL);
        if (largest == 0) break;
        uint8_t *p = palloc(largest);
        if (p == NULL) goto out;
        held[i] = p;
        if (!in_pool(p, largest, start, end)) continue;

        /* Exercise the whole payload, including physical RAM behind D-cache. */
        memset(p, 0x5a, largest);
        int cache_rc = sys_cache_data_flush_and_invd_range(p, largest);
        if (cache_rc != 0 && cache_rc != -ENOTSUP) goto out;
        if (!bytes_equal(p, largest, 0x5a)) goto out;
        memset(p, 0xa5, largest);
        cache_rc = sys_cache_data_flush_and_invd_range(p, largest);
        if (cache_rc != 0 && cache_rc != -ENOTSUP) goto out;
        if (!bytes_equal(p, largest, 0xa5)) goto out;

        uint8_t *q = prealloc(p, 32);
        if (q == NULL) goto out;
        held[i] = q;
        if (q != p || !bytes_equal(q, 32, 0xa5)) goto out;
        q = prealloc(q, largest);
        if (q == NULL) goto out;
        held[i] = q;
        if (!in_pool(q, largest, start, end) || !bytes_equal(q, 32, 0xa5)) goto out;
        tested = true;
        break;
    }
    if (tested) rc = 0;
out:
    for (size_t i = 0; i < ARRAY_SIZE(held); i++) pfree(held[i]);
    size_t largest, smallest, links;
    if (palloc_stats(&largest, &smallest, &links) != initial ||
        largest != initial_largest || smallest != initial_smallest || links != initial_links) {
        rc = -EFAULT;
    }
    if (rc == 0) {
        LOG_INF("PASS %s [%p, %p): memory patterns/realloc, free %zu, regions %zu",
            name, (void*)start, (void*)end, initial, links);
    } else {
        LOG_ERR("%s palloc self-test failed", name);
    }
    return rc;
}

#endif /* CONFIG_HU_PALLOC */

#if !CONFIG_HU_PALLOC && defined(CONFIG_SHARED_MULTI_HEAP)
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/multi_heap.h>

#define SHARED_TEST_ALIGNMENT 32U

/* Find the largest aligned payload through the public, locked API. The
 * half-address-space limit leaves headroom for allocator size arithmetic.
 * Each probe is freed immediately; no allocator metadata is accessed.
 */
static size_t shared_largest(enum shared_multi_heap_attr attr)
{
    size_t low = 0;
    size_t high = (SIZE_MAX / 2) / SHARED_TEST_ALIGNMENT;

    while (low < high) {
        size_t mid = low + (high - low + 1) / 2;
        void *p = shared_multi_heap_aligned_alloc(attr, SHARED_TEST_ALIGNMENT,
            mid * SHARED_TEST_ALIGNMENT);

        if (p != NULL) {
            shared_multi_heap_free(p);
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low * SHARED_TEST_ALIGNMENT;
}

static bool shared_in_region(const void *p, size_t size, uintptr_t start, uintptr_t end)
{
    uintptr_t addr = (uintptr_t)p;

    return p != NULL && addr >= start && addr < end && size <= end - addr;
}

static bool shared_pattern(uint8_t *p, size_t size, uint8_t value,
    enum shared_multi_heap_attr attr)
{
    memset(p, value, size);
    if (attr != SMH_REG_ATTR_NON_CACHEABLE) {
        int rc = sys_cache_data_flush_and_invd_range(p, size);

        if (rc != 0 && rc != -ENOTSUP) return false;
    }
    return bytes_equal(p, size, value);
}

/* Run immediately after adding the region, before application heap users.
 * Temporarily hold larger pools of the same attribute until the new region
 * is reached. Only owned allocations are touched, and all exits free them.
 */
int shared_heap_self_test(uintptr_t start, uintptr_t end, const char *name,
    unsigned int attr_value)
{
    enum shared_multi_heap_attr attr = (enum shared_multi_heap_attr)attr_value;
    void *held[MAX_MULTI_HEAPS] = {0};
    uint8_t *p = NULL, *guard = NULL, *q;
    size_t payload = 0, initial_largest = 0;
    unsigned checks = 0;
    bool moved = false;
    int rc = -EFAULT;
#define CHECK(condition) do { \
    if (!(condition)) { \
        LOG_ERR("shared %s check failed at line %d", name, __LINE__); \
        goto out; \
    } \
    checks++; \
} while (0)

    CHECK(start < end && attr_value < MAX_SHARED_MULTI_HEAP_ATTR);
    initial_largest = shared_largest(attr);
    for (size_t i = 0; i < ARRAY_SIZE(held); i++) {
        payload = shared_largest(attr);
        CHECK(payload >= 256);
        p = shared_multi_heap_aligned_alloc(attr, SHARED_TEST_ALIGNMENT, payload);
        CHECK(p != NULL);
        if (shared_in_region(p, payload, start, end)) break;
        held[i] = p;
        p = NULL;
    }
    CHECK(shared_in_region(p, payload, start, end));
    CHECK((uintptr_t)p % SHARED_TEST_ALIGNMENT == 0);
    CHECK(shared_pattern(p, payload, 0x5a, attr));
    CHECK(shared_pattern(p, payload, 0xa5, attr));

    /* A rejected growth must leave the original allocation untouched. */
    q = shared_multi_heap_realloc(attr, p, SIZE_MAX / 2);
    if (q != NULL) {
        p = q;
        CHECK(false);
    }
    CHECK(bytes_equal(p, payload, 0xa5));
    q = shared_multi_heap_realloc(attr, p, 64);
    CHECK(q != NULL);
    p = q;
    CHECK(shared_in_region(p, 64, start, end) && bytes_equal(p, 64, 0xa5));
    q = shared_multi_heap_realloc(attr, p, payload);
    CHECK(q != NULL);
    p = q;
    CHECK(shared_in_region(p, payload, start, end) && bytes_equal(p, 64, 0xa5));
    CHECK(shared_pattern(p, payload, 0x3c, attr));
    q = shared_multi_heap_realloc(attr, p, 64);
    CHECK(q != NULL);
    p = q;
    CHECK(shared_in_region(p, 64, start, end) && bytes_equal(p, 64, 0x3c));

    /* A plain allocation may come from any pool of this attribute. Keep
     * the original region's block for the cross-region realloc below.
     */
    guard = shared_multi_heap_alloc(attr, 256);
    CHECK(guard != NULL && (uintptr_t)guard % sizeof(void *) == 0);
    memset(guard, 0xc3, 256);
    CHECK(bytes_equal(guard, 256, 0xc3) && bytes_equal(p, 64, 0x3c));
    shared_multi_heap_free(guard);
    guard = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(held); i++) {
        shared_multi_heap_free(held[i]);
        held[i] = NULL;
    }
    /* If a larger pool exists, force a move out of the tested region. */
    if (initial_largest >= end - start) {
        q = shared_multi_heap_realloc(attr, p, initial_largest);
        CHECK(q != NULL);
        p = q;
        CHECK(!shared_in_region(p, 1, start, end) && bytes_equal(p, 64, 0x3c));
        moved = true;
    }
    shared_multi_heap_free(p);
    p = NULL;
    CHECK(shared_largest(attr) == initial_largest);
    rc = 0;
out:
    shared_multi_heap_free(guard);
    shared_multi_heap_free(p);
    for (size_t i = 0; i < ARRAY_SIZE(held); i++) shared_multi_heap_free(held[i]);
    if (rc == 0) {
        LOG_INF("PASS shared %s [%p, %p): %u checks, payload %zu, moved %u",
            name, (void *)start, (void *)end, checks, payload, (unsigned)moved);
    }
    return rc;
#undef CHECK
}
#endif /* !CONFIG_HU_PALLOC && CONFIG_SHARED_MULTI_HEAP */
