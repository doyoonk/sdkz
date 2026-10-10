/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/palloc.h>

#include <zephyr/kernel.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_HU_PALLOC

#define PALLOC_ALIGNMENT	_Alignof(max_align_t)
#define ALIGNED_MASK		(PALLOC_ALIGNMENT - 1)
#define ALIGNED_VALUE(a)	(((a) + (size_t)(ALIGNED_MASK)) & ~(size_t)(ALIGNED_MASK))

typedef struct __link
{
	struct __link* next;
	size_t flagNsize;
#define PALLOC_ALLOCATED	((size_t)0x1 << (sizeof(size_t) * 8 - 1))
} link_t, *plink_t;
static const size_t _sizeof_link_t = ALIGNED_VALUE(sizeof(link_t));

static link_t _anchor = { NULL, 0 };
static link_t* _ppool_last = NULL;
static size_t _pool_size = (size_t)0U;

static size_t _free_size = (size_t) 0U;

static size_t _num_alloc = (size_t)0U;
static size_t _num_free = (size_t)0U;

static K_SEM_DEFINE(palloc_sem, 1, 1);

static void _insert_link(link_t* plink);
static void _set_block(link_t* pstart, link_t* pend, size_t size, link_t* next, link_t* plink);
static void _set_link(link_t* plink, size_t size, link_t* next);

/* Convert payload bytes before taking the lock; no pool state is consulted. */
static size_t _block_size(size_t size)
{
	if (size == 0 || size > SIZE_MAX - _sizeof_link_t - ALIGNED_MASK)
		return 0;
	size = ALIGNED_VALUE(size + _sizeof_link_t);
	return (size & PALLOC_ALLOCATED) ? 0 : size;
}

/* All helpers below require palloc_sem to be held. */
static void* _alloc_locked(size_t size)
{
	if (size > _free_size)
		return NULL;

	link_t* prev = &_anchor;
	link_t* block = prev->next;
	while (block != NULL && block->flagNsize < size)
	{
		prev = block;
		block = block->next;
	}
	if (block == NULL)
		return NULL;

	prev->next = block->next;
	if (block->flagNsize - size >= _sizeof_link_t + PALLOC_ALIGNMENT)
	{
		link_t* tail = (link_t*)((uint8_t*)block + size);
		_set_link(tail, block->flagNsize - size, prev->next);
		block->flagNsize = size;
		prev->next = tail;
	}
	_free_size -= block->flagNsize;
	block->flagNsize |= PALLOC_ALLOCATED;
	block->next = NULL;
	_num_alloc++;
	return (uint8_t*)block + _sizeof_link_t;
}

static void _free_locked(link_t* block)
{
	if ((block->flagNsize & PALLOC_ALLOCATED) != 0 && block->next == NULL)
	{
		block->flagNsize &= ~PALLOC_ALLOCATED;
		_free_size += block->flagNsize;
		_insert_link(block);
		_num_free++;
	}
}

static void _trim_locked(link_t* block, size_t size)
{
	size_t old_size = block->flagNsize & ~PALLOC_ALLOCATED;
	size_t remainder = old_size - size;
	if (remainder >= _sizeof_link_t + PALLOC_ALIGNMENT)
	{
		link_t* tail = (link_t*)((uint8_t*)block + size);
		_set_link(tail, remainder, NULL);
		block->flagNsize = size | PALLOC_ALLOCATED;
		_free_size += remainder;
		_insert_link(tail);
	}
}

void* palloc(size_t size)
{
	size = _block_size(size);
	if (size == 0)
		return NULL;

	k_sem_take(&palloc_sem, K_FOREVER);
	void* ptr = _alloc_locked(size);
	k_sem_give(&palloc_sem);
	return ptr;
}

void pfree(void* p)
{
	if (p == NULL)
		return;

	k_sem_take(&palloc_sem, K_FOREVER);
	_free_locked((link_t*)((uint8_t*)p - _sizeof_link_t));
	k_sem_give(&palloc_sem);
}

void* pcalloc(size_t num, size_t size)
{
	if (size != 0 && num > SIZE_MAX / size)
		return NULL;
	size_t bytes = num * size;
	void* ptr = palloc(bytes);
	if (ptr != NULL)
		memset(ptr, 0, bytes);
	return ptr;
}

void* prealloc(void* ap, size_t nbytes)
{
	size_t size = _block_size(nbytes);
	if (nbytes != 0 && size == 0)
		return NULL;

	k_sem_take(&palloc_sem, K_FOREVER);
	void* ptr = NULL;
	if (ap == NULL)
	{
		if (size != 0)
			ptr = _alloc_locked(size);
		goto out;
	}

	link_t* block = (link_t*)((uint8_t*)ap - _sizeof_link_t);
	size_t old_size = block->flagNsize & ~PALLOC_ALLOCATED;
	if (size == 0)
	{
		_free_locked(block);
		goto out;
	}
	if (size <= old_size)
	{
		_trim_locked(block, size);
		ptr = ap;
		goto out;
	}

	/* Grow in place only when the physically adjacent free block is enough. */
	uintptr_t end = (uintptr_t)block + old_size;
	link_t* prev = &_anchor;
	while (prev->next != NULL && (uintptr_t)prev->next < end)
		prev = prev->next;
	link_t* next = prev->next;
	if (next != NULL && (uintptr_t)next == end && next->flagNsize >= size - old_size)
	{
		size_t combined = old_size + next->flagNsize;
		prev->next = next->next;
		_free_size -= next->flagNsize;
		block->flagNsize = combined | PALLOC_ALLOCATED;
		_trim_locked(block, size);
		ptr = ap;
		goto out;
	}

	/* Relocate only when in-place growth is impossible. Failure preserves ap. */
	ptr = _alloc_locked(size);
	if (ptr != NULL)
	{
		memcpy(ptr, ap, old_size - _sizeof_link_t);
		_free_locked(block);
	}
out:
	k_sem_give(&palloc_sem);
	return ptr;
}

