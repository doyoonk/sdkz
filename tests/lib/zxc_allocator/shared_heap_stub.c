/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/multi_heap/shared_multi_heap.h>
#include "shared_heap_test.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

bool heap_test_fail_next;
size_t heap_test_live_bytes, heap_test_peak_bytes;
static struct { void *pointer; size_t size; } blocks[4096];

void *shared_multi_heap_alloc(enum shared_multi_heap_attr attr, size_t bytes)
{
	assert(attr == SMH_REG_ATTR_EXTERNAL && bytes);
	if (heap_test_fail_next) { heap_test_fail_next = false; return NULL; }
	if (bytes > 30 * 1024 * 1024 - heap_test_live_bytes) { return NULL; }
	void *pointer = malloc(bytes);
	assert(pointer);
	for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
		if (!blocks[i].pointer) {
			blocks[i].pointer = pointer; blocks[i].size = bytes;
			heap_test_live_bytes += bytes;
			if (heap_test_live_bytes > heap_test_peak_bytes) {
				heap_test_peak_bytes = heap_test_live_bytes;
			}
			return pointer;
		}
	}
	abort();
}

void *shared_multi_heap_realloc(enum shared_multi_heap_attr attr, void *pointer, size_t bytes)
{
	assert(attr == SMH_REG_ATTR_EXTERNAL && pointer && bytes);
	if (heap_test_fail_next) { heap_test_fail_next = false; return NULL; }
	for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
		if (blocks[i].pointer == pointer) {
			size_t other_bytes = heap_test_live_bytes - blocks[i].size;
			if (bytes > 30 * 1024 * 1024 - other_bytes) { return NULL; }
			void *replacement = realloc(pointer, bytes);
			if (!replacement) { return NULL; }
			blocks[i].pointer = replacement; blocks[i].size = bytes;
			heap_test_live_bytes = other_bytes + bytes;
			if (heap_test_live_bytes > heap_test_peak_bytes) {
				heap_test_peak_bytes = heap_test_live_bytes;
			}
			return replacement;
		}
	}
	abort();
}

void shared_multi_heap_free(void *pointer)
{
	for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
		if (blocks[i].pointer == pointer && pointer) {
			heap_test_live_bytes -= blocks[i].size;
			blocks[i].pointer = NULL;
			free(pointer);
			return;
		}
	}
	abort();
}
