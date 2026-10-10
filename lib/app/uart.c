/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/app_api.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/slist.h>

#include <stdint.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(app_uart, CONFIG_LOG_DEFAULT_LEVEL);

#define HUP_UART DT_ALIAS(hup_uart)

#if DT_NODE_HAS_STATUS(HUP_UART, okay)

#define RX_BUF_SIZE  		512
#define TX_BUF_SIZE  		512

#define UART_FIFO_SIZE		64

enum uart_mode { MODE_POLL, MODE_IRQ, MODE_ASYNC };

struct handle
{
	const struct device *dev;
	enum uart_mode mode;
	atomic_t stopping;
	struct k_sem tx_done;
	struct k_sem rx_done;
#if (defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API))
	sys_snode_t callback_node;
	uintptr_t callback_cookie;
	unsigned callbacks;
	bool detached;
	struct k_sem callbacks_idle;
	struct ring_buf tx_buffer;
	struct ring_buf rx_buffer;
	uint8_t tx_data[RX_BUF_SIZE];
	uint8_t rx_data[TX_BUF_SIZE];
#if CONFIG_UART_INTERRUPT_DRIVEN
	/* Serialize RX storage, pause/resume and stop to avoid a lost resume. */
	struct k_spinlock rx_lock;
	bool rx_paused;
#endif
#if CONFIG_UART_ASYNC_API
	atomic_t rx_active;
	atomic_t tx_active;
	struct k_sem rx_stopped;
	struct k_sem tx_stopped;
	uint8_t fifo_buffer[2][UART_FIFO_SIZE];
	uint8_t* fifo_next;
#endif
#endif
};

#if CONFIG_UART_ASYNC_API || CONFIG_UART_INTERRUPT_DRIVEN
/* Callbacks look up a live handle under the lock instead of dereferencing
 * driver user_data: even a callback queued before detachment is harmless.
 */
static sys_slist_t callback_handles = SYS_SLIST_STATIC_INIT(&callback_handles);
static struct k_spinlock callback_lock;
static uintptr_t next_callback_cookie;

static bool register_callbacks(struct handle *h)
{
	k_spinlock_key_t key = k_spin_lock(&callback_lock);
	struct handle *it;
	SYS_SLIST_FOR_EACH_CONTAINER(&callback_handles, it, callback_node) {
		if (it->dev == h->dev) {
			k_spin_unlock(&callback_lock, key);
			return false;
		}
	}
	/* Never reuse a cookie: a delayed event must not reach a reopened UART. */
	if (next_callback_cookie == UINTPTR_MAX) {
		k_spin_unlock(&callback_lock, key);
		return false;
	}
	h->callback_cookie = ++next_callback_cookie;
	sys_slist_append(&callback_handles, &h->callback_node);
	k_spin_unlock(&callback_lock, key);
	return true;
}

static struct handle *callback_enter(const struct device *dev, enum uart_mode mode, void *cookie)
{
	k_spinlock_key_t key = k_spin_lock(&callback_lock);
	struct handle *it;
	struct handle *h = NULL;
	SYS_SLIST_FOR_EACH_CONTAINER(&callback_handles, it, callback_node) {
		if (it->dev == dev && it->mode == mode && !it->detached &&
			it->callback_cookie == (uintptr_t)cookie) {
			h = it;
			h->callbacks++;
			break;
		}
	}
	k_spin_unlock(&callback_lock, key);
	return h;
}

static void callback_leave(struct handle *h)
{
	k_spinlock_key_t key = k_spin_lock(&callback_lock);
	if (--h->callbacks == 0) k_sem_give(&h->callbacks_idle);
	k_spin_unlock(&callback_lock, key);
}

static void wait_callbacks(struct handle *h)
{
	while (true) {
		k_spinlock_key_t key = k_spin_lock(&callback_lock);
		bool busy = h->callbacks != 0;
		k_spin_unlock(&callback_lock, key);
		if (!busy) return;
		k_sem_take(&h->callbacks_idle, K_FOREVER);
	}
}

static void detach_callbacks(struct handle *h)
{
	k_spinlock_key_t key = k_spin_lock(&callback_lock);
	h->detached = true;
	k_spin_unlock(&callback_lock, key);
	wait_callbacks(h);
	key = k_spin_lock(&callback_lock);
	sys_slist_find_and_remove(&callback_handles, &h->callback_node);
	k_spin_unlock(&callback_lock, key);
}
#endif

