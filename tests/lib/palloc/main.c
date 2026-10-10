/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../app/src/palloc_test.h"

_Thread_local void (*test_before_lock)(void);
_Thread_local void (*test_after_unlock)(void);
static const void *watched_source;
static size_t watched_capacity, copied_bytes;
static void *checked_memcpy(void *dst, const void *src, size_t size)
{
    if (src == watched_source) {
        assert(size <= watched_capacity);
        copied_bytes = size;
    }
    return memcpy(dst, src, size);
}
#define memcpy checked_memcpy
/* Include the implementation to reset private state and audit copy bounds. */
#include "../../../lib/hu/palloc.c"
#undef memcpy

_Alignas(max_align_t) static unsigned char arena[65536];
static void reset_pool(void)
{
    memset(arena, 0xa5, sizeof(arena));
    _anchor.next = NULL;
    _anchor.flagNsize = 0;
    _ppool_last = NULL;
    _pool_size = _free_size = _num_alloc = _num_free = 0;
    test_before_lock = test_after_unlock = NULL;
    watched_source = NULL;
    watched_capacity = copied_bytes = 0;
}
static size_t capacity(void *p)
{
    link_t *block = (link_t *)((uint8_t *)p - _sizeof_link_t);
    return (block->flagNsize & ~PALLOC_ALLOCATED) - _sizeof_link_t;
}
static void check_bytes(const void *p, size_t size, uint8_t value)
{
    const uint8_t *bytes = p;
    for (size_t i = 0; i < size; i++) assert(bytes[i] == value);
}
static void check_pool(void)
{
    size_t sum = 0, nodes = 0;
    uintptr_t last_end = 0;
    for (link_t *b = _anchor.next; b; b = b->next) {
        assert(++nodes < sizeof(arena) / _sizeof_link_t);
        assert((uintptr_t)b >= last_end);
        assert((uintptr_t)b % PALLOC_ALIGNMENT == 0);
        assert(!(b->flagNsize & PALLOC_ALLOCATED));
        assert(b->flagNsize % PALLOC_ALIGNMENT == 0);
        assert(b->flagNsize <= _pool_size);
        sum += b->flagNsize;
        last_end = (uintptr_t)b + (b->flagNsize ? b->flagNsize : _sizeof_link_t);
    }
    assert(sum == _free_size);
}
static void test_reallocation(void)
{
    reset_pool();
    palloc_init(arena, arena + 2048);
    size_t initial = _free_size;
    uint8_t *p = prealloc(NULL, 16);
    assert(p && capacity(p) >= 16);
    memset(p, 0x31, 16);
    /* Adjacent free space permits growth in place, including 16 -> 17. */
    assert(prealloc(p, 17) == p && capacity(p) >= 17);
    check_bytes(p, 16, 0x31);
    assert(prealloc(p, 256) == p && capacity(p) >= 256);
    memset(p, 0x42, 256);
    size_t before = _free_size;
    assert(prealloc(p, 16) == p && _free_size > before);
    check_bytes(p, 16, 0x42);
    /* A live neighbour forces relocation and bounds the source copy. */
    void *guard = palloc(64);
    assert(guard);
    watched_source = p;
    watched_capacity = capacity(p);
    uint8_t *q = prealloc(p, 512);
    assert(q && q != p && capacity(q) >= 512);
    assert(copied_bytes == watched_capacity);
    watched_source = NULL;
    check_bytes(q, 16, 0x42);
    assert(prealloc(q, SIZE_MAX) == NULL);
    check_bytes(q, 16, 0x42);
    assert(prealloc(q, 0) == NULL);
    pfree(guard);
    assert(_free_size == initial);
    assert(prealloc(NULL, 0) == NULL);
    check_pool();

    /* Consume an adjacent block exactly, without constructing an empty tail. */
    reset_pool(); palloc_init(arena, arena + 2048);
    p = palloc(32);
    void *adjacent = palloc(64);
    guard = palloc(32);
    assert(p && adjacent && guard);
    size_t grown = capacity(p) + _sizeof_link_t + capacity(adjacent);
    pfree(adjacent);
    assert(prealloc(p, grown) == p && capacity(p) == grown);
    pfree(p); pfree(guard); check_pool();
}
static void test_empty_full_and_overflow(void)
{
    reset_pool();
    size_t largest = 9, smallest = 9, links = 9;
    assert(palloc_stats(&largest, &smallest, &links) == 0);
    assert(largest == 0 && smallest == 0 && links == 0);
    assert(!palloc(1) && !prealloc(NULL, 1));
    palloc_init(arena, arena + 512);
    size_t initial = palloc_stats(&largest, &smallest, &links);
    assert(largest == smallest && links == 1);
    assert(!palloc(0) && !pcalloc(0, 32) && !pcalloc(32, 0));
    assert(!palloc(SIZE_MAX) && !palloc(PALLOC_ALLOCATED));
    assert(!pcalloc(SIZE_MAX / 2 + 2, 2));
    assert(!pcalloc(SIZE_MAX, 2));
    for (size_t i = 0; i <= _sizeof_link_t + ALIGNED_MASK; i++)
        assert(!palloc(SIZE_MAX - i));
    uint8_t *p = palloc(largest);
    assert(p); memset(p, 0x73, largest);
    assert(palloc_stats(&largest, &smallest, &links) == 0);
    assert(largest == 0 && smallest == 0 && links == 0);
    size_t old_capacity = capacity(p);
    assert(!prealloc(p, old_capacity + 1));
    check_bytes(p, old_capacity, 0x73);
    pfree(p); pfree(NULL);
    assert(palloc_stats(NULL, NULL, NULL) == initial);
    p = pcalloc(7, 9); assert(p); check_bytes(p, 63, 0); pfree(p);
    check_pool();
}
static void test_boundaries_alignment_regions(void)
{
    reset_pool();
    palloc_init(NULL, arena + 512);
    palloc_init(arena + 512, arena);
    palloc_init(arena, arena);
    palloc_init(arena, arena + 1);
    palloc_init((void *)UINTPTR_MAX, (void *)UINTPTR_MAX);
    assert(_anchor.next == NULL && _free_size == 0);
    palloc_init(arena + 1, arena + 511);
    assert(arena[0] == 0xa5 && arena[511] == 0xa5);
    void *ptrs[16];
    for (size_t i = 0; i < 8; i++) {
        ptrs[i] = palloc(i + 1); assert(ptrs[i]);
        assert((uintptr_t)ptrs[i] % _Alignof(max_align_t) == 0);
        memset(ptrs[i], 0, i + 1);
    }
    for (size_t i = 0; i < 8; i++) pfree(ptrs[i]);
    assert(arena[0] == 0xa5 && arena[511] == 0xa5);
    check_pool();
    /* Non-overlapping regions registered in non-address order, with gaps. */
    reset_pool();
    palloc_init(arena + 1024, arena + 1536);
    palloc_init(arena, arena + 512);
    palloc_init(arena + 2048, arena + 2560);
    size_t initial = _free_size;
    size_t count = 0;
    while (count < 16 && (ptrs[count] = palloc(128)) != NULL) count++;
    assert(count >= 6);
    for (size_t i = count; i; i--) pfree(ptrs[i - 1]);
    assert(_free_size == initial);
    check_bytes(arena + 512, 512, 0xa5);
    check_bytes(arena + 1536, 512, 0xa5);
    check_pool();
}
static void test_cross_region_reallocation(void)
{
    reset_pool();
    /* Match the board: register the larger, higher-address region first. */
    palloc_init(arena + 1024, arena + 2048);
    palloc_init(arena, arena + 512);
    size_t largest, smallest, links;
    size_t initial = palloc_stats(&largest, &smallest, &links);
    assert(links == 2);
    uint8_t *p = palloc(smallest);
    assert(p == arena + _sizeof_link_t);
    memset(p, 0x86, smallest);
    watched_source = p; watched_capacity = smallest;
    uint8_t *q = prealloc(p, largest);
    assert(q == arena + 1024 + _sizeof_link_t && copied_bytes == smallest);
    check_bytes(q, smallest, 0x86);
    memset(q, 0x97, largest);
    p = palloc(smallest);
    assert(p == arena + _sizeof_link_t);
    memset(p, 0xa8, smallest);
    assert(palloc(1) == NULL && prealloc(p, smallest + 1) == NULL);
    check_bytes(p, smallest, 0xa8); check_bytes(q, largest, 0x97);
    pfree(q); pfree(p);
    assert(palloc_stats(NULL, NULL, &links) == initial && links == 2);
    assert(palloc(largest + 1) == NULL);
    check_bytes(arena + 512, 512, 0xa5);
    check_pool();
}
static void *competing;
static void allocate_competing(void) { competing = palloc(32); assert(competing); }
static void test_lock_interleavings(void)
{
    reset_pool(); palloc_init(arena, arena + 2048);
    size_t initial = _free_size;
    test_before_lock = allocate_competing;
    uint8_t *p = palloc(64);
    assert(p && competing);
    assert((uintptr_t)p + 64 <= (uintptr_t)competing ||
           (uintptr_t)competing + 32 <= (uintptr_t)p);
    memset(p, 0x11, 64); memset(competing, 0x22, 32);
    check_pool();
    pfree(p); pfree(competing);
    test_after_unlock = allocate_competing;
    size_t largest, smallest, links;
    assert(palloc_stats(&largest, &smallest, &links) == initial);
    assert(links == 1 && largest == smallest);
    pfree(competing); check_pool();
}
static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13; *state ^= *state >> 17; *state ^= *state << 5;
    return *state;
}
static void *stress_worker(void *arg)
{
    struct { uint8_t *p; size_t size; uint8_t value; } slots[12] = {0};
    uint32_t seed = (uint32_t)(uintptr_t)arg;
    for (size_t step = 0; step < 4000; step++) {
        size_t i = next_random(&seed) % 12;
        size_t size = next_random(&seed) % 768 + 1;
        if (slots[i].p) check_bytes(slots[i].p, slots[i].size, slots[i].value);
        if (next_random(&seed) % 4 == 0) {
            pfree(slots[i].p); slots[i].p = NULL;
        } else {
            uint8_t *p = prealloc(slots[i].p, size);
            if (p) {
                if (slots[i].p) check_bytes(p, size < slots[i].size ? size : slots[i].size, slots[i].value);
                assert((uintptr_t)p % _Alignof(max_align_t) == 0);
                slots[i].p = p; slots[i].size = size;
                slots[i].value = (uint8_t)next_random(&seed);
                memset(p, slots[i].value, size);
            }
        }
        if (step % 100 == 0) {
            size_t largest, smallest, links;
            size_t free_bytes = palloc_stats(&largest, &smallest, &links);
            assert(largest <= free_bytes && smallest <= largest);
            assert((links == 0) == (largest == 0));
        }
    }
    for (size_t i = 0; i < 12; i++) {
        if (slots[i].p) check_bytes(slots[i].p, slots[i].size, slots[i].value);
        pfree(slots[i].p);
    }
    return NULL;
}
static void test_concurrency(void)
{
    reset_pool(); palloc_init(arena, arena + sizeof(arena));
    size_t initial = _free_size;
    pthread_t threads[6];
    for (size_t i = 0; i < 6; i++) assert(pthread_create(&threads[i], NULL, stress_worker, (void *)(uintptr_t)(i + 1)) == 0);
    for (size_t i = 0; i < 6; i++) assert(pthread_join(threads[i], NULL) == 0);
    assert(_free_size == initial); check_pool();
}
static void test_boot_self_tests(void)
{
    /* Run the firmware's tests against each region alone and then together. */
    reset_pool(); palloc_init(arena, arena + 16384);
    assert(palloc_self_test((uintptr_t)arena, (uintptr_t)(arena + 16384), "ITCM") == 0);
    check_pool();
    reset_pool(); palloc_init(arena + 32768, arena + sizeof(arena));
    assert(palloc_self_test((uintptr_t)(arena + 32768),
        (uintptr_t)(arena + sizeof(arena)), "DTCM") == 0);
    palloc_init(arena, arena + 16384);
    assert(palloc_multi_region_self_test((uintptr_t)arena, (uintptr_t)(arena + 16384),
        (uintptr_t)(arena + 32768), (uintptr_t)(arena + sizeof(arena))) == 0);
    check_bytes(arena + 16384, 16384, 0xa5);
    check_pool();
}

int main(void)
{
    test_reallocation();
    test_empty_full_and_overflow();
    test_boundaries_alignment_regions();
    test_cross_region_reallocation();
    test_lock_interleavings();
    test_concurrency();
    test_boot_self_tests();
    puts("palloc: realloc, bounds, overflow, alignment, stats and 24000 threaded operations passed");
    return 0;
}
