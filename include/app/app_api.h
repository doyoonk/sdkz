/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __APP_API_H__
#define __APP_API_H__

#include <stdint.h>
#include <stddef.h>

struct device;

struct app_api
{
    void* (*init)(void* arg1, void* arg2, void* arg3);
    void (*deinit)(void* arg1);
    int (*recv)(void*, uint8_t*, size_t);
    int (*send)(void*, const uint8_t*, size_t);
};

extern const struct app_api udp_server;

extern const struct app_api uart_interrupt;
extern const struct app_api uart_async;
extern const struct app_api uart_polling;

#endif // __APP_API_H__
