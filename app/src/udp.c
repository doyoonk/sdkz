/*
 * Copyright (c) 2021 TiaC Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/hupacket.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(net_echo_server_sample, LOG_LEVEL_DBG);

#include <errno.h>
#include <stdio.h>


#define MY_PORT 4242

struct udp_data
{
    struct hu_data data;
   	struct sockaddr_in addr;
	struct hup_handle h;

	struct sockaddr client;
	socklen_t client_len;
};

static int start_udp_proto(struct hu_data *data, struct sockaddr *bind_addr, socklen_t bind_addrlen)
{
	int ret;

	data->sock = socket(bind_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (data->sock < 0)
	{
		NET_ERR("Failed to create UDP socket (udp): %d", errno);
		return -errno;
	}

	ret = bind(data->sock, bind_addr, bind_addrlen);
	if (ret < 0)
	{
		NET_ERR("Failed to bind UDP socket (udp): %d", errno);
		return -errno;
	}

	return 0;
}

static int udp_send(char* buffer, int size, void* user_data)
{
	struct udp_data* data = (struct udp_data*)user_data;
	int ret = sendto(data->data.sock, buffer, size, 0, &data->client, data->client_len);
	if (ret < 0) {
		NET_ERR("UDP: Failed to send %d", errno);
		ret = -errno;
	}
	return ret;
}

static void hup_udp_thread(void* arg1, void* arg2, void* arg3)
{
	struct udp_data* data = (struct udp_data*)arg1;
	int received;
    int ret;

	(void)memset(&data->addr, 0, sizeof(data->addr));
	data->addr.sin_family = AF_INET;
	data->addr.sin_port = htons(MY_PORT);
	data->addr.sin_addr.s_addr = htons(INADDR_ANY);

	data->data.connected = false;
	data->data.sock = -1;

	ret = start_udp_proto(&data->data, (struct sockaddr *)&data->addr, sizeof(data->addr));
	if (ret < 0)
		return;

	hup_init(&data->h, udp_send, arg1);
	data->client_len = sizeof(data.client);
	while (1)
	{
		received = recvfrom(data->data.sock, data->data.buffer, sizeof(data->data.buffer), 0,
		&data->client, &data->client_len);

		if (received < 0)
		{
			/* Socket error */
			NET_ERR("UDP : Connection error %d", errno);
		}
		else if (received)
		{
			process_hupacket(&data->h, data->data.buffer, received);
		}
	}
}

struct udp_data _udp_data;
K_THREAD_DEFINE(pudp, 1024, hup_udp_thread, (void*)&_udp_data, NULL, NULL, 5, 0, 0);
