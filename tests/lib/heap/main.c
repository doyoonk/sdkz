/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <string.h>
#include "heap_test.h"

/* Match the board's registration order, sizes and shared attributes. */
static uint8_t dtcm[128 * 1024 + 64] __aligned(32);
static uint8_t itcm[64 * 1024 + 64] __aligned(32);
static uint8_t ahbsram[32 * 1024 + 64] __aligned(32);
static uint8_t vencram[128 * 1024 + 64] __aligned(32);
static uint8_t external[8192] __aligned(32);

ZTEST(heap_boot, test_shared_regions)
{
    struct {
        uint8_t *base;
        size_t size;
        enum shared_multi_heap_attr attr;
        const char *name;
        size_t probe_size;
    } regions[] = {
        {dtcm, sizeof(dtcm), SMH_REG_ATTR_NON_CACHEABLE, "DTCM", 96 * 1024},
        {itcm, sizeof(itcm), SMH_REG_ATTR_NON_CACHEABLE, "ITCM", 48 * 1024},
        {ahbsram, sizeof(ahbsram), SMH_REG_ATTR_CACHEABLE, "AHBSRAM", 24 * 1024},
        {vencram, sizeof(vencram), SMH_REG_ATTR_CACHEABLE, "VENCRAM", 96 * 1024},
    };
    void *blocks[ARRAY_SIZE(regions)];
    struct shared_multi_heap_region region = {
        .addr = (uintptr_t)external,
        .size = sizeof(external),
        .attr = SMH_REG_ATTR_EXTERNAL,
    };

    zassert_ok(shared_multi_heap_pool_init());
    zassert_ok(shared_multi_heap_add(&region, NULL));
    uint8_t *guard = shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, 256);
    zassert_not_null(guard);
    memset(guard, 0x69, 256);

    for (size_t i = 0; i < ARRAY_SIZE(regions); i++) {
        memset(regions[i].base, 0x96, regions[i].size);
        /* Reserve both ends, as a linked prefix and excluded tail. */
        region.addr = (uintptr_t)(regions[i].base + 32);
        region.size = regions[i].size - 64;
        region.attr = regions[i].attr;
        zassert_ok(shared_multi_heap_add(&region, NULL));
        zassert_ok(shared_heap_self_test(region.addr, region.addr + region.size,
            regions[i].name, region.attr));
        for (size_t j = 0; j < 32; j++) {
            zassert_equal(regions[i].base[j], 0x96);
            zassert_equal(regions[i].base[regions[i].size - 32 + j], 0x96);
        }
    }
    /* Repeating with all pools present also covers a smaller pool registered
     * before a larger one. No test allocations may remain reserved.
     */
    for (size_t i = 0; i < ARRAY_SIZE(regions); i++) {
        zassert_ok(shared_heap_self_test((uintptr_t)(regions[i].base + 32),
            (uintptr_t)(regions[i].base + regions[i].size - 32),
            regions[i].name, regions[i].attr));
    }
    for (size_t i = 0; i < ARRAY_SIZE(regions); i++) {
        blocks[i] = shared_multi_heap_alloc(regions[i].attr, regions[i].probe_size);
        uintptr_t addr = (uintptr_t)blocks[i];
        zassert_true(addr >= (uintptr_t)(regions[i].base + 32));
        zassert_true(addr + regions[i].probe_size <=
            (uintptr_t)(regions[i].base + regions[i].size - 32));
    }
    for (size_t i = 0; i < ARRAY_SIZE(blocks); i++) shared_multi_heap_free(blocks[i]);
    for (size_t i = 0; i < 256; i++) zassert_equal(guard[i], 0x69);
    shared_multi_heap_free(guard);
}

ZTEST_SUITE(heap_boot, NULL, NULL, NULL, NULL, NULL);
