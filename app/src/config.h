/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>
#include <stddef.h>

struct config {
	uint32_t ip;
	uint32_t nm;
	uint32_t gw;

	int baud;
	char parity;
	char data_bits;
	char stop_bits;
};

#endif // __CONFIG_H__
