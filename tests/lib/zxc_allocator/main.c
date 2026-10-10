/* SPDX-License-Identifier: Apache-2.0 */
#include <zxc/zxc_allocator_backend.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include "shared_heap_test.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
	assert(!zxc_custom_malloc(0));
	assert(!zxc_custom_malloc(SIZE_MAX));
	assert(!zxc_custom_calloc(SIZE_MAX, 2));
	assert(!zxc_custom_calloc(3, 0));
	zxc_custom_free(NULL);
	uint8_t *p = zxc_custom_calloc(3, 17);
	assert(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
	assert(heap_test_live_bytes == 51);
	for (size_t i = 0; i < 51; i++) { assert(p[i] == 0); p[i] = (uint8_t)i; }
	size_t live = heap_test_live_bytes;
	heap_test_fail_next = true;
	assert(!zxc_custom_realloc(p, 8192));
	assert(heap_test_live_bytes == live);
	for (size_t i = 0; i < 51; i++) { assert(p[i] == i); }
	assert(!zxc_custom_realloc(p, SIZE_MAX));
	uint8_t *grown = zxc_custom_realloc(p, 8192);
	assert(grown && (uintptr_t)grown % _Alignof(max_align_t) == 0);
	assert(heap_test_live_bytes == 8192);
	for (size_t i = 0; i < 51; i++) { assert(grown[i] == i); }
	uint8_t *shrunk = zxc_custom_realloc(grown, 13);
	assert(shrunk && (uintptr_t)shrunk % _Alignof(max_align_t) == 0);
	assert(heap_test_live_bytes == 13);
	for (size_t i = 0; i < 13; i++) { assert(shrunk[i] == i); }
	shrunk = zxc_custom_realloc(shrunk, 13);
	assert(shrunk);
	assert(!zxc_custom_realloc(shrunk, 0));
	p = zxc_custom_realloc(NULL, 7);
	assert(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
	/* Cross-use the shared heap API to reject hidden prefixes or pointer shifts. */
	shared_multi_heap_free(p);
	p = shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, 17);
	memset(p, 0xa5, 17);
	p = zxc_custom_realloc(p, 37);
	assert(p);
	for (size_t i = 0; i < 17; i++) { assert(p[i] == 0xa5); }
	zxc_custom_free(p);
	assert(heap_test_live_bytes == 0);
	puts("PASS: alignment, zeroing, overflow, realloc preservation and balanced frees");
	return 0;
}
