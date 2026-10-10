/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
/* A single-threaded FIFO model: tests synchronize access at callback gates. */
struct ring_buf { uint8_t *data; uint32_t capacity, used; };
static inline void ring_buf_init(struct ring_buf *r,uint32_t n,uint8_t *p) {
    r->data=p;r->capacity=n;r->used=0;
}
static inline uint32_t ring_buf_put_ptr(struct ring_buf *r,uint8_t **p,uint32_t n) {
    *p=r->data+r->used; return r->capacity-r->used;
}
static inline uint32_t ring_buf_get_ptr(struct ring_buf *r,uint8_t **p,uint32_t n) {
    *p=r->data;return r->used;
}
static inline void ring_buf_commit(struct ring_buf *r,uint32_t n) {
    assert(r->used+n<=r->capacity);r->used+=n;
}
static inline void ring_buf_consume(struct ring_buf *r,uint32_t n) {
    assert(n<=r->used);r->used-=n;memmove(r->data,r->data+n,r->used);
}
static inline bool ring_buf_is_empty(struct ring_buf *r) { return r->used==0; }
static inline uint32_t ring_buf_space_get(const struct ring_buf *r) { return r->capacity-r->used; }
static inline uint32_t ring_buf_put(struct ring_buf *r,const uint8_t *p,uint32_t n) {
    if(n>r->capacity-r->used)n=r->capacity-r->used;
    memcpy(r->data+r->used,p,n);r->used+=n;return n;
}
static inline uint32_t ring_buf_get(struct ring_buf *r,uint8_t *p,uint32_t n) {
    if(n>r->used)n=r->used;
    memcpy(p,r->data,n);ring_buf_consume(r,n);return n;
}
