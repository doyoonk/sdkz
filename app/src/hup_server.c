/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main.h"

#include <app/udp.h>
#include <app/app_api.h>
#include <hu/hupacket.h>
#include <hu/palloc.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/logging/log.h>

#include <huerrno.h>
#include <stdio.h>

LOG_MODULE_REGISTER(hup_server, CONFIG_LOG_DEFAULT_LEVEL);

struct handle
{
	void* hup;
	void* drv;
	const struct app_api* api;

	k_tid_t tid;
	struct k_thread tdata;
	char buffer[RECV_BUFFER_SIZE];
};

static void _hup_server(void* arg1, void* arg2, void* arg3)
{
	int received;
	struct handle* h = arg1;

	while (1)
	{
		received = h->api->recv(h->drv, h->buffer, sizeof(h->buffer));
		if (received < 0)
		{
			NET_ERR("UDP : Connection error %d", errno);
			break;
		}
		else if (received)
		{
			process_hupacket(h->hup, h->buffer, received);
		}
	}
}

void* init_hup_server(const struct app_api* api, const char* name, void* stack, int stack_size, void* arg1, void* arg2, void* arg3)
{
	struct handle* h;

	h = palloc(sizeof(struct handle));
	if (h == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	memset(h, 0, sizeof(struct handle));

	h->drv = api->init(arg1, arg2, arg3);
	if (h->drv == NULL)
		goto error_drv_init_hup_server;

	h->api = api;
	h->hup = init_hupacket(NULL, api->send, h->drv);
	if (h->hup == NULL)
		goto error_hup_init_hup_server;

	if (stack != NULL)
	{
		h->tid = k_thread_create(&h->tdata, stack, stack_size,
			_hup_server, h, NULL, NULL,
			7, 0, K_NO_WAIT);
		k_thread_name_set(h->tid, name);
		return h;
	}
	LOG_ERR("can not start hupacket");
	deinit_hup_server(h, api);

error_hup_init_hup_server:
	if (api->deinit != NULL)
		api->deinit(h->drv);
error_drv_init_hup_server:
	pfree(h);
	return NULL;
}

void deinit_hup_server(void *handle, const struct app_api* api)
{
	struct handle* h = handle;

	if (handle == NULL)
		return;

	if (h->drv != NULL && api != NULL)
		api->deinit(h->drv);

	if (h->tid != NULL)
	{
		k_thread_join(h->tid, K_MSEC(1000));
		k_thread_abort(h->tid);
	}

	if (h->hup != NULL)
		deinit_hupacket(h->hup);

	free(handle);
}