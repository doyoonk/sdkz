/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include <app/app_api.h>
#include <zephyr/net/net_mgmt.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct app_data {
    bool connected;

	void* hup_uart;
	void* hup_udp;

#if CONFIG_NET_CONNECTION_MANAGER
    struct net_mgmt_event_callback mgmt_cb;
#endif
};

extern struct app_data app;

int init_bbram();

void* init_hup_server(const struct app_api* api
    , const char* name, void* stack, int stack_size
    , void* arg1, void* arg2, void* arg3);
void deinit_hup_server(void *handle, const struct app_api* api);

#endif // __MAIN_H__
