/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc. All rights reserved.
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
#include <zephyr/sys/atomic.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>

#if CONFIG_NET_L2_ETHERNET
LOG_MODULE_REGISTER(app_udp, CONFIG_LOG_DEFAULT_LEVEL);

struct handle
{
    int sock;
	atomic_t stopping;
};

static int _sendto_udp(void* user_data, const uint8_t* buffer, size_t size,
	const struct app_peer *peer)
{
	struct handle* h = (struct handle*)user_data;
	if (atomic_get(&h->stopping)) return -ECANCELED;
	struct sockaddr_in client = {
		.sin_family = AF_INET,
		.sin_port = peer->port,
		.sin_addr = { .s_addr = peer->ipv4_addr },
	};
	ssize_t ret = sendto(h->sock, buffer, size, 0,
		(const struct sockaddr *)&client, sizeof(client));

	if (ret < 0)
	{
		LOG_ERR("UDP: Failed to send %d", errno);
		ret = -errno;
	}
	return ret;
}

static int _recvfrom_udp(void* user_data, uint8_t* buffer, size_t size,
	struct app_peer *peer)
{
	ssize_t received;
	struct handle* h = (struct handle*)user_data;

	struct sockaddr_in client;
	socklen_t client_sz = sizeof(client);
	if (atomic_get(&h->stopping)) return -ECANCELED;
	received = recvfrom(h->sock, buffer, size, 0,
		(struct sockaddr *)&client, &client_sz);
	if (atomic_get(&h->stopping)) return -ECANCELED;
	if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return 0;

	if (received < 0)
	{
		LOG_ERR("UDP: Failed to receive %d", errno);
		received = -errno;
	}
	else if (received > 0) {
		peer->ipv4_addr = client.sin_addr.s_addr;
		peer->port = client.sin_port;
	}
	return received;
}


static int _bind_udp(struct handle* h, struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	h->sock = zsock_socket(addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (h->sock < 0)
	{
		LOG_ERR("Failed to create UDP socket (udp): %d", errno);
		return -errno;
	}

	ret = zsock_bind(h->sock, addr, addrlen);
	if (ret < 0)
	{
		close(h->sock);
		LOG_ERR("Failed to bind UDP socket (udp): %d", errno);
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
	atomic_clear(&h->stopping);

	(void)memset(&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons((int)port);
	addr_in.sin_addr.s_addr = htonl(INADDR_ANY);

	if (_bind_udp(h, (struct sockaddr *)&addr_in, (socklen_t)sizeof(addr_in)) != 0)
	{
		free(h);
		return NULL;
	}
	/* Periodically observe stop without closing a socket still used by recv. */
	struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };
	if (setsockopt(h->sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
		setsockopt(h->sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
		LOG_ERR("Failed to configure UDP I/O timeout: %d", errno);
		zsock_close(h->sock);
		free(h);
		return NULL;
	}

	return h;
}

static void _stop_udp(void* user_data)
{
	struct handle* h = user_data;
	atomic_set(&h->stopping, 1);
}

static void _deinit_udp(void* user_data)
{
	struct handle* h = (struct handle*)user_data;
	if (h->sock >= 0)
	{
		zsock_close(h->sock);
	}
	free(h);
}

const struct app_api udp_server =
{
	.init = _init_udp,
	.deinit = _deinit_udp,
	.stop = _stop_udp,
	.recvfrom = _recvfrom_udp,
	.sendto = _sendto_udp
};

#else

const struct app_api udp_server =
{
	.init = NULL,
	.deinit = NULL,
	.recv = NULL,
	.send = NULL
};

#endif
