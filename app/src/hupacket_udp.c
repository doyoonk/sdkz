/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/udp.h>
#include <hu/hupacket.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdio.h>

#define MY_PORT     4241

struct hup_udp_data
{
    struct udp_data ipv4;
    struct hup_handle h;
};

LOG_MODULE_DECLARE(app, CONFIG_LOG_DEFAULT_LEVEL);

static int udp_send(char* buffer, int size, void* user_data)
{
	struct udp_data* ipv4 = (struct udp_data*)user_data;
	int ret = sendto(ipv4->udp.sock, buffer, size, 0, &ipv4->client, ipv4->client_len);
	if (ret < 0) {
		NET_ERR("UDP: Failed to send %d", errno);
		ret = -errno;
	}
	return ret;
}

static void hup_udp_thread(void* arg1, void* arg2, void* arg3)
{
	struct hup_udp_data* data = (struct hup_udp_data*)arg1;
	int received;

    init_udp(&data->ipv4, INADDR_ANY, MY_PORT);

	init_hupacket(&data->h, udp_send, arg1);
	data->ipv4.client_len = sizeof(data->ipv4.client);
	while (1)
	{
		received = recvfrom(data->ipv4.udp.sock, data->ipv4.udp.buffer, sizeof(data->ipv4.udp.buffer), 0,
		&data->ipv4.client, &data->ipv4.client_len);

		if (received < 0)
			NET_ERR("UDP : Connection error %d", errno);
		else if (received)
			process_hupacket(&data->h, data->ipv4.udp.buffer, received);
	}
}

static struct hup_udp_data hup_udp_data;
K_THREAD_DEFINE(pudp, 1024, hup_udp_thread, (void*)&hup_udp_data, NULL, NULL, 5, 0, 0);
