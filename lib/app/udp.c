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


LOG_MODULE_REGISTER(app_ddp, CONFIG_LOG_DEFAULT_LEVEL);

static int start_udp_proto(struct sockaddr *bind_addr, socklen_t bind_addrlen)
{
	int ret;
	int sock;

	sock = socket(bind_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		NET_ERR("Failed to create UDP socket (udp): %d", errno);
		return -errno;
	}

	ret = bind(sock, bind_addr, bind_addrlen);
	if (ret < 0)
	{
		close(sock);
		NET_ERR("Failed to bind UDP socket (udp): %d", errno);
		return -errno;
	}

	return sock;
}

int init_udp(uint32_t addr, int port)
{
	struct sockaddr_in addr_in;

	(void)memset(&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons(port);
	addr_in.sin_addr.s_addr = htonl(addr);

	return start_udp_proto((struct sockaddr *)&addr_in, sizeof(addr_in));
}
