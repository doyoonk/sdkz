/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(enet, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/net/net_if.h>

extern void* enet4_init(struct net_if *iface, const uint8_t *ip, const uint8_t *nm, const uint8_t *gw);
extern void* enet6_init(struct net_if *iface, const uint8_t *ip);

void* enet_init(const uint8_t *hwaddr, const char *ipv6, const uint8_t *ipv4, const uint8_t *nm, const uint8_t *gw) {
	struct net_if *iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("No default network interface found");
		return NULL;
	} else {
		LOG_INF("Using default network interface: %s", iface->if_dev->dev->name);
		if (hwaddr) {
			LOG_INF("Setting MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
				hwaddr[0], hwaddr[1], hwaddr[2],
				hwaddr[3], hwaddr[4], hwaddr[5]);
			net_if_set_link_addr(iface, (uint8_t *)hwaddr, 6, NET_LINK_ETHERNET);
		}

		(void)enet4_init(iface, ipv4, nm, gw);
		(void)enet6_init(iface, ipv6);
		return iface;
	}
}
