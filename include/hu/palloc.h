/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PALLOC_H__
#define __PALLOC_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PALLOC_OK                        0
#define PALLOC_ERROR                    -1
#define PALLOC_ERROR_PTR_RANGE          -2
#define PALLOC_ERROR_INVALID_HEADER     -3
#define PALLOC_ERROR_BAD_LINK           -4
#define PALLOC_ERROR_BLOCK_SIZE         -5

/* Add a persistent, non-overlapping region [start, end) to the custom pool.
 * Alignment padding is discarded; invalid or too-small ranges are ignored.
 * This is a no-op when CONFIG_HU_PALLOC is disabled.
 */
void	palloc_init(void* start, void* end);
/* Thread context only. Returned storage is aligned for max_align_t.
 * free/realloc require NULL or a live pointer from this allocator. The caller
 * must own the allocation exclusively while freeing or reallocating it.
 */
void*	palloc(size_t);
void	pfree(void *);
/* A zero size frees the allocation and returns NULL; failure for a nonzero
 * size preserves the original allocation and its contents.
 */
void*	prealloc(void*, size_t);
void*	pcalloc (size_t, size_t);
/* Snapshot of the custom pool: return free block bytes including headers;
 * largest/smallest are usable payload sizes, links counts free blocks.
 * Output pointers may be NULL. All values are zero when empty/unavailable.
 */
size_t  palloc_stats(size_t* plargest, size_t* psmallest, size_t* plinks);

#ifdef __cplusplus
}
#endif

#endif
