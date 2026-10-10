/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
struct device { const char *name; bool ready; };
enum uart_event_type { UART_TX_DONE, UART_TX_ABORTED, UART_RX_RDY,
    UART_RX_BUF_REQUEST, UART_RX_BUF_RELEASED, UART_RX_DISABLED };
struct uart_event {
    enum uart_event_type type;
    union {
        struct { size_t len; } tx;
        struct { uint8_t *buf; size_t offset, len; } rx;
        struct { uint8_t *buf; } rx_buf;
    } data;
};
typedef void (*uart_callback_t)(const struct device *,struct uart_event *,void *);
typedef void (*uart_irq_callback_user_data_t)(const struct device *,void *);
static inline bool device_is_ready(const struct device *d) { return d->ready; }
int uart_callback_set(const struct device *,uart_callback_t,void *);
int uart_irq_callback_user_data_set(const struct device *,uart_irq_callback_user_data_t,void *);
int uart_rx_enable(const struct device *,uint8_t *,size_t,int);
int uart_rx_disable(const struct device *);
int uart_tx_abort(const struct device *);
int uart_rx_buf_rsp(const struct device *,uint8_t *,size_t);
int uart_tx(const struct device *,const uint8_t *,size_t,int);
int uart_irq_update(const struct device *);
int uart_irq_is_pending(const struct device *);
int uart_irq_rx_ready(const struct device *);
int uart_irq_tx_ready(const struct device *);
int uart_fifo_read(const struct device *,uint8_t *,int);
int uart_fifo_fill(const struct device *,const uint8_t *,int);
void uart_irq_rx_enable(const struct device *);
void uart_irq_tx_enable(const struct device *);
void uart_irq_rx_disable(const struct device *);
void uart_irq_tx_disable(const struct device *);
int uart_poll_in(const struct device *,uint8_t *);
void uart_poll_out(const struct device *,uint8_t);
