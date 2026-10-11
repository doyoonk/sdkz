/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PALLOC_TEST_KERNEL_H
#define PALLOC_TEST_KERNEL_H
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#ifndef TEST_HAS_DTCM
#define TEST_HAS_DTCM 1
#endif
#define DT_NODE_EXISTS(node) TEST_HAS_DTCM
#define DT_CHOSEN(name) 1
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define K_FOREVER (-1)
struct k_sem { pthread_mutex_t lock; };
#define K_SEM_DEFINE(name, initial, limit) struct k_sem name = {PTHREAD_MUTEX_INITIALIZER}
extern _Thread_local void (*test_before_lock)(void);
extern _Thread_local void (*test_after_unlock)(void);
static inline int k_sem_take(struct k_sem *sem, int timeout)
{
    (void)timeout;
    if (test_before_lock) {
        void (*hook)(void) = test_before_lock;
        test_before_lock = NULL;
        hook();
    }
    int rc = pthread_mutex_lock(&sem->lock);
    assert(rc == 0);
    return rc;
}
static inline void k_sem_give(struct k_sem *sem)
{
    int rc = pthread_mutex_unlock(&sem->lock);
    assert(rc == 0);
    if (test_after_unlock) {
        void (*hook)(void) = test_after_unlock;
        test_after_unlock = NULL;
        hook();
    }
}
#endif
