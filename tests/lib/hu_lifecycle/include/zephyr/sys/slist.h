/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
typedef struct node { struct node *next; } sys_snode_t;
typedef struct { sys_snode_t *head; } sys_slist_t;
#define SYS_SLIST_STATIC_INIT(p) {NULL}
#define SYS_SLIST_FOR_EACH_CONTAINER(list, it, field) \
    for(sys_snode_t *n=(list)->head; n && ((it)=CONTAINER_OF(n,__typeof__(*(it)),field),1); n=n->next)
static inline void sys_slist_append(sys_slist_t *l, sys_snode_t *n) {
    sys_snode_t **p=&l->head;while(*p)p=&(*p)->next;*p=n;n->next=NULL;
}
static inline void sys_slist_find_and_remove(sys_slist_t *l,sys_snode_t *n) {
    sys_snode_t **p=&l->head;while(*p && *p!=n)p=&(*p)->next;assert(*p==n);*p=n->next;
}
