/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_SHARED_MULTI_HEAP_H
#define TEST_SHARED_MULTI_HEAP_H
#include <stddef.h>
enum shared_multi_heap_attr {
	SMH_REG_ATTR_CACHEABLE, SMH_REG_ATTR_NON_CACHEABLE, SMH_REG_ATTR_EXTERNAL,
};
void *shared_multi_heap_alloc(enum shared_multi_heap_attr attr, size_t bytes);
void *shared_multi_heap_realloc(enum shared_multi_heap_attr attr, void *pointer, size_t bytes);
void shared_multi_heap_free(void *pointer);
#endif
