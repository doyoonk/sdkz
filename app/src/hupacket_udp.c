/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(app, CONFIG_LOG_DEFAULT_LEVEL);

#include <app/udp.h>
#include <hu/hupacket.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>

#include <huerrno.h>
#include <stdio.h>

#define MY_PORT     4241

struct hup_udp_data
{
    int sock;
	struct sockaddr client;
	socklen_t client_len;
	char buffer[RECV_BUFFER_SIZE];

    void* h;
};

static ssize_t udp_send(const void* buffer, size_t size, void* user_data)
{
	struct hup_udp_data* data = (struct hup_udp_data*)user_data;
	ssize_t ret = sendto(data->sock, buffer, size, 0, &data->client, data->client_len);

	if (ret < 0)
	{
		NET_ERR("UDP: Failed to send %d", errno);
		ret = -errno;
	}
	return ret;
}

static void hup_udp_thread(void* arg1, void* arg2, void* arg3)
{
	struct hup_udp_data* data = (struct hup_udp_data*)arg1;
	int received;

	data->h = init_hupacket(NULL, udp_send, arg1);
	if (data->h != NULL)
	{
	    data->sock = init_udp(INADDR_ANY, MY_PORT);
		data->client_len = sizeof(data->client);
		while (1)
		{
			received = recvfrom(data->sock, data->buffer, sizeof(data->buffer), 0,
				&data->client, &data->client_len);

			if (received < 0)
				NET_ERR("UDP : Connection error %d", errno);
			else if (received)
				process_hupacket(data->h, data->buffer, received);
		}
	}
	LOG_ERR("can not start hupacket udp");
}

static struct hup_udp_data hup_udp_data;
K_THREAD_DEFINE(pudp, 1024, hup_udp_thread, (void*)&hup_udp_data, NULL, NULL, 5, 0, 0);
