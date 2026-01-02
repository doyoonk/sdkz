/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/palloc.h>

#include <zephyr/kernel.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if DT_NODE_EXISTS(DT_CHOSEN(zephyr_dtcm)) && !defined(STM32N6)
 
#define ALIGNED_MASK		(sizeof(size_t) - 1)
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

void* palloc(size_t size)
{
	if (_anchor.next == NULL || size == 0)
		return NULL;

	size = ALIGNED_VALUE(size + _sizeof_link_t);
	if ((size & PALLOC_ALLOCATED) != 0)
		return NULL;
	if ((size > _free_size))
		return NULL;

	link_t* prev_plink = &_anchor;
	link_t* plink = _anchor.next;

	k_sem_take(&palloc_sem, K_FOREVER);
	while ((plink->flagNsize < size) && (plink->next != NULL))
	{
		prev_plink = plink;
		plink = plink->next;
	}

	if (plink == _ppool_last)
	{
		k_sem_give(&palloc_sem);
		return NULL;
	}

	void* ptr = (void*)((uint8_t*)prev_plink->next + _sizeof_link_t);
	prev_plink->next = plink->next;

	if ((plink->flagNsize - size) > (_sizeof_link_t * 2))
	{
		link_t* next_plink = (link_t*)((uint8_t*)plink + size);

		_set_link(next_plink, plink->flagNsize - size, prev_plink->next);
		plink->flagNsize = size;
		prev_plink->next = next_plink;
	}

	_free_size -= plink->flagNsize;

	plink->flagNsize |= PALLOC_ALLOCATED;
	plink->next = NULL;
	_num_alloc++;

	k_sem_give(&palloc_sem);

	return ptr;
}

void pfree(void* p)
{
	uint8_t* ptr = (uint8_t*)p;

	if (ptr != NULL)
	{
		link_t* plink;
		ptr -= _sizeof_link_t;
		plink = (void*)ptr;

		if ((plink->flagNsize & PALLOC_ALLOCATED) != 0)
		{
			if (plink->next == NULL)
			{
				k_sem_take(&palloc_sem, K_FOREVER);
				plink->flagNsize &= ~PALLOC_ALLOCATED;
				_free_size += plink->flagNsize;
				_insert_link((link_t*)plink);
				_num_free++;
				k_sem_give(&palloc_sem);
			}
		}
	}
}

void* pcalloc(size_t num, size_t size)
{
	void* ptr = NULL;
	size_t tsize = num * size;

	if (tsize > 0)
	{
		ptr = palloc(tsize);
		if (ptr != NULL)
			memset(ptr, 0, tsize);
	}

	return ptr;
}

void* prealloc(void *ap, size_t nbytes)
{
	if (ap == NULL)
	{
		ap = palloc(nbytes);
	}
	else
	{
		link_t* plink = (link_t*)((uint8_t*)ap - _sizeof_link_t);
		size_t csize = plink->flagNsize & ~PALLOC_ALLOCATED;
		if (csize < nbytes)
		{
			#if 0
			if (plink->next != NULL && (plink->next->flagNsize & PALLOC_ALLOCATED) == 0)
			{
				size_t nsize = plink->next->flagNsize;
				size_t tsize = csize + nsize;

				nbytes = (nbytes + 7) & ~(size_t)7;
				if (tsize > nbytes)
				{
					size_t new_block_size = nsize - nbytes - csize;
					link_t* pnew_link = (link_t*)((uint8_t*)plink + _sizeof_link_t + nbytes);

					k_sem_take(&palloc_sem, K_FOREVER);
					_set_link(pnew_link, new_block_size, plink->next->next);
					_set_link(plink, nbytes, pnew_link);
					k_sem_give(&palloc_sem);
					return ap;
				}
			}
			#endif
			void* ptr = palloc(nbytes);
			if (ptr != NULL)
			{
				memcpy(ptr, ap, nbytes);
				pfree(ap);
			}
			return ptr;
		}
	}
	return ap;
}

size_t palloc_stats(size_t* plargest, size_t* psmallest, size_t* plinks)
{
	size_t links = 0;
	size_t largest = (size_t) 0U;
	size_t smallest = ~(size_t)0;
	link_t* itor = _anchor.next;

	k_sem_take(&palloc_sem, K_FOREVER);
	while(itor != NULL)
	{
		links ++;
		if (largest < itor->flagNsize)
			largest = itor->flagNsize;
		if (itor->flagNsize > 0 && smallest > itor->flagNsize)
			smallest = itor->flagNsize;
		itor = itor->next;
	}
	if (psmallest != NULL)
	{
		if (smallest == ~(size_t)0)
			smallest = 0;
		*psmallest = smallest - _sizeof_link_t;
	}
	if (plargest != NULL)
		*plargest = largest - _sizeof_link_t;
	if (plinks != NULL)
		*plinks = links;
	k_sem_give(&palloc_sem);
    return _free_size;
}

void palloc_init(void* pstart, void* pend)
{
	size_t start = ALIGNED_VALUE((size_t)pstart);
	size_t end = ALIGNED_VALUE((size_t)pend);
	size_t block_size = end - start;

	if (block_size > (_sizeof_link_t * 2))
	{
		link_t* prev_pstart = &_anchor;
		link_t* next_pstart;
		k_sem_take(&palloc_sem, K_FOREVER);
		for (next_pstart = prev_pstart->next; next_pstart != NULL &&
			(size_t)next_pstart < start; next_pstart = next_pstart->next)
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
			if ((size_t)next_pstart > start)
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

	for (itor = &_anchor; itor->next < plink; itor = itor->next);
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
	return realloc(ap, nbytes);
}
size_t palloc_stats(size_t* plargest, size_t* psmallest, size_t* plinks)
{
	return 0;
}
void palloc_init(void* pstart, void* pend)
{
}
#endif