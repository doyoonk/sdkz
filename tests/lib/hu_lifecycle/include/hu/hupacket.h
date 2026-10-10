/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
#define RECV_BUFFER_SIZE CONFIG_HU_PACKET_SIZE
/* Mock only protocol lifecycle; wire-format tests use the real header. */
typedef ssize_t (*send_func)(void *,const uint8_t *,size_t);
void *init_hupacket(void *,send_func,void *);
void deinit_hupacket(void *);
void process_hupacket(void *,uint8_t *,size_t);
