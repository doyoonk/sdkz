/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/udp.h>
#include <app/app_api.h>
#include <hu/hupacket.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/logging/log.h>

#include <huerrno.h>
#include <stdio.h>

LOG_MODULE_DECLARE(hup_server, CONFIG_LOG_DEFAULT_LEVEL);

#define MY_PORT     4241

struct handle
{
	void* hup;
	void* drv;
	char buffer[RECV_BUFFER_SIZE];
};

void* init_hup_server(const struct app_api* api, void* arg1, void* arg2, void* arg3)
{
	struct handle* h;

	h = malloc(sizeof(struct handle));
	if (h == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	h->drv = api->init(arg1, arg2, arg3);
	h->hup = init_hupacket(NULL, api->send, h->drv);
	if (h->drv != NULL && h->hup != NULL)
	{
		int received;
		while (1)
		{
			received = api->recv(h->drv, h->buffer, sizeof(h->buffer));
			if (received < 0)
				NET_ERR("UDP : Connection error %d", errno);
			else if (received)
				process_hupacket(h->hup, h->buffer, received);
		}
	}
	LOG_ERR("can not start hupacket udp");
}
