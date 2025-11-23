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


LOG_MODULE_DECLARE(app, CONFIG_LOG_DEFAULT_LEVEL);

static int start_udp_proto(struct net_data *udp, struct sockaddr *bind_addr, socklen_t bind_addrlen)
{
	int ret;

	udp->sock = socket(bind_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (udp->sock < 0)
	{
		NET_ERR("Failed to create UDP socket (udp): %d", errno);
		return -errno;
	}

	ret = bind(udp->sock, bind_addr, bind_addrlen);
	if (ret < 0)
	{
		NET_ERR("Failed to bind UDP socket (udp): %d", errno);
		return -errno;
	}

	udp->connected = true;
	return 0;
}

int init_udp(struct udp_data* data, uint32_t addr, int port)
{
	struct sockaddr_in addr_in;

	(void)memset(&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons(port);
	addr_in.sin_addr.s_addr = htonl(addr);

	data->udp.connected = false;
	data->udp.sock = -1;

	return start_udp_proto(&data->udp, (struct sockaddr *)&addr_in, sizeof(addr_in));
}
