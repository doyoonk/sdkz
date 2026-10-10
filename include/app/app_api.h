/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __APP_API_H__
#define __APP_API_H__

#include <stdint.h>
#include <stddef.h>

struct device;

/* IPv4 datagram endpoint. Address and port are both in network byte order. */
struct app_peer {
    uint32_t ipv4_addr;
    uint16_t port;
};

struct app_api
{
    void* (*init)(void* arg1, void* arg2, void* arg3);
    void (*deinit)(void* arg1);
    int (*recv)(void*, uint8_t*, size_t);
    int (*send)(void*, const uint8_t*, size_t);
    /* Cancel/wake I/O without freeing state. After the I/O thread exits,
     * deinit must stop callbacks/DMA before freeing the transport handle.
     */
    void (*stop)(void*);
    /* Datagram transports provide both hooks instead of recv/send. The peer
     * is copied by the caller; sendto never uses a mutable last-sender address.
     * recvfrom may return zero on an idle timeout for session maintenance.
     */
    int (*recvfrom)(void*, uint8_t*, size_t, struct app_peer*);
    int (*sendto)(void*, const uint8_t*, size_t, const struct app_peer*);
};

extern const struct app_api udp_server;

extern const struct app_api uart_interrupt;
extern const struct app_api uart_async;
extern const struct app_api uart_polling;

#endif // __APP_API_H__
