/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define SYS_FOREVER_US (-1)
#define DT_NODE_EXISTS(x) 0
#define DT_NODE_HAS_STATUS(x, status) 1
#define DT_CHOSEN(x) 0
#define DT_ALIAS(x) 0
#define CONTAINER_OF(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))
struct k_sem { pthread_mutex_t lock; pthread_cond_t ready; unsigned count, limit; };
static inline void k_sem_init(struct k_sem *s, unsigned count, unsigned limit) {
    assert(!pthread_mutex_init(&s->lock, NULL)); assert(!pthread_cond_init(&s->ready, NULL));
    s->count=count; s->limit=limit;
}
static inline int k_sem_take(struct k_sem *s, int timeout) {
    pthread_mutex_lock(&s->lock);
    while (!s->count) {
        if (timeout==K_NO_WAIT) { pthread_mutex_unlock(&s->lock); return -EAGAIN; }
        assert(timeout==K_FOREVER); pthread_cond_wait(&s->ready, &s->lock);
    }
    s->count--; pthread_mutex_unlock(&s->lock); return 0;
}
static inline void k_sem_give(struct k_sem *s) {
    pthread_mutex_lock(&s->lock); if(s->count<s->limit) s->count++;
    pthread_cond_signal(&s->ready); pthread_mutex_unlock(&s->lock);
}
static inline void k_sem_reset(struct k_sem *s) {
    pthread_mutex_lock(&s->lock); s->count=0; pthread_mutex_unlock(&s->lock);
}
static inline void k_msleep(int ms) { usleep(ms*1000); }
static atomic_llong test_time_offset;
static inline int64_t k_uptime_get(void) {
    struct timespec now; assert(!clock_gettime(CLOCK_MONOTONIC,&now));
    return now.tv_sec*1000LL+now.tv_nsec/1000000+atomic_load(&test_time_offset);
}
struct k_spinlock { atomic_int locked; };
typedef int k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {
    while(atomic_exchange(&l->locked, 1)) { sched_yield(); }
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t key) {
    (void)key; atomic_store(&l->locked, 0);
}
struct k_thread { pthread_t thread; void (*entry)(void *,void *,void *); void *a,*b,*c; };
typedef struct k_thread *k_tid_t;
static _Thread_local k_tid_t test_current;
static inline k_tid_t k_current_get(void) { return test_current; }
static inline k_tid_t k_thread_create(struct k_thread *t, void *stack, int size,
    void (*entry)(void *,void *,void *), void *a,void *b,void *c,int prio,int opts,int delay) {
    (void)stack;(void)size;(void)prio;(void)opts; assert(delay==K_FOREVER);
    t->entry=entry;t->a=a;t->b=b;t->c=c;return t;
}
static inline void *test_thread_entry(void *p) {
    k_tid_t t=p; test_current=t;t->entry(t->a,t->b,t->c);return NULL;
}
static inline void k_thread_start(k_tid_t t) { assert(!pthread_create(&t->thread,NULL,test_thread_entry,t)); }
static inline int k_thread_join(k_tid_t t, int timeout) { assert(timeout==K_FOREVER);return pthread_join(t->thread,NULL); }
static inline void k_thread_name_set(k_tid_t t,const char *name) { (void)t;(void)name; }
