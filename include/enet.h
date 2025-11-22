/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ENET_H__
#define __ENET_H__

#include <stdint.h>

extern void* enet_init(const uint8_t *hwaddr, const char *ipv6, const uint8_t *ipv4, const uint8_t *nm, const uint8_t *gw);
extern int init_vlan(void);
extern int init_tunnel(void);
extern int init_ws(void);

#endif
