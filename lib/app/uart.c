/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/app_api.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(app_uart, CONFIG_LOG_DEFAULT_LEVEL);

#define HUP_UART DT_ALIAS(hup_uart)

#if DT_NODE_HAS_STATUS(HUP_UART, okay)

#define RX_BUF_SIZE  		512
#define TX_BUF_SIZE  		512

#define UART_FIFO_SIZE		64

struct handle
{
	const struct device *dev;
	struct k_sem tx_done;
	struct k_sem rx_done;
#if (defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API))
	struct ring_buf tx_buffer;
	struct ring_buf rx_buffer;
	uint8_t tx_data[RX_BUF_SIZE];
	uint8_t rx_data[TX_BUF_SIZE];
#if CONFIG_UART_ASYNC_API
	uint8_t fifo_buffer[2][UART_FIFO_SIZE];
	uint8_t* fifo_next;
	int bytes_send;
#endif
#endif
};

#if CONFIG_UART_ASYNC_API
static int _tx_from_queue(const struct device* dev, struct handle* h)
{
	uint8_t *data_ptr;

	h->bytes_send = ring_buf_get_claim(&h->tx_buffer, &data_ptr, TX_BUF_SIZE);
	if(h->bytes_send > 0)
		h->bytes_send = uart_tx(dev, data_ptr, h->bytes_send, SYS_FOREVER_MS);
	else
		h->bytes_send = 0;

	return h->bytes_send;
}

void _async_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct handle* h = (struct handle*)user_data;
	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("tx done: %d", evt->data.tx.len);
		ring_buf_get_finish(&h->tx_buffer, h->bytes_send);
		if(_tx_from_queue(dev, h) == 0)
			k_sem_give(&h->tx_done);
		break;

	case UART_RX_RDY:
		LOG_DBG("rx rdy:%d/%d", evt->data.rx.offset, evt->data.rx.len);
		ring_buf_put(&h->rx_buffer, evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
		if (evt->data.rx.len > 0)
			k_sem_give(&h->rx_done);
		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("rx request");
		uart_rx_buf_rsp(dev, h->fifo_next, UART_FIFO_SIZE);
		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("rx released");
		h->fifo_next = evt->data.rx_buf.buf;
		break;

	case UART_RX_DISABLED:
		LOG_DBG("rx disabled");
		break;
	
	default:
		LOG_DBG("unknown: %d", evt->type);
		break;
	}
}

static int _send_async(void* user_data, const uint8_t* data_ptr, size_t data_len)
{
	struct handle* h = (struct handle*)user_data;
	while(1)
	{
		uint32_t written_to_buf = ring_buf_put(&h->tx_buffer, data_ptr, data_len);
		data_len -= written_to_buf;
		
		if(k_sem_take(&h->tx_done, K_NO_WAIT) == 0)
			_tx_from_queue(h->dev, h);

		if(data_len == 0) break;
		k_msleep(7);
		data_ptr += written_to_buf;
	}
	return data_len;
}
#endif

#if CONFIG_UART_INTERRUPT_DRIVEN
static void _irq_cb(const struct device *dev, void *user_data)
{
	struct handle* h = (struct handle*)user_data;
	while (uart_irq_update(dev) && uart_irq_is_pending(dev))
	{
		if (uart_irq_rx_ready(dev))
		{
			int err;
			uint8_t* bytes;
			int _claimed = ring_buf_put_claim(&h->rx_buffer, &bytes, RX_BUF_SIZE);
			int rx_size = uart_fifo_read(dev, bytes, _claimed);
			if (rx_size < 0)
			{
				LOG_ERR("uart fifo read error: %d", rx_size);
			}
			else
			{
				if (rx_size > 0)
				{
					if ((err = ring_buf_put_finish(&h->rx_buffer, rx_size)) != 0)
						LOG_ERR("error rx ring buffer put:%d", err);
					else
						k_sem_give(&h->rx_done);
				}
			}
		}

		if (uart_irq_tx_ready(dev))
		{
			uint8_t *data_ptr;
			uint32_t _claimed = ring_buf_get_claim(&h->tx_buffer, &data_ptr, TX_BUF_SIZE);
			if(_claimed > 0)
			{
				int sent = uart_fifo_fill(dev, data_ptr, _claimed);
				if (sent > 0)
				{
					ring_buf_get_finish(&h->tx_buffer, sent);
					k_sem_give(&h->tx_done);
				}
			}
			else
			{
				if (ring_buf_is_empty(&h->tx_buffer))
				{
					uart_irq_tx_disable(dev);
				}
			}
		}
	}
}

