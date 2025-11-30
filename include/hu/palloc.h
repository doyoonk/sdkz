/*
 * Copyright (c) 2025 HU Inc.
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

void	palloc_init(void*, void*);
void*	palloc(size_t);
void	pfree(void *);
void*	prealloc(void*, size_t);
void*	pcalloc (size_t, size_t);
size_t palloc_stats(size_t* plargest, size_t* psmallest, size_t* plinks);

#ifdef __cplusplus
}
#endif

#endif