#if CONFIG_UART_ASYNC_API
static int _tx_from_queue(const struct device* dev, struct handle* h)
{
	uint8_t *data_ptr;
	uint32_t len = ring_buf_get_ptr(&h->tx_buffer, &data_ptr, 0);
	int err;

	if (len == 0) {
		return 0;
	}

	if (atomic_get(&h->stopping)) return -ECANCELED;
	atomic_set(&h->tx_active, 1);
	err = uart_tx(dev, data_ptr, len, SYS_FOREVER_US);
	if (err != 0) atomic_clear(&h->tx_active);
	return err == 0 ? (int)len : err;
}

void _async_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct handle* h = callback_enter(dev, MODE_ASYNC, user_data);
	if (h == NULL) return;
	switch (evt->type) {
	case UART_TX_DONE:
	case UART_TX_ABORTED:
		atomic_clear(&h->tx_active);
		k_sem_give(&h->tx_stopped);
		LOG_DBG("tx done: %d", evt->data.tx.len);
		ring_buf_consume(&h->tx_buffer, evt->data.tx.len);
		if(atomic_get(&h->stopping) || _tx_from_queue(dev, h) <= 0)
			k_sem_give(&h->tx_done);
		break;

	case UART_RX_RDY:
		if (atomic_get(&h->stopping)) break;
		LOG_DBG("rx rdy:%d/%d", evt->data.rx.offset, evt->data.rx.len);
		ring_buf_put(&h->rx_buffer, evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
		if (evt->data.rx.len > 0)
			k_sem_give(&h->rx_done);
		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("rx request");
		if (!atomic_get(&h->stopping)) uart_rx_buf_rsp(dev, h->fifo_next, UART_FIFO_SIZE);
		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("rx released");
		h->fifo_next = evt->data.rx_buf.buf;
		break;

	case UART_RX_DISABLED:
		atomic_clear(&h->rx_active);
		k_sem_give(&h->rx_stopped);
		LOG_DBG("rx disabled");
		break;
	
	default:
		LOG_DBG("unknown: %d", evt->type);
		break;
	}
	callback_leave(h);
}

static int _send_async(void* user_data, const uint8_t* data_ptr, size_t data_len)
{
	struct handle* h = (struct handle*)user_data;
	while(1)
	{
		if (atomic_get(&h->stopping)) return -ECANCELED;
		uint32_t written_to_buf = ring_buf_put(&h->tx_buffer, data_ptr, data_len);
		data_len -= written_to_buf;
		
		if(k_sem_take(&h->tx_done, K_NO_WAIT) == 0) {
			int err = _tx_from_queue(h->dev, h);

			if (err <= 0) {
				k_sem_give(&h->tx_done);
				if (err < 0) {
					return err;
				}
			}
		}

		if(data_len == 0) break;
		k_msleep(7);
		data_ptr += written_to_buf;
	}
	return data_len;
}
#endif

#if CONFIG_UART_INTERRUPT_DRIVEN
static int _read_irq_fifo(struct handle *h)
{
	uint8_t *bytes;
	k_spinlock_key_t key = k_spin_lock(&h->rx_lock);
	if (h->rx_paused || atomic_get(&h->stopping)) {
		k_spin_unlock(&h->rx_lock, key);
		return 0;
	}
	uint32_t available = ring_buf_put_ptr(&h->rx_buffer, &bytes, 0);
	int received = 0;
	if (available != 0) {
		/* Bound the time spent under the RX lock, including USB FIFO reads. */
		received = uart_fifo_read(h->dev, bytes, MIN(available, UART_FIFO_SIZE));
		if (received > 0) ring_buf_commit(&h->rx_buffer, received);
	}
	if (ring_buf_space_get(&h->rx_buffer) == 0) {
		h->rx_paused = true;
		uart_irq_rx_disable(h->dev);
	}
	k_spin_unlock(&h->rx_lock, key);
	if (received > 0) k_sem_give(&h->rx_done);
	return received;
}

static uint32_t _read_irq_buffer(struct handle *h, uint8_t *buffer, size_t len)
{
	k_spinlock_key_t key = k_spin_lock(&h->rx_lock);
	uint32_t received = ring_buf_get(&h->rx_buffer, buffer, len);
	if (h->rx_paused && received > 0 && !atomic_get(&h->stopping)) {
		h->rx_paused = false;
		uart_irq_rx_enable(h->dev);
	}
	k_spin_unlock(&h->rx_lock, key);
	return received;
}