static int _send_int(void* user_data, const uint8_t* data_ptr, size_t data_len)
{
	struct handle* h = (struct handle*)user_data;
	while(1)
	{
		uint32_t written_to_buf = ring_buf_put(&h->tx_buffer, data_ptr, data_len);
		data_len -= written_to_buf;
		
		uart_irq_tx_enable(h->dev);
		if(data_len == 0) break;
		k_sem_take(&h->tx_done, K_FOREVER);
		data_ptr += written_to_buf;
	}
	return data_len;
}
#endif

static int _send_poll(void* user_data, const uint8_t* data_ptr, size_t data_len)
{
	struct handle* h = (struct handle*)user_data;
	k_sem_take(&h->tx_done, K_FOREVER);
	for (size_t i = 0; i < data_len; i++)
		uart_poll_out(h->dev, *data_ptr ++);
	k_sem_give(&h->tx_done);
	return data_len;
}

static int _recv_poll(void* user_data, uint8_t* buffer, size_t len)
{
	struct handle* h = (struct handle*)user_data;
	size_t i;
	k_sem_take(&h->rx_done, K_FOREVER);
	for (i = 0; i < len; i++, buffer ++)
	{
		if (uart_poll_in(h->dev, buffer) < 0)
			break;
	}
	k_sem_give(&h->rx_done);
	return i;
}

static struct handle* _init_handle(const struct device *dev) {
	struct handle* h;

	if (dev == NULL || !device_is_ready(dev))
	{
		LOG_ERR("Cannot bind %s", dev->name);
		return NULL;
	}

	h = malloc(sizeof(struct handle));
	if (h == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	h->dev = dev;
	k_sem_init(&h->rx_done, 0, 1);
	k_sem_init(&h->tx_done, 1, 1);

#if CONFIG_UART_ASYNC_API || CONFIG_UART_INTERRUPT_DRIVEN
	ring_buf_init(&h->rx_buffer, sizeof(h->rx_data), h->rx_data);
	ring_buf_init(&h->tx_buffer, sizeof(h->tx_data), h->tx_data);
#endif
	return h;
}

static void _deinit_uart(void* h)
{
	if (h != NULL)
		free(h);
}

#if CONFIG_UART_ASYNC_API || CONFIG_UART_INTERRUPT_DRIVEN
static int _recv_async_int(void* user_data, uint8_t* buffer, size_t len)
{
	struct handle* h = (struct handle*)user_data;
	uint32_t ret = ring_buf_get(&h->rx_buffer, buffer, len);
	while (ret == 0)
	{
		k_sem_take(&h->rx_done, K_FOREVER);
		ret = ring_buf_get(&h->rx_buffer, buffer, len);
	}
	return ret;
}
#endif


#if CONFIG_UART_ASYNC_API
static void* _init_async(void* arg1, void* arg2, void* arg3)
{
	struct handle* h;
	const struct device* dev = (struct device*)arg1;

	if ((h = _init_handle(dev)) == NULL)
		return NULL;

	h->fifo_next = h->fifo_buffer[1];
	uart_callback_set(dev, _async_cb, h);
	uart_rx_enable(dev, h->fifo_buffer[0], UART_FIFO_SIZE, 3000);

	return h;
}

const struct app_api uart_async =
{
	.init = _init_async,
	.deinit = _deinit_uart,
	.recv = _recv_async_int,
	.send = _send_async
};
#endif


#if CONFIG_UART_INTERRUPT_DRIVEN
static void* _init_int(void* arg1, void* arg2, void* arg3)
{
	struct handle* h;
	const struct device* dev = (struct device*)arg1;

	if ((h = _init_handle(dev)) == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	uart_irq_callback_user_data_set(dev, _irq_cb, h);
	uart_irq_rx_enable(dev);
	k_sem_reset(&h->tx_done);

	return h;
}

const struct app_api uart_interrupt =
{
	.init = _init_int,
	.deinit = _deinit_uart,
	.recv = _recv_async_int,
	.send = _send_int
};
#endif


static void* _init_poll(void* dev, void* arg2, void* arg3)
{
	struct handle* h;

	if ((h = _init_handle((const struct device*)dev)) == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	k_sem_give(&h->rx_done);
	return h;
}

const struct app_api uart_polling =
{
	.init = _init_poll,
	.deinit = _deinit_uart,
	.recv = _recv_poll,
	.send = _send_poll
};

#else
#error "hup-uart device is disabled."
#endif
