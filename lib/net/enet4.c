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

void* enet4_init(struct net_if *iface, const uint8_t *ip, const uint8_t *nm, const uint8_t *gw) {
	if (IS_ENABLED(CONFIG_NET_IPV4) && net_if_flag_is_set(iface, NET_IF_IPV4) && ip != NULL) {
		struct in_addr addr = { .s_addr = *(uint32_t*)ip };
		struct net_if_addr *ifaddr = net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
		if (ifaddr) {
			LOG_INF("ip=%d.%d.%d.%d",
				ntohl(addr.s_addr) >> 24, (ntohl(addr.s_addr) >> 16) & 0xFF,
				(ntohl(addr.s_addr) >> 8) & 0xFF, ntohl(addr.s_addr) & 0xFF);
			if (nm) {
				struct in_addr netmask = { .s_addr = *(uint32_t*)nm };

				bool ret = net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);
				LOG_INF("nm=%d.%d.%d.%d, %d",
					ntohl(netmask.s_addr) >> 24, (ntohl(netmask.s_addr) >> 16) & 0xFF,
					(ntohl(netmask.s_addr) >> 8) & 0xFF, ntohl(netmask.s_addr) & 0xFF,
					ret);
			}
		} else {
			LOG_ERR("Failed to add IPv4 address %d.%d.%d.%d",
				ntohl(addr.s_addr) >> 24, (ntohl(addr.s_addr) >> 16) & 0xFF,
				(ntohl(addr.s_addr) >> 8) & 0xFF, ntohl(addr.s_addr) & 0xFF);
			return NULL;
		}
		if (gw != NULL) {
			addr.s_addr = *(uint32_t*)gw;
			net_if_ipv4_set_gw(iface, &addr);
			LOG_INF("gw=%d.%d.%d.%d",
				ntohl(addr.s_addr) >> 24, (ntohl(addr.s_addr) >> 16) & 0xFF,
				(ntohl(addr.s_addr) >> 8) & 0xFF, ntohl(addr.s_addr) & 0xFF);
		}
		return ifaddr;
	}
	return NULL;
}
