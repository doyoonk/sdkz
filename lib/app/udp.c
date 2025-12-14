/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/udp.h>
#include <app/app_api.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/logging/log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>

LOG_MODULE_REGISTER(app_udp, CONFIG_LOG_DEFAULT_LEVEL);

struct handle
{
    int sock;
	struct sockaddr client;
	socklen_t client_sz;
};

static ssize_t _send_udp(void* user_data, const uint8_t* buffer, size_t size)
{
	struct handle* h = (struct handle*)user_data;
	ssize_t ret = sendto(h->sock, buffer, size, 0, &h->client, h->client_sz);

	if (ret < 0)
	{
		NET_ERR("UDP: Failed to send %d", errno);
		ret = -errno;
	}
	return ret;
}

static ssize_t _recv_udp(void* user_data, uint8_t* buffer, size_t size)
{
	ssize_t received;
	struct handle* h = (struct handle*)user_data;

	h->client_sz = sizeof(h->client);
	received = recvfrom(h->sock, buffer, size, 0,
		&h->client, &h->client_sz);

	if (received < 0)
	{
		NET_ERR("UDP: Failed to receive %d", errno);
		received = -errno;
	}
	return received;
}


static int _bind_udp(struct handle* h, struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	h->sock = zsock_socket(addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (h->sock < 0)
	{
		NET_ERR("Failed to create UDP socket (udp): %d", errno);
		return -errno;
	}

	ret = zsock_bind(h->sock, addr, addrlen);
	if (ret < 0)
	{
		close(h->sock);
		NET_ERR("Failed to bind UDP socket (udp): %d", errno);
		return -errno;
	}

	return 0;
}

static void* _init_udp(void* port, void* arg2, void* arg3)
{
	struct handle* h;
	struct sockaddr_in addr_in;

	h = malloc(sizeof(struct handle));
	if (h == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	(void)memset(&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons((int)port);
	addr_in.sin_addr.s_addr = htonl(INADDR_ANY);

	if (_bind_udp(h, (struct sockaddr *)&addr_in, (socklen_t)sizeof(addr_in)) != 0)
	{
		free(h);
		return NULL;
	}

	return h;
}

static void _deinit_udp(void* user_data)
{
	struct handle* h = (struct handle*)user_data;
	if (h->sock >= 0)
	{
		zsock_shutdown(h->sock, SHUT_RDWR);
		zsock_close(h->sock);
	}
	free(h);
}

const struct app_api udp_server =
{
	.init = _init_udp,
	.deinit = _deinit_udp,
	.recv = _recv_udp,
	.send = _send_udp
};
