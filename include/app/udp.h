/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __UDP_H__
#define __UDP_H__

#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>

#include <net/net_common.h>

struct udp_data
{
    struct net_data udp;

	struct sockaddr client;
	socklen_t client_len;
};

int init_udp(struct udp_data* data, uint32_t addr, int port);

#endif // __UDP_H__