static void _irq_cb(const struct device *dev, void *user_data)
{
	struct handle* h = callback_enter(dev, MODE_IRQ, user_data);
	if (h == NULL) return;
	while (!atomic_get(&h->stopping))
	{
		bool progressed = false;
		uart_irq_update(dev);
		if (uart_irq_is_pending(dev) <= 0)
			break;
		if (uart_irq_rx_ready(dev) > 0)
		{
			int rx_size = _read_irq_fifo(h);
			if (rx_size < 0)
			{
				LOG_ERR("uart fifo read error: %d", rx_size);
			}
			progressed = rx_size > 0;
		}

		if (uart_irq_tx_ready(dev) > 0)
		{
			uint8_t *data_ptr;
			uint32_t _claimed = ring_buf_get_ptr(&h->tx_buffer, &data_ptr, 0);
			if(_claimed > 0)
			{
				int sent = uart_fifo_fill(dev, data_ptr, _claimed);
				if (sent > 0)
				{
					ring_buf_consume(&h->tx_buffer, sent);
					k_sem_give(&h->tx_done);
					progressed = true;
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
		/* A stale ready flag or a full RX buffer must not spin in this
		 * callback. TX is serviced above even while RX is paused.
		 */
		if (!progressed) break;
	}
	callback_leave(h);
}

static int _send_int(void* user_data, const uint8_t* data_ptr, size_t data_len)
{
	struct handle* h = (struct handle*)user_data;
	while(1)
	{
		if (atomic_get(&h->stopping)) return -ECANCELED;
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
	for (size_t i = 0; i < data_len; i++) {
		if (atomic_get(&h->stopping)) {
			k_sem_give(&h->tx_done);
			return -ECANCELED;
		}
		uart_poll_out(h->dev, *data_ptr ++);
	}
	k_sem_give(&h->tx_done);
	return data_len;
}

static int _recv_poll(void* user_data, uint8_t* buffer, size_t len)
{
	struct handle* h = (struct handle*)user_data;
	size_t i;
	k_sem_take(&h->rx_done, K_FOREVER);
	if (atomic_get(&h->stopping)) return -ECANCELED;
	for (i = 0; i < len; i++, buffer ++)
	{
		if (uart_poll_in(h->dev, buffer) < 0)
			break;
	}
	k_sem_give(&h->rx_done);
	return i;
}

static struct handle* _init_handle(const struct device *dev, enum uart_mode mode) {
	struct handle* h;

	if (dev == NULL || !device_is_ready(dev))
	{
		LOG_ERR("Cannot bind %s", dev != NULL ? dev->name : "NULL");
		return NULL;
	}

	h = calloc(1, sizeof(struct handle));
	if (h == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	h->dev = dev;
	h->mode = mode;
	k_sem_init(&h->rx_done, 0, 1);
	k_sem_init(&h->tx_done, 1, 1);

#if CONFIG_UART_ASYNC_API || CONFIG_UART_INTERRUPT_DRIVEN
	k_sem_init(&h->callbacks_idle, 0, 1);
	ring_buf_init(&h->rx_buffer, sizeof(h->rx_data), h->rx_data);
	ring_buf_init(&h->tx_buffer, sizeof(h->tx_data), h->tx_data);
	if (!register_callbacks(h)) {
		LOG_ERR("UART %s already has an HU owner", dev->name);
		free(h);
		return NULL;
	}
#endif
#if CONFIG_UART_ASYNC_API
	k_sem_init(&h->rx_stopped, 0, 1);
	k_sem_init(&h->tx_stopped, 0, 1);
#endif
	return h;
}

static void _stop_uart(void* user_data)
{
	struct handle *h = user_data;
	atomic_set(&h->stopping, 1);
#if CONFIG_UART_INTERRUPT_DRIVEN
	if (h->mode == MODE_IRQ) {
		k_spinlock_key_t key = k_spin_lock(&h->rx_lock);
		h->rx_paused = true;
		uart_irq_rx_disable(h->dev);
		uart_irq_tx_disable(h->dev);
		k_spin_unlock(&h->rx_lock, key);
	}
#endif
	k_sem_give(&h->rx_done);
	k_sem_give(&h->tx_done);
}

static void _deinit_uart(void* user_data)
{
	struct handle *h = user_data;
	if (h == NULL) return;
	/* The owner has joined the I/O thread before calling deinit. */
	_stop_uart(h);
#if CONFIG_UART_ASYNC_API
	if (h->mode == MODE_ASYNC) {
		/* A callback already running when stop was requested may still be
		 * submitting RX/TX buffers. Let it finish before stopping DMA.
		 */
		wait_callbacks(h);
		int rc = uart_rx_disable(h->dev);
		if (rc == -EFAULT) atomic_clear(&h->rx_active);
		else if (rc != 0) {
			LOG_ERR("Cannot stop UART RX; retaining DMA buffers: %d", rc);
			return;
		}
		while (atomic_get(&h->rx_active)) k_sem_take(&h->rx_stopped, K_FOREVER);
		rc = uart_tx_abort(h->dev);
		if (rc == -EFAULT) atomic_clear(&h->tx_active);
		else if (rc != 0) {
			LOG_ERR("Cannot stop UART TX; retaining DMA buffers: %d", rc);
			return;
		}
		while (atomic_get(&h->tx_active)) k_sem_take(&h->tx_stopped, K_FOREVER);
		uart_callback_set(h->dev, NULL, NULL);
	}
#endif
#if CONFIG_UART_INTERRUPT_DRIVEN
	if (h->mode == MODE_IRQ) uart_irq_callback_user_data_set(h->dev, NULL, NULL);
#endif
#if CONFIG_UART_ASYNC_API || CONFIG_UART_INTERRUPT_DRIVEN
	detach_callbacks(h);
#endif
	free(h);
}

#if CONFIG_UART_ASYNC_API || CONFIG_UART_INTERRUPT_DRIVEN
static uint32_t _read_rx_buffer(struct handle *h, uint8_t *buffer, size_t len)
{
#if CONFIG_UART_INTERRUPT_DRIVEN
	if (h->mode == MODE_IRQ) return _read_irq_buffer(h, buffer, len);
#endif
	return ring_buf_get(&h->rx_buffer, buffer, len);
}

static int _recv_async_int(void* user_data, uint8_t* buffer, size_t len)
{
	struct handle* h = (struct handle*)user_data;
	if (len == 0) return 0;
	if (atomic_get(&h->stopping)) return -ECANCELED;
	uint32_t ret = _read_rx_buffer(h, buffer, len);
	while (ret == 0)
	{
		k_sem_take(&h->rx_done, K_FOREVER);
		if (atomic_get(&h->stopping)) return -ECANCELED;
		ret = _read_rx_buffer(h, buffer, len);
	}
	return ret;
}
#endif


#if CONFIG_UART_ASYNC_API
static void* _init_async(void* arg1, void* arg2, void* arg3)
{
	struct handle* h;
	const struct device* dev = (struct device*)arg1;

	if ((h = _init_handle(dev, MODE_ASYNC)) == NULL)
		return NULL;

	h->fifo_next = h->fifo_buffer[1];
	int rc = uart_callback_set(dev, _async_cb, (void *)h->callback_cookie);
	if (rc != 0) {
		detach_callbacks(h);
		free(h);
		return NULL;
	}
	atomic_set(&h->rx_active, 1);
	rc = uart_rx_enable(dev, h->fifo_buffer[0], UART_FIFO_SIZE, 3000);
	if (rc != 0) {
		atomic_clear(&h->rx_active);
		uart_callback_set(dev, NULL, NULL);
		detach_callbacks(h);
		free(h);
		return NULL;
	}

	return h;
}

const struct app_api uart_async =
{
	.init = _init_async,
	.deinit = _deinit_uart,
	.stop = _stop_uart,
	.recv = _recv_async_int,
	.send = _send_async
};
#endif


#if CONFIG_UART_INTERRUPT_DRIVEN
static void* _init_int(void* arg1, void* arg2, void* arg3)
{
	struct handle* h;
	const struct device* dev = (struct device*)arg1;

	if ((h = _init_handle(dev, MODE_IRQ)) == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	int rc = uart_irq_callback_user_data_set(dev, _irq_cb, (void *)h->callback_cookie);
	if (rc != 0) {
		detach_callbacks(h);
		free(h);
		return NULL;
	}
	uart_irq_rx_enable(dev);
	k_sem_reset(&h->tx_done);

	return h;
}

const struct app_api uart_interrupt =
{
	.init = _init_int,
	.deinit = _deinit_uart,
	.stop = _stop_uart,
	.recv = _recv_async_int,
	.send = _send_int
};
#endif


static void* _init_poll(void* dev, void* arg2, void* arg3)
{
	struct handle* h;

	if ((h = _init_handle((const struct device*)dev, MODE_POLL)) == NULL)
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
	.stop = _stop_uart,
	.recv = _recv_poll,
	.send = _send_poll
};

#else
#error "hup-uart device is disabled."
#endif