size_t palloc_stats(size_t* plargest, size_t* psmallest, size_t* plinks)
{
	size_t links = 0;
	size_t largest = (size_t) 0U;
	size_t smallest = ~(size_t)0;
	k_sem_take(&palloc_sem, K_FOREVER);
	link_t* itor = _anchor.next;
	while(itor != NULL)
	{
		/* Region end markers have no allocatable payload. */
		if (itor->flagNsize > _sizeof_link_t)
		{
			size_t capacity = itor->flagNsize - _sizeof_link_t;
			links++;
			if (largest < capacity)
				largest = capacity;
			if (smallest > capacity)
				smallest = capacity;
		}
		itor = itor->next;
	}
	if (psmallest != NULL)
	{
		if (smallest == ~(size_t)0)
			smallest = 0;
		*psmallest = smallest;
	}
	if (plargest != NULL)
		*plargest = largest;
	if (plinks != NULL)
		*plinks = links;
	size_t free_size = _free_size;
	k_sem_give(&palloc_sem);
	return free_size;
}

void palloc_init(void* pstart, void* pend)
{
	uintptr_t start = (uintptr_t)pstart;
	uintptr_t end = (uintptr_t)pend;
	if (pstart == NULL || start > UINTPTR_MAX - ALIGNED_MASK)
		return;
	start = (start + ALIGNED_MASK) & ~(uintptr_t)ALIGNED_MASK;
	end &= ~(uintptr_t)ALIGNED_MASK;
	if (end <= start || end - start >= PALLOC_ALLOCATED)
		return;
	size_t block_size = (size_t)(end - start);

	if (block_size > (_sizeof_link_t * 2))
	{
		link_t* prev_pstart = &_anchor;
		link_t* next_pstart;
		k_sem_take(&palloc_sem, K_FOREVER);
		if (block_size > SIZE_MAX - _pool_size)
		{
			k_sem_give(&palloc_sem);
			return;
		}
		for (next_pstart = prev_pstart->next; next_pstart != NULL &&
			(uintptr_t)next_pstart < start; next_pstart = next_pstart->next)
		{
			if (next_pstart->next != NULL)
				prev_pstart = next_pstart;
		}

		if (next_pstart == NULL)
		{
			link_t* plink = _ppool_last;

			_ppool_last = (link_t*)(start + block_size - _sizeof_link_t);
			_set_block((link_t*)start, _ppool_last, block_size - _sizeof_link_t, NULL, plink);
			if (_anchor.next == NULL)
				_set_link(&_anchor, 0, (link_t*)start);
		}
		else
		{
			if ((uintptr_t)next_pstart > start)
			{
				link_t* pblast = (link_t*)(start + block_size - _sizeof_link_t);
				_set_block((link_t*)start, pblast, block_size - _sizeof_link_t, next_pstart, prev_pstart);
			}
			else
			{
				block_size = 0;
			}
		}
		if (block_size != 0)
		{
			_free_size += ((link_t*)start)->flagNsize;
			_pool_size += block_size;
		}
		k_sem_give(&palloc_sem);
	}
}

static void _set_block(link_t* pstart, link_t* pend, size_t size, link_t* next, link_t* plink)
{
	_set_link(pstart, size, pend);
	_set_link(pend, 0, next);
	if (plink != NULL)
		plink->next = pstart;
}

static void _set_link(link_t* plink, size_t size, link_t* next)
{
	plink->flagNsize = size;
	plink->next = next;
}

static void _insert_link(link_t* plink)
{
	link_t* itor;

	for (itor = &_anchor; itor->next != NULL &&
		(uintptr_t)itor->next < (uintptr_t)plink; itor = itor->next);
	if (((uint8_t*)itor + itor->flagNsize) == (uint8_t*)plink)
	{
		itor->flagNsize += plink->flagNsize;
		plink = itor;
	}
	if (((uint8_t*)plink + plink->flagNsize) == (uint8_t*)itor->next)
	{
		if (itor->next != _ppool_last)
			_set_link(plink, plink->flagNsize + itor->next->flagNsize, itor->next->next);
		else
			plink->next = _ppool_last;
	}
	else
	{
		plink->next = itor->next;
	}

	if (itor != plink)
		itor->next = plink;
}

#else
void* palloc(size_t size)
{
	return malloc(size);
}
void pfree(void* p)
{
	free(p);
}
void* pcalloc(size_t num, size_t size)
{
	return calloc(num, size);
}
void* prealloc(void *ap, size_t nbytes)
{
	if (nbytes == 0)
	{
		free(ap);
		return NULL;
	}
	return realloc(ap, nbytes);
}
size_t palloc_stats(size_t* plargest, size_t* psmallest, size_t* plinks)
{
	if (plargest != NULL)
		*plargest = 0;
	if (psmallest != NULL)
		*psmallest = 0;
	if (plinks != NULL)
		*plinks = 0;
	return 0;
}
void palloc_init(void* pstart, void* pend)
{
	(void)pstart;
	(void)pend;
}
#endif
