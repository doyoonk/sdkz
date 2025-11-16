/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(enet, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

void* enet6_init(struct net_if *iface, const uint8_t *ip) {
	if (IS_ENABLED(CONFIG_NET_IPV6) && net_if_flag_is_set(iface, NET_IF_IPV6) && ip != NULL) {
		struct in6_addr addr6;

		if (net_addr_pton(AF_INET6, ip, &addr6)) {
			LOG_ERR("Invalid ipv6 address: %s", ip);
			return NULL;
		}

		struct net_if_addr *ifaddr = net_if_ipv6_addr_add(iface, &addr6, NET_ADDR_MANUAL, 0);
		if (!ifaddr) {
			LOG_ERR("Cannot add %s to interface %p", ip, iface);
			return NULL;
		}
		LOG_INF("IPv6 address: %s", ip);
		return ifaddr;
	}
	return NULL;
}